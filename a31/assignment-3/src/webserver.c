#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/buffer.h>

//#include "redis_parse.h"
#include "http_parse.h"
//#include "urlencode.h"

typedef struct _ph
{
    int sockfd;
    int redis_sockfd;
    int index_num;
    int length;
    int state;
    int working;
    char *key;
    char *value;
    pthread_mutex_t lock;
    pthread_cond_t cond; 
} ph;

typedef struct _redis
{
    int result;
    int sockfd;
    int length;
    int working;
    struct bufferevent *sock_bev;
    int index_num;
} redis;
int task_queue[10000];
int end_queue=0;
int MAX_queue=10000;

struct bufferevent *redis_bev_list[40];
struct _redis *redis_info;

pthread_t *p_thread;
ph *p_info;
int pool_size = 10;
pthread_t provider_thread;
pthread_mutex_t provider_mutex;
pthread_cond_t provider_cond;

void read_cb(struct bufferevent *bev, void *ctx);
void provider_th();
void accept_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *address, int soclen, void *ctx);
void *thread_task(void *data);
void redis_read_cb(struct bufferevent *bev, void *ctx);
void redis_write_cb(struct bufferevent *bev, void *ctx);
void redis_event_cb(struct bufferevent *bev, short events, void *ptr);
void event_cb(struct bufferevent *bev, short events, void *ctx);


int main(int argc, char *argv[]) 
{
    struct sockaddr_in redisaddr;
    struct sockaddr_in serveraddr;
    int server_socket;
    int client_socket;
    struct event_base *base;
    struct evconnlistener *listener;
    
    if(argc < 4)
    {
        printf("Invalid arguments\n");
        return -1;
    }
    
    base = event_base_new();
     
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(atoi(argv[1]));

    memset(&redisaddr, 0, sizeof(redisaddr));
    redisaddr.sin_family = AF_INET;
    redisaddr.sin_addr.s_addr = inet_addr(argv[2]);
    redisaddr.sin_port = htons(atoi(argv[3]));

    redis_info = (redis *)malloc(sizeof(redis)*40);
    
    for(int i=0;i<40;i++)
    {   
        printf("redis %d\n", i);
        redis_bev_list[i] = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_setcb(redis_bev_list[i], redis_read_cb, redis_write_cb, redis_event_cb, &redis_info[i]);
        if(((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0))
        {
            printf("Create Client Error\n");
            return -1;
        }
        if(connect(client_socket, (struct sockaddr *)&redisaddr, sizeof(redisaddr))<0)
        {
            printf("Redis server connect failed\n");
            return -1;
        }
        redis_bev_list[i] = bufferevent_socket_new(base, client_socket, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_setcb(redis_bev_list[i], redis_read_cb, NULL, NULL, &redis_info[i]); 
        redis_info[i].sockfd = -1;
        redis_info[i].result = -1;
        redis_info[i].index_num = i;
        redis_info[i].working = -1;
        bufferevent_disable(redis_bev_list[i], EV_WRITE);
    }
    
    p_thread = (pthread_t *)malloc(sizeof(pthread_t)*pool_size);
    p_info = (ph *)malloc(sizeof(ph)*pool_size);

    for(int i=0;i<pool_size;i++)
    {
        p_info[i].sockfd = -1;
        p_info[i].redis_sockfd = -1;
        p_info[i].index_num = i;
        p_info[i].working = -1;
        pthread_cond_init(&(p_info[i].cond), NULL);
        pthread_mutex_init(&(p_info[i].lock), NULL);
        if(pthread_create(&p_thread[i], NULL, thread_task, (void *)&p_info[i])<0)
        {
            printf("thread create failed\n");
            return -1;
        }

    }

    pthread_create(&provider_thread, NULL, &provider_th, NULL);
    pthread_mutex_init(&provider_mutex, NULL);
    pthread_cond_init(&provider_cond, NULL);

    listener = evconnlistener_new_bind(base, accept_cb, NULL, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
    if(!listener)
    {
        printf("Listener create error\n");
        return -1;
    }

    event_base_dispatch(base);
    free(p_thread);
    free(p_info);
    return 0;
}

void redis_event_cb(struct bufferevent *bev, short events, void *ptr)
{
    if(events & BEV_EVENT_CONNECTED)
      printf("REDIS_CONNECTED\n");
    else if(events & BEV_EVENT_ERROR)
      printf("REDIS_ERROR\n");
}

void provider_th()
{
    int fd;
    int state;
    while(1)
    {   
        state=0;
        if(end_queue == 0)
        {
            pthread_mutex_lock(&provider_mutex);
            pthread_cond_wait(&provider_cond, &provider_mutex);
            pthread_mutex_unlock(&provider_mutex);
        }
        else
        {
            fd = task_queue[end_queue-1];
            end_queue-=1;
            while(state==0)
            {
                for(int i=0;i<pool_size;i++)
                {
                    if(p_info[i].working==-1)
                    {
                        p_info[i].sockfd = fd;
                        pthread_cond_signal(&p_info[i].cond);
                        pthread_mutex_unlock(&p_info[i].lock);
                        printf("thread %d picked for %d\n", i, fd);
                        state=1;
                        p_info[i].working=0;
                        break;
                    }
                    //if(pthread_mutex_trylock(&(p_info[i].lock))==0)
                    //{
                    //    p_info[i].sockfd = fd;
                    //    pthread_mutex_unlock(&(p_info[i].lock));
                    //    pthread_cond_signal(&(p_info[i].cond));
                    //    printf("thread %d picked for %d\n", i, fd);
                    //    state=1;
                    //    break;
                    //}
                }
            }
        }
    }
}

void accept_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *address, int soclen, void *ctx)
{
    if(end_queue==MAX_queue)
    {
       while(end_queue==MAX_queue)
       {
       }
    }
    printf("%d\n", fd);
    task_queue[end_queue] = fd;
    end_queue++;
    if(end_queue==1)
    {
        pthread_cond_signal(&provider_cond);
    }
}

void *thread_task(void *data)
{
    struct event_base *base;
    ph *thread_info;
    thread_info = data;
    struct bufferevent *bev;
    int fd = thread_info->sockfd;
    base = event_base_new();
    printf("thread created %d\n", thread_info->index_num);
    while(1)
    {
        pthread_mutex_lock(&(thread_info->lock));
        pthread_cond_wait(&(thread_info->cond), &(thread_info->lock));
        pthread_mutex_unlock(&(thread_info->lock));
        thread_info = data;
        fd = thread_info->sockfd;
        if(fd>0)
        {
            bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
            if(!bev)
                printf("bev error %d\n", fd);
            bufferevent_setcb(bev, read_cb, NULL, event_cb, (void *)thread_info);
            //printf("setcb\n");
            bufferevent_enable(bev, EV_READ|EV_WRITE); 
            //printf("enabled \n");
            event_base_dispatch(base);
        }
    }
}
void event_cb(struct bufferevent *bev, short events, void *ctx)
{
    if(events & BEV_EVENT_ERROR)
      printf("bufferevent error\n");
    if(events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
      bufferevent_free(bev);
}

void read_cb(struct bufferevent *bev, void *ctx)
{
    char* line = (char*)malloc(4096);
    char* name = (char*)malloc(1024);
    char* value = (char*)malloc(1024);
    char* method = (char*)malloc(1024);
    char* target = (char*)malloc(2048);
    char* response = (char*)malloc(1024);
    short enabled;
    int read_len;
    int size = 0;
    int state = 0;
    int redis_state = 0;
    int content_length;
    int working;
    int check;
    struct evbuffer *input;
    input = evbuffer_new();
    struct evbuffer *output;
    output = evbuffer_new();
    int state_host=0;
    int state_length=0;
    printf("read\n");
    input = bufferevent_get_input(bev);
    struct _ph *thread_info;
    thread_info = (struct _ph *)ctx;
    working = thread_info->working;
    if(evbuffer_get_length(input)<10000)
    {
        printf("%d\n", evbuffer_get_length(input));
        return ;
    }



    if(working==0)
    {
        while(1)
        {
            evbuffer_remove(input, (line+size), 1);
            if(size > 0)
            {
                if(line[size] == '\n' && line[size-1] == '\r')
                {
                    line[size-1] = '\0';
                    //printf("%s\n", line);
                    if(size == 1)
                    {
                        state = 2;
                        break;
                    }
                    else if(state == 0)
                    {   
                        check = http_parse_first_line(line, method, target);
                        //printf("method %s target %s\n", method, target);
                        state = 1;
                        if(check < 0)
                        {   
                            free(line);
                            free(name);
                            free(value);
                            sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
                            bufferevent_write(bev, response, strlen(response));
                            free(method);
                            free(target);
                            free(response);
                            return ;
                        }
                        size = -1;
                    }
                    else if(state == 1)
                    {
                        check = http_parse_header_line(line, name, value);
                        //printf("%s\n", line);
                        if(check < 0)
                        {
                            sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
                            bufferevent_write(bev, response, strlen(response));
                            free(line);
                            free(name);
                            free(value);
                            free(method);
                            free(target);
                            free(response);
                            return ;
                        }
                        if(!strcmp(name, "Host"))
                        {
                            state_host = 1;
                        }
                        else if(!strcmp(name, "Content-Length") || !strcmp(name, "Content-length"))
                        {
                            state_length = 1;
                            content_length = atoi(value);
                            thread_info->length = content_length;
                            //printf("%d\n", content_length);
                        }
                        size=-1;
                    }
                }
            }
            size++;
        }
        free(line);
        free(name);
        free(value);

    /* ERROR CASES */
    
        working = 1;
    }

    //bufferevent_disable(bev, EV_WRITE); 
    if(working == 1)
    {
        if(!strcmp(method, "POST"))
        {  
            printf("%s\n", "POST"); 
            char key[1032];
            char *value=(char *)malloc(1025*1024*4);
            int key_size;
            int value_size;
            size = 0;
            state=thread_info->state;
            int redis_response;
            int i;
            int n;
            int redis_index;
            struct bufferevent *redis_bev;
            int len = thread_info->length;
            redis_state=0;
            if(thread_info->state<0)
                state=1;

            //free(target);
            /*
            if(content_length == 0)
            {
                sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
                bufferevent_write(bev, response, strlen(response));
                free(target);
                free(response);
                thread_info->working = -1;
                return ;
            }
            */
            printf("len %d\n", len);
            printf("inptut %d\n", evbuffer_get_length(input));
            while(len)
            {
                printf("len %d\n", len);
                printf("input %d\n", evbuffer_get_length(input));

                if(state==1)
                {
                    n = evbuffer_remove(input, key+size, 1);
                    printf("%s\n", key);
                }
                else if(state==2)
                {
                    n = evbuffer_remove(input, value+size, 1);
                    printf("%c", *(value+size));
                }
                printf("%d\n", n);
                if(n<=0)
                {
                    thread_info->length = len;
                    thread_info->working = working;
                    thread_info->state = state;
                    return ;
                }
                len--;
                if(*(key+size) == '=' && state==1)
                {
                    *(key+size)='\0';
                    key_size = size;
                    size = 0;
                    state=2;
                    redis_state=0;
                    redis_index = -1;
                    while(redis_state == 0)
                    {
                        for(int j=0;j<40;j++)
                        {
                            if(redis_info[j].working == -1)
                            {
                                redis_bev = redis_bev_list[j];
                                bufferevent_enable(redis_bev, EV_WRITE | EV_READ);
                                redis_state=1;
                                redis_index=j;
                                printf("%d\n", redis_info[j].index_num);
                                redis_info[j].working=0;
                                break;
                            }
                            printf("asd\n");
                        }
                        if(redis_index>=0)
                        printf("redis %d is picked\n", redis_index);
                        redis_info[redis_index].sock_bev = bev;
                        redis_info[redis_index].working = 0;
                    }
                    sprintf(response, "*3\r\n$3\r\nSET\r\n$%d\r\n", key_size);
                    printf("%s", response);
                    bufferevent_write(redis_bev, response, strlen(response));
                    bufferevent_write(redis_bev, key, key_size);
                    printf("%s\n", key);
                    bufferevent_write(redis_bev, "\r\n", 2);
                }
                else if(*(value+size) == '&' && state==2)
                {
                    //printf("value\n");
                    *(value+size)='\0';
                    value_size = size;
                    printf("in for %d\n", value_size);
                    size=0;
                    sprintf(response, "$%d\r\n", value_size);
                    printf("%s", response);
                    bufferevent_write(redis_bev, response, strlen(response));
                    bufferevent_write(redis_bev, value, value_size);
                    printf("%s\n", value);
                    sprintf(response, "\r\n", 2);
                    bufferevent_write(redis_bev, response, 2);
                    while(redis_info[redis_index].result < 0)
                    {
                        printf("");
                    }
                    if(redis_info[redis_index].result == 0)
                    {
                        /* error */
                        printf("upload fail\n");
                        return ;
                    }
                    redis_info[redis_index].result=-1;
                    redis_info[redis_index].working=-1;
                    state=1;
                }
                else
                    size++;
            }
            if(state == 2)
            {
                *(value+size)='\0';
                value_size = size;
                printf("out for %d\n", value_size);
                sprintf(response, "$%d\r\n", value_size);
                printf("%s", response);
                bufferevent_write(redis_bev, response, strlen(response));
                bufferevent_write(redis_bev, value, value_size);
                printf("%s\n", value);
                sprintf(response, "\r\n", 2);
                bufferevent_write(redis_bev, response, 2);

                while(redis_info[redis_index].result < 0)
                {
                    printf("");
                }
                if(redis_info[redis_index].result == 0)
                {
                    /* error */
                    printf("upload fail\n");
                    return ;
                }
                redis_info[redis_index].result=-1;
                redis_info[redis_index].working=0;
            }
            sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nOK");
            printf("%s\n", response);
            bufferevent_write(bev, response, strlen(response));
            thread_info->state=-1;
            thread_info->length=-1;
            thread_info->working=-1;
            free(response);
            free(value);
            free(method);
        
            thread_info->working=0;
        
            //bufferevent_free(bev);
            return ;
        }
        else if(!strcmp("GET", method))
        {
            int redis_index;
            struct bufferevent *redis_bev;
            redis_state=0;
            while(redis_state == 0)
            {
                for(int j=0;j<40;j++)
                {
                    enabled = bufferevent_get_enabled(redis_bev_list[j]);
                    if(redis_info[j].working == -1)
                    {
                        redis_bev = redis_bev_list[j];
                        bufferevent_enable(redis_bev, EV_WRITE | EV_READ);
                        redis_state=1;
                        redis_index=j;
                        redis_info[j].working=0;
                        break;
                    }
                }
                if(redis_index>=0)
                printf("redis %d is picked\n", redis_index);
                redis_info[redis_index].sock_bev = bev;
                redis_info[redis_index].working = 0;
                redis_info[redis_index].length = -1;
            }

            sprintf(response, "*2\r\n$3\r\nGET\r\n$%d\r\n", strlen(target));
            bufferevent_write(redis_bev, response, strlen(response));
            bufferevent_write(redis_bev, target, strlen(target));
            printf("%s\n", target);
            bufferevent_write(redis_bev, "\r\n", 2);
            //while(redis_info[redis_index].result < 0)
            //{
            //}
            //if(redis_info[redis_index].result == 0)
            //{
            //    printf("invalid\n"); 
            //}
        }
        else
        {
             /* error */
        }
        //bufferevent_enable(bev, EV_WRITE);
    }
}


void redis_read_cb(struct bufferevent *bev, void *ctx)
{
    struct evbuffer *output;
    output = evbuffer_new();
    struct _redis *redis_server;
    int size = 0;
    char data[10];
    char *response = (char *)malloc(4096);
    redis_server = (struct _redis *)ctx;
    output = bufferevent_get_input(bev);
    int working = redis_server->working;
    int length = redis_server->length;
    int len=evbuffer_get_length(output);
    int part_len;
    printf("redis read %d\n", len);
    if(working=0)
    {
        evbuffer_remove(output,response,1);
        printf("%s\n", response);
        if(response[0] == '+')
        {
            redis_info[redis_server->index_num].result = 1;
            redis_info[redis_server->index_num].length = -1;
            len = evbuffer_get_length(output);
            printf("%d\n", len);
            evbuffer_drain(output, len);
        }   
        else if(response[0] == '$')
        {
            size = 0;
            while(1)
            {
                evbuffer_remove(output, data+size, 1);
                if(size > 0)
                    if(data[size] == '\n' && data[size-1] == '\r')
                    {
                        data[size-1]='\0';
                        break; 
                    }
                size++;
            }
            len = atoi(data);
            printf("%d\n", len);

            if(atoi(data) < 0)
            {
                redis_info[redis_server->index_num].result=0;
                redis_info[redis_server->index_num].length=-1;
                printf("error\n");
                return ;
            }
            redis_info[redis_server->index_num].length=len;
        }
        working=1;
        redis_info[redis_server->index_num].length = atoi(data);
        sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n", atoi(data));
        bufferevent_write(redis_server->sock_bev, response, strlen(response));
        printf("%s\n", response);
    }
    if(working=1)
    {
        len = redis_info[redis_server->index_num].length;
        part_len = evbuffer_get_length(output);
        if(part_len<=len)
        {
            bufferevent_write_buffer(redis_server->sock_bev, output);
            len=len-part_len;
            if(len==0)
            {
                redis_server->working=-1;
                redis_server->length=-1;
            }
        }
        else
        {
            bufferevent_write(output, response, part_len);
            bufferevent_write(redis_server->sock_bev, response, part_len);
            len=0;
            redis_server->working=-1;
            redis_server->length=-1;
        }
    }
    free(response);
}

void redis_write_cb(struct bufferevent *bev, void *ctx)
{
    struct evbuffer *output;

}
