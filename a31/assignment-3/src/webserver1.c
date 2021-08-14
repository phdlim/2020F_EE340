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
#include <fcntl.h>

#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/buffer.h>

#include "http_parse.h"

typedef struct _ph
{
    int sockfd;
    struct bufferevent *sock_bev;
    int redis_index;
    int length;
    int state;
    int working;
    int index_num;
    char key[1032];
    int key_size;
    int shift;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} ph;

typedef struct _redis
{
    int result;
    int sockfd;
    int length;
    int redisfd;
    int working;
    struct bufferevent *sock_bev;
} redis;

int task_queue[10000];
int front_queue = 0;
int end_queue = 0;
int MAX_queue=10000;

struct bufferevent *redis_bev_list[40];
struct _redis *redis_info;

pthread_t *p_thread;
ph *p_info;

pthread_t provider_thread;
pthread_mutex_t provider_mutex;
pthread_cond_t provider_cond;

/* server event callback */

void event_cb(struct bufferevent *bev, short events, void *ptr)
{
    if (events & BEV_EVENT_CONNECTED) {
         printf("Connect okay.\n");
    } 
    else if (events & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) 
    {
         struct event_base *base = ptr;
         if (events & BEV_EVENT_ERROR) 
         {
         }
         printf("Closing\n");
         bufferevent_free(bev);
         event_base_loopexit(base, NULL);
    }
}

/* redis event callback */

void redis_event_cb(struct bufferevent *bev, short events, void *ptr)
{
    if (events & BEV_EVENT_CONNECTED) {
         printf("Connect okay.\n");
    } 
    else if (events & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) 
    {
         struct event_base *base = ptr;
         if (events & BEV_EVENT_ERROR) 
         {
         }
         printf("Closing\n");
         bufferevent_free(bev);
         event_base_loopexit(base, NULL);
    }
}

/* write event callback */
/* If write is done. close client connection of working thread and set working thread is not working now. */

void write_cb(struct bufferevent *bev, void *info)
{
    struct _ph *thread_info;
    thread_info = info;
    bufferevent_free(bev);
    printf("close connection %d of thread %d\n", thread_info->sockfd, thread_info->index_num);
    thread_info->working=-1;
}

/* read event callback */

void read_cb(struct bufferevent *bev, void *info)
{
    struct _ph *thread_info;
    
    char* line = (char *)malloc(4096);
    char* name = (char *)malloc(1024);
    char* value = (char *)malloc(1024);
    char* method = (char *)malloc(1024);
    char* target = (char *)malloc(1032);
    char* response = (char *)malloc(1024);
    
    int state=0;
    int check;
    int size=0;
    int state_host=0;
    int state_length=0;

    struct evbuffer *input;

    thread_info = info;


    input = bufferevent_get_input(bev);

    if(thread_info->working == 0) //configure header of http request
    {
        while(1)
        {
            evbuffer_remove(input, line+size, 1);
            if(size > 0)
            {
                if(line[size] == '\n' && line[size-1] == '\r')
                {
                    line[size-1] = '\0';
                    if(size == 1)
                    {
                        break;
                    }
                    else if(state==0)
                    {
                        check = http_parse_first_line(line, method, target);
                        if(check<0)
                        {
                            printf("error\n");
                            free(line);
                            free(name);
                            free(value);
                            free(method);
                            free(target);
                            sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
                            bufferevent_write(bev, response, strlen(response)); 
                            free(response);
                            bufferevent_enable(bev, EV_WRITE);
                            return ;
                        }
                        state = 1;
                        size = -1;
                    }
                    else if(state == 1)
                    {
                        check = http_parse_header_line(line, name, value);
                        if(check < 0)
                        {
                            printf("error\n");
                            free(line);
                            free(name);
                            free(value);
                            free(method);
                            free(target);
                            sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
                            bufferevent_write(bev, response, strlen(response)); 
                            bufferevent_enable(bev, EV_WRITE);
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
                            thread_info->length = atoi(value);
                        }
                        size = -1;
                    }
                }
            }
            size++;
        }

        free(line);
        free(name);
        free(value);
        
        if(state_host!=1)
        {
            printf("no host\n");
            free(method);
            free(target);
            sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
            bufferevent_write(bev, response, strlen(response)); 
            bufferevent_enable(bev, EV_WRITE);
            free(response);
            return ;
        }
        if(!strcmp(method, "POST"))
        { 
            //printf("post method\n");
            if(state_length!=1 || thread_info->length == 0)
            {
                printf("no content\n");
                free(method);
                free(target);
                sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
                bufferevent_write(bev, response, strlen(response)); 
                bufferevent_enable(bev, EV_WRITE);
                free(response);
                return ;
            }   
            thread_info->working = 1;
            thread_info->state = 1;
        }
        else if(!strcmp(method, "GET"))
        {
            //printf("get method\n");
            free(method);
            thread_info->working = 2;
        }
        else
        {
            printf("invalid method\n");
            free(method);
            free(target);
            sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
            bufferevent_write(bev, response, strlen(response)); 
            free(response);
            bufferevent_enable(bev, EV_WRITE);
            return ;
        }
    }

    if(thread_info->working == 1)
    {
        //printf("POST\n");
        char *value = (char *)malloc(4096);
        int len;
        char *key = thread_info->key;
        int key_size = thread_info->key_size;
        int value_size;
        int redis_state;
        int redis_index;
        state=0;
        struct bufferevent *redis_bev;
        len = thread_info->length;
        size=0;
        if(thread_info->state == 1)
            size=thread_info->key_size;

        while(len)
        {
            if(thread_info->state == 1)
            {
                evbuffer_remove(input, key+size, 1);
            }
            else if(thread_info->state == 2)
            {
                evbuffer_remove(input, value+size, 1);
            }
            len--;
            if(*(key+size) == '=' && thread_info->state == 1)
            {
                *(key+size) = '\0';
                key_size = size;
                //printf("key %s\n", key);
                size = 0;
                thread_info->state = 2;
                redis_state = 0;
                while(redis_state == 0)
                {
                    for(int j=0;j<40;j++)
                    {
                        if(redis_info[j].working == -1)
                        {
                            redis_bev = redis_bev_list[j];
                            bufferevent_disable(redis_bev, EV_WRITE | EV_READ);
                            redis_state=1;
                            redis_info[j].working=0;
                            thread_info->redis_index = j;
                            break;
                        }
                    }
                }
                state=1;
                redis_index = thread_info->redis_index;
                if(redis_index>=0)
                printf("redis1 %d is picked\n", redis_index);
                redis_info[redis_index].sock_bev = bev;
                thread_info->key_size = key_size;
            }
            else if(*(value+size) == '&' && thread_info->state == 2)
            {
                //printf("value\n");
                *(value+size)='\0';
                value_size = size;
                //printf("in for %d\n", value_size);
                size=0;

                if(state!=1)
                {
                    redis_state = 0;
                    while(redis_state == 0)
                    {
                        for(int j=0;j<40;j++)
                        {
                            if(redis_info[j].working == -1)
                            {
                                redis_bev = redis_bev_list[j];
                                bufferevent_disable(redis_bev, EV_WRITE | EV_READ);
                                redis_state=1;
                                redis_info[j].working=0;
                                thread_info->redis_index = j;
                                redis_index = j;
                                printf("redis2 %d is picked\n", redis_index);
                                break;
                            }
                        }
                    }
                }
                sprintf(response, "*3\r\n$6\r\nAPPEND\r\n$%d\r\n", thread_info->key_size);
                //printf("%s", response);
                bufferevent_write(redis_bev, response, strlen(response));
                bufferevent_write(redis_bev, thread_info->key, thread_info->key_size);
                //printf("%s\n", thread_info->key);
                bufferevent_write(redis_bev, "\r\n", 2);
                sprintf(response, "$%d\r\n", value_size);
                //printf("%s", response);
                bufferevent_write(redis_bev, response, strlen(response));
                bufferevent_write(redis_bev, value, value_size);
                //printf("%s\n", value);
                bufferevent_write(redis_bev, "\r\n", 2);
                bufferevent_enable(redis_bev, EV_READ|EV_WRITE);
                while(redis_info[thread_info->redis_index].result < 0 && redis_info[redis_index].working >= 0)
                {
                }
                if(redis_info[thread_info->redis_index].result == 0)
                {
                    printf("upload fail\n");
                    sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
                    bufferevent_write(bev, response, strlen(response)); 
                    free(response);
                    bufferevent_enable(bev, EV_WRITE);
                    return ;
                }
                redis_info[thread_info->redis_index].result=-1;
                redis_info[thread_info->redis_index].working=-1;
                thread_info->state=1;
                thread_info->key_size = 0;
            }
            else if(evbuffer_get_length(input) == 0 && len > 0 && thread_info->state == 2)
            {
                size++;
                if(state!=1)
                {
                    redis_state = 0;
                    while(redis_state == 0)
                    {
                        for(int j=0;j<40;j++)
                        {
                            if(redis_info[j].working == -1)
                            {
                                redis_bev = redis_bev_list[j];
                                bufferevent_disable(redis_bev, EV_WRITE | EV_READ);
                                redis_state=1;
                                redis_info[j].working=0;
                                thread_info->redis_index = j;
                                redis_index = j;
                                printf("redis3 %d is picked\n", redis_index);
                                break;
                            }
                        }
                    }
                }
                value_size = size; 
                sprintf(response, "*3\r\n$6\r\nAPPEND\r\n$%d\r\n", thread_info->key_size);
                //printf("%s", response);
                bufferevent_write(redis_bev, response, strlen(response));
                bufferevent_write(redis_bev, thread_info->key, thread_info->key_size);
                //printf("%s\n", thread_info->key);
                bufferevent_write(redis_bev, "\r\n", 2);
                sprintf(response, "$%d\r\n", value_size);
                //printf("%s", response);
                bufferevent_write(redis_bev, response, strlen(response));
                bufferevent_write(redis_bev, value, value_size);
                //printf("%s\n", value);
                bufferevent_write(redis_bev, "\r\n", 2);
                bufferevent_enable(redis_bev, EV_READ|EV_WRITE);
                thread_info->length = len;
                while(redis_info[thread_info->redis_index].result < 0 && redis_info[redis_index].working >= 0)
                {
                }
                if(redis_info[thread_info->redis_index].result == 0)
                {
                    printf("upload fail\n");
                    sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
                    bufferevent_write(bev, response, strlen(response)); 
                    free(response);
                    bufferevent_enable(bev, EV_WRITE);

                    return ;
                }
                redis_info[thread_info->redis_index].result=-1;
                redis_info[thread_info->redis_index].working=-1;
                thread_info->state=2;
                return ;
            }
            else if(evbuffer_get_length(input) == 0 && len > 0 && thread_info->state == 1)
            {
                thread_info->key_size = size+1;
                thread_info->length = len;
                return ;
            }
            else
                size++;
        }
        if(thread_info->state == 2 && len == 0)
        {   
            *(value+size)='\0';
            redis_state = 0;
            if(state!=1)
            {
                while(redis_state == 0)
                {
                    for(int j=0;j<40;j++)
                    {
                        if(redis_info[j].working == -1)
                        {
                            redis_bev = redis_bev_list[j];
                            bufferevent_disable(redis_bev, EV_WRITE | EV_READ);
                            redis_state=1;
                            redis_info[j].working=0;
                            thread_info->redis_index = j;
                            redis_index = j;
                            printf("redis4 %d is picked\n", redis_index);
                            break;
                        }
                    }   
                }
            }

            value_size = size;
            //printf("in for %d\n", value_size);
            size=0;
            sprintf(response, "*3\r\n$6\r\nAPPEND\r\n$%d\r\n", thread_info->key_size);
            //printf("%s", response);
            bufferevent_write(redis_bev, response, strlen(response));
            bufferevent_write(redis_bev, thread_info->key, thread_info->key_size);
            //printf("%s\n", thread_info->key);
            bufferevent_write(redis_bev, "\r\n", 2);
            sprintf(response, "$%d\r\n", value_size);
            //printf("%s", response);
            bufferevent_write(redis_bev, response, strlen(response));
            bufferevent_write(redis_bev, value, value_size);
            //printf("%s\n", value);
            bufferevent_write(redis_bev, "\r\n", 2);
            bufferevent_enable(redis_bev, EV_READ|EV_WRITE);
            while(redis_info[redis_index].result < 0 && redis_info[redis_index].working >= 0)
            {
            }
            if(redis_info[redis_index].result == 0)
            {
                sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
                bufferevent_write(bev, response, strlen(response)); 
                free(response);
                bufferevent_enable(bev, EV_WRITE);
                printf("upload fail\n");
                return ;
            }
            redis_info[redis_index].result=-1;
            redis_info[redis_index].working=-1;
            thread_info->state=1;
            thread_info->length = len;
            thread_info->key_size = 0;
            sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nOK");
            //printf("%s\n", response);
            bufferevent_write(bev, response, strlen(response));
            bufferevent_enable(bev, EV_WRITE);
            free(value);
            return ;
        }
        free(value);
    }
    else if(thread_info->working == 2)
    {
        //printf("GET\n");
        int redis_state = 0;
        int redis_index;
        struct bufferevent *redis_bev;
        while(redis_state == 0)
        {
            for(int j=thread_info->index_num*4;j<(thread_info->index_num+1)*4;j++)
            {
                if(redis_info[j].working == -1)
                {
                    redis_bev = redis_bev_list[j];
                    bufferevent_disable(redis_bev, EV_WRITE | EV_READ);
                    redis_state=1;
                    redis_info[j].working=0;
                    thread_info->redis_index = j;
                    redis_index = j;
                    redis_info[j].sock_bev=thread_info->sock_bev;
                    printf("redis %d is picked for %d\n", thread_info->redis_index, thread_info->sockfd);
                    break;
                }
            }
        }
        int target_len = strlen(target);
        sprintf(response, "*2\r\n$3\r\nGET\r\n$%d\r\n", target_len);
        //printf("%s", response);
        //write(redis_info[redis_index].redisfd, response, strlen(response));
        //write(redis_info[redis_index].redisfd, response, strlen(response));
        int n=bufferevent_write(redis_bev, response, strlen(response));
        int n1=bufferevent_write(redis_bev, target, target_len);
        //printf("%s\n", target);
        //write(redis_info[redis_index].redisfd, "\r\n", 2);
        bufferevent_write(redis_bev, "\r\n", 2);
        //while(1)
        bufferevent_enable(redis_bev, EV_WRITE|EV_READ);
        while(redis_info[thread_info->redis_index].result < 0 && redis_info[redis_index].working >= 0)
        if(redis_info[thread_info->redis_index].result == 0)
        {
            printf("get fail\n");
            sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
            bufferevent_write(bev, response, strlen(response)); 
            free(response);
            bufferevent_enable(bev, EV_WRITE);
            return ;
        }
        redis_info[thread_info->redis_index].result=-1;
        redis_info[thread_info->redis_index].working=-1;
    }
    free(target);
    free(response);
}

void redis_read_cb(struct bufferevent *bev, void *ctx)
{
    //printf("redis read\n");
    struct evbuffer *output;
    output = evbuffer_new();
    struct _redis *redis_server;
    int size = 0;
    char data[10];
    char *response = (char *)malloc(4096);
    redis_server = ctx;
    output = bufferevent_get_input(bev);
    int length = redis_server->length;
    int len=evbuffer_get_length(output);
    int part_len;
    printf("redis read %d\n", redis_server->sockfd);
    //printf("workin %d\n", redis_server->working);
    if(redis_server->working==0)
    {
        size = 0;
        while(1)
        {
            int n=evbuffer_remove(output, data+size, 1);
            if(size > 0)
                if(data[size] == '\n' && data[size-1] == '\r')
                {
                    data[size-1]='\0';
                    break; 
                }
            size++;
        }
        //printf("%s\n", data);
        if(data[0] == ':')
        {
            redis_server->result = 1;
            redis_server->working = -1;
            redis_server->length = -1;
            len = evbuffer_get_length(output);
            //printf("%d\n", len);
            return ;
        }
        else if(data[0] == '$')
        {
            len = atoi(data+1);
            //printf("%d\n", len);

            if(len < 0)
            {
                redis_server->result=0;
                redis_server->length=-1;
                printf("error\n");
                return ;
            }
            redis_server->length=len;
            redis_server->working=1;
            sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n", len);
            bufferevent_write(redis_server->sock_bev, response, strlen(response));
            //printf("%s\n", response);
        }
    }
    if(redis_server->working==1)
    {
        //printf("workin %d\n", redis_server->working);
        len = redis_server->length;
        part_len = evbuffer_get_length(output);
        //printf("part len %d\n", part_len);
        if(part_len<=len)
        {
            bufferevent_write_buffer(redis_server->sock_bev, output);
            redis_server->length=len-part_len;
            //printf("left len %d\n", redis_server->length);
            if(len==0)
            {
                redis_server->result=1;
                redis_server->length=-1;
            }
        }
        else
        {
            evbuffer_remove(output, response, part_len);
            bufferevent_write(redis_server->sock_bev, response, len);
            len=0;
            redis_server->result=1;
            redis_server->length=-1;
            bufferevent_enable(redis_server->sock_bev, EV_WRITE);
        }
    }
    free(response);
}

void provider_thread_task(char *argv[])
{
    struct sockaddr_in redisaddr;
    int client_socket;
    struct event_base* base;

    base = event_base_new();
    
    memset(&redisaddr, 0, sizeof(redisaddr));
    redisaddr.sin_family = AF_INET;
    redisaddr.sin_addr.s_addr = inet_addr(argv[2]);
    redisaddr.sin_port = htons(atoi(argv[3]));
    printf("%s %s\n", argv[2], argv[3]);
    for(int i=0;i<40;i++)
    {
        if(((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0))
        {
            printf("Create Client Error\n");
            //return -1;
        }
        if(connect(client_socket, (struct sockaddr *)&redisaddr, sizeof(redisaddr))<0)
        {
            printf("Redis server connect failed\n");
            //return -1;
        }
        int value=fcntl(client_socket, F_GETFL, 0);
        fcntl(client_socket, F_SETFL, value|O_NONBLOCK);
        redis_bev_list[i] = bufferevent_socket_new(base, client_socket, BEV_OPT_CLOSE_ON_FREE | O_NONBLOCK);
        bufferevent_setcb(redis_bev_list[i], redis_read_cb, NULL, NULL, (void *)&redis_info[i]); 
        redis_info[i].sockfd = -1;
        redis_info[i].redisfd = client_socket;
        redis_info[i].result = -1;
        redis_info[i].working = -1;
        redis_info[i].length = -1;
        bufferevent_enable(redis_bev_list[i], EV_WRITE | EV_READ);
        printf("Redis %d connected\n", i);
    }
    event_base_dispatch(base);
}

void *thread_task(void *info)
{
    struct event_base *base;
    ph *thread_info;
    struct bufferevent *bev;
    int fd;

    thread_info = info;
    fd = thread_info->sockfd;

    base = event_base_new();
    printf("thread created\n");
    while(1)
    {
        pthread_mutex_lock(&thread_info->lock);
        pthread_cond_wait(&thread_info->cond, &thread_info->lock);
        fd = thread_info->sockfd;
        printf("thread run %d\n", thread_info->index_num);
        //pthread_mutex_unlock(&thread_info->lock);
        if(fd > 0)
        {
            bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
            if(!bev)
            {
                printf("bufferevent make error");
                return (void *)-1;
            }
            thread_info->sock_bev = bev;
            bufferevent_setcb(bev, read_cb, write_cb, event_cb, info);
            bufferevent_enable(bev, EV_READ);
            bufferevent_disable(bev, EV_WRITE);
            event_base_dispatch(base);
            printf("Thread %d end\n", thread_info->index_num);
            pthread_mutex_unlock(&thread_info->lock);
        }
    }
}

int dequeue_task(pthread_mutex_t lock, int shift)
{
    int fd;
    int state;
    pthread_mutex_lock(&lock);
    if(end_queue-front_queue==0)
    {
        return 0;
    }
    fd = task_queue[front_queue];
    front_queue = (front_queue+1)%MAX_queue;
    state = 0;
    printf("%d of %d\n", fd, (end_queue-front_queue)%MAX_queue);
    while(state == 0)
    {
        for(int i=shift;i<10+shift;i++)
        {
            if(pthread_mutex_trylock(&p_info[i%10].lock)==0)
            {
                p_info[i%10].sockfd = fd;
                p_info[i%10].working = 0;
                pthread_mutex_unlock(&p_info[i%10].lock);
                pthread_cond_signal(&p_info[i%10].cond);
                //pthread_mutex_unlock(&p_info[i].lock);
                printf("Thread %d picked for %d\n", i%10, fd);
                state=1;
                break;
            }
            //printf("searching\n");
        }
        //printf("working dequeue\n");
    }
    pthread_mutex_unlock(&lock);
    return (end_queue-front_queue)%MAX_queue;
}


int enqueue_task(int fd, pthread_mutex_t lock, int shift)
{
    pthread_mutex_lock(&lock);
    if((front_queue-end_queue)%MAX_queue == 1)
    {
        pthread_mutex_unlock(&lock);
        dequeue_task(lock, shift);
        pthread_mutex_lock(&lock);
    }
    task_queue[end_queue] = fd;
    end_queue = (end_queue+1)%MAX_queue;
    pthread_mutex_unlock(&lock);
    return (end_queue-front_queue)%MAX_queue;
}

int main(int argc, char *argv[])
{
    //event_enable_debug_mode();
    struct sockaddr_in redisaddr;
    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr;
    int server_socket;
    int client_socket;
    int clientlen;
    struct event_base *base;
    struct evconnlistener *listener;
    int enqueue_len;

    fd_set ready_set;

    pthread_mutex_t main_lock;

    if(argc < 4)
    {
        printf("Too less arguments\n");
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
    
    /* thread pool init */

    p_thread = (pthread_t *)malloc(sizeof(pthread_t) * 10);
    p_info = (ph *)malloc(sizeof(ph) * 10);
    
    for(int i=0;i<10;i++)
    {
        p_info[i].sockfd = -1;
        p_info[i].working = -1;
        p_info[i].length = -1;
        p_info[i].state = -1;
        p_info[i].key_size = 0;
        p_info[i].redis_index = -1;
        p_info[i].index_num = i;
        pthread_cond_init(&p_info[i].cond, NULL);
        pthread_mutex_init(&p_info[i].lock, NULL);

        if(pthread_create(&p_thread[i], NULL, thread_task, (void *)&p_info[i])<0)
        {
            printf("thread create failed\n");
            return -1;
        }
    }

    /* redis pool init */

    redis_info = (redis *)malloc(sizeof(redis) * 40);

    /*
    for(int i=0;i<40;i++)
    {
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
        bufferevent_setcb(redis_bev_list[i], redis_read_cb, NULL, NULL, (void *)&redis_info[i]); 
        redis_info[i].sockfd = -1;
        redis_info[i].redisfd = client_socket;
        redis_info[i].result = -1;
        redis_info[i].working = -1;
        redis_info[i].length = -1;
        bufferevent_disable(redis_bev_list[i], EV_WRITE | EV_READ);
        printf("Redis %d connected\n", i);
    }
    */
    /* provider thread init */
    pthread_create(&provider_thread, NULL, &provider_thread_task, argv);
    pthread_mutex_init(&provider_mutex, NULL);
    pthread_cond_init(&provider_cond, NULL);

    pthread_mutex_init(&main_lock, NULL);

    /* Create socket */
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket < 0)
    {
        printf("Create Socket Error\n");
        return -1;
    }    

    int optval = 1;
    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int))<0)
        return -1;
    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, (const void *)&optval, sizeof(int))<0)
        return -1;

    if(bind(server_socket, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
        printf("bind ERROR\n");
        return -1;
    }
  
    /* Listen socket */
    if(listen(server_socket, 100) < 0)
    {
        printf("listen ERROR\n");
        return -1;
    }
    
    FD_ZERO(&ready_set);
    FD_SET(server_socket, &ready_set);

    struct timeval timeout;
    timeout.tv_sec=1;
    timeout.tv_usec=0;
    event_base_dispatch(base);
    /* Task */
    int shift=0;
    while(1)
    {
        select(server_socket+1, &ready_set, NULL, NULL, NULL);

        if(FD_ISSET(server_socket, &ready_set))
        {
            /* Accept client connection request */
            client_socket = accept(server_socket, (struct sockaddr *)&clientaddr, &clientlen);
            if(client_socket < 0)
            {
                printf("accept ERROR\n");
                return -1;
            }
            int value=fcntl(client_socket, F_GETFL, 0);
            fcntl(client_socket, F_SETFL, value|O_NONBLOCK);
            FD_SET(client_socket, &ready_set);
            shift=(shift+1)%10;
            enqueue_len = enqueue_task(client_socket, main_lock, shift);

        }
        if(enqueue_len > 0)
        {
            enqueue_len = dequeue_task(main_lock, shift);
            printf("enqueue %d\n", enqueue_len);
        }
    }
    free(p_thread);
    free(p_info);
    free(redis_info);
    return 0;
}
