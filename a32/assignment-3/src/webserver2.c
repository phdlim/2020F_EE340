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
#include <event2/event.h>
#include <signal.h>
#include <pthread.h>

#include "redis_parse.h"
#include "http_parse.h"
#include "urlencode.h"

struct event_base *base;
char *ip;
char *port;
void event_accept(int fd, short event, void *arg);
void task(int fd, short event, void *arg);

typedef struct _ph
{
    int sockfd;
    int redis_index;
    int working;
    int index_num;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} ph;

typedef struct _redis
{
    int sockfd;
    int working;
} redis;

int task_queue[10000];
int front_queue = 0;
int end_queue = 0;
int MAX_queue=10000;

struct _redis *redis_info;

pthread_t *p_thread;
ph *p_info;

void *thread_task(void *info);


int dequeue_task(pthread_mutex_t lock, int shift)
{
    int fd;
    int state;
    pthread_mutex_lock(&lock);
    if(end_queue-front_queue==0)
    {
        return 0;
    }
    shift=0;
    fd = task_queue[front_queue];
    front_queue = (front_queue+1)%MAX_queue;
    state = 0;
    printf("%d of %d\n", fd, (end_queue-front_queue)%MAX_queue);
    while(state == 0)
    {
        for(int i=shift;i<10+shift;i++)
        {
            if(pthread_mutex_trylock(&p_info[i%10].lock)!=0 && p_info[i%10].working!=1)
            {
                p_info[i%10].sockfd = fd;
                p_info[i%10].working = 1;
                pthread_mutex_unlock(&p_info[i%10].lock);
                pthread_cond_signal(&p_info[i%10].cond);
                //pthread_mutex_unlock(&p_info[i].lock);
                printf("Thread %d picked for %d\n", i%10, fd);
                state=1;
                break;
            }
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
    struct sockaddr_in clientaddr;
    struct sockaddr_in serveraddr;
    struct sockaddr_in redisaddr;
    int server_socket;
    int client_socket;
    socklen_t clientlen=sizeof(clientaddr);
    int enqueue_len;
    pthread_mutex_t main_lock;

    fd_set ready_set;


    /* Check command */
    if(argc < 4)
    {
        printf("No Port INPUT\n");
        return -1;
    }

    p_thread = (pthread_t *)malloc(sizeof(pthread_t) * 10);
    p_info = (ph *)malloc(sizeof(ph) * 10);
    
    for(int i=0;i<10;i++)
    {
        p_info[i].sockfd = -1;
        p_info[i].working = 0;
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

    memset(&redisaddr, 0, sizeof(redisaddr));
    redisaddr.sin_family = AF_INET;
    redisaddr.sin_addr.s_addr = inet_addr(argv[2]);
    redisaddr.sin_port = htons(atoi(argv[3]));

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
        redis_info[i].working = 0;
        redis_info[i].sockfd = client_socket;
        printf("Redis %d connected\n", i);
    }

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

    /* Bind socket */
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(atoi(argv[1]));

    if(bind(server_socket, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
        printf("bind ERROR\n");
        return -1;
    }
  
    /* Listen socket */
    if(listen(server_socket, 1000) < 0)
    {
        printf("listen ERROR\n");
        return -1;
    }

    FD_ZERO(&ready_set);
    FD_SET(server_socket, &ready_set);

    struct timeval timeout;
    timeout.tv_sec=1;
    timeout.tv_usec=0;
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
            printf("accepted\n");
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
/*
    base = event_base_new();
    if(base==0)
    {
        printf("Libevent Initialize ERROR\n");
        return -1;
    }
    ip = argv[2];
    port = argv[3];
    accept_event = event_new(base, server_socket, EV_READ | EV_PERSIST, event_accept, (void*)base);
    event_add(accept_event, NULL);
    event_base_dispatch(base);
    printf("end\n");
*/
}

void *thread_task(void *info)
{
    struct event_base *base;
    ph *thread_info;
    struct event *bev;
    int fd;
    struct timeval timeout = {5, 0};

    thread_info = info;
    fd = thread_info->sockfd;

    base = event_base_new();
    printf("thread created\n");
    while(1)
    {
        pthread_mutex_lock(&thread_info->lock);
        pthread_cond_wait(&thread_info->cond, &thread_info->lock);
        fd = thread_info->sockfd;
        thread_info->working=1;
        printf("thread run %d\n", thread_info->index_num);
        //pthread_mutex_unlock(&thread_info->lock);
        if(fd > 0)
        {
            bev = event_new(base, fd, EV_READ|EV_WRITE, task, thread_info);
            if(!bev)
            {
                printf("bufferevent make error");
                return (void *)-1;
            }
            event_add(bev, &timeout);
            event_base_dispatch(base);
            printf("Thread %d end\n", thread_info->index_num);
            thread_info->working=0;
            pthread_mutex_unlock(&thread_info->lock);
        }
    }
}


void task(int fd, short event, void *arg)
{
    //printf("task\n");
    struct _ph *thread_info;
    thread_info=arg;
    int client_socket = fd;
    int redis_socket;
    int redis_index;
    /* Task */
    
    for(int i=4*thread_info->index_num;i<4*(thread_info->index_num+1);i++)
    {
        if(redis_info[i].working==0)
        {
            redis_socket = redis_info[i].sockfd;
            redis_index=i;
            redis_info[i].working=1;
        }
    }
    printf("redis %d is picked\n", redis_socket);
      int content_size;
      int encode;
      char method[512];
      char target[1024];
      char line[4096];
      
      //get request from client server 
      content_size = http_read_request(client_socket, line, method, target, &encode);
      //printf("encode%d\n", encode);
      if(content_size < 0)
      {
        send_error(client_socket, 404);
        redis_info[redis_index].working=0;
        shutdown(client_socket, SHUT_WR);
        return ;
      }
      //printf("content size is %d\n", content_size);
	//POST method task
        if (!strcmp(method, "POST"))
        {
          char *buf = (char *)malloc(sizeof(char) * 512*1024*1025);
          char key[1032];
          int key_size;
          int value_size;
          int size = 0;
          int state = 1;
          int redis_response;
          int i;
          if(content_size == 0)
          {
            send_error(client_socket, 404);
            redis_info[redis_index].working=0;
            shutdown(client_socket, SHUT_WR);
            return ;
          }
	  //read content and send redis request and get response
          for(i=0;i<content_size;i++)
          {
            read(client_socket, buf+size, 1);
            if(*(buf+size) == '=' && state == 1)
            {
              *(buf+size)='\0';
              memcpy(key, buf, size+1);
              //printf("%s\n", key);
              key_size = size;
              size = 0;
              state = 2;
            }
            else if(*(buf+size) == '&' && state == 2)
            {
              *(buf+size)='\0';
              value_size = size;
              //printf("%s\n", buf);
              redis_set_request(redis_socket, key, buf, key_size, value_size);
              redis_response = redis_set_response(redis_socket);
              if(redis_response<0)
              {
                free(buf);
                redis_info[redis_index].working=0;
                send_error(client_socket, 404);
                shutdown(client_socket, SHUT_WR);
                return ;
              }
              size = 0;
              state = 1;
            }
	    //url decoding
            else if(*(buf+size) == '%' && encode == 1)
            {
              char* encoded = (char*)malloc(4);
              char* decoded = (char*)malloc(4);
              read(client_socket, buf+size+1, 1);
              read(client_socket, buf+size+2, 1);
              encoded[0] = *(buf+size);
              encoded[1] = *(buf+size+1);
              encoded[2] = *(buf+size+2);
              encoded[3] = '\0';
              decoded = url_decode(encoded);
              *(buf+size) = decoded[0];
              size++;
              i+=2;
	      free(encoded);
	      free(decoded);
            }
          else
            size++;
        }
        if(state == 2)
        {
          *(buf+size) = '\0';
          value_size = size;
          redis_set_request(redis_socket, key, buf, key_size, value_size);
          redis_response = redis_set_response(redis_socket);
          if(redis_response<0)
          {
            free(buf);
            redis_info[redis_index].working=0;
            send_error(client_socket, 404);
            return ;
          } 
        }
      //send ok response to client
      http_set_ok(client_socket); 
      free(buf);
      }
      //GET method task
      else if(!strcmp(method, "GET"))
      {
        char key_buf[1032];
        char key[1032];
        char *buf = (char *)malloc(4*1024*1025); 
        if(target[0] == '/')
        {
    	      memcpy(key_buf, target+1, strlen(target));
        }
        int size=0;
        int i;
        for(i=0;i<strlen(key_buf);i++)
        {
	  //url decoding
          if(key_buf[i] == '%' && encode == 1)
          {
            char* encoded = (char*)malloc(4);
            char* decoded = (char*)malloc(4);
            encoded[0] = key_buf[i];
            encoded[1] = key_buf[i+1];
            encoded[2] = key_buf[i+2];
            encoded[3] = '\0';
            decoded = url_decode(encoded);
            key[size] = decoded[0];
            size++;
            i+=2;
	    free(encoded);
	    free(decoded);
          }
          else
          {
            key[size] = key_buf[i];
            size++;
          }
        }
        redis_get_request(redis_socket, key, size);
        int redis_response = redis_get_response(redis_socket, buf);
        if(redis_response < 0)
        {
          printf("1\n");
          send_error(client_socket, 404);
          free(buf);
          shutdown(client_socket, SHUT_WR);
          return ;
        }
        else
        {
        http_get_response(client_socket, redis_response, buf);
        free(buf);
        redis_info[redis_index].working=0;
        shutdown(client_socket, SHUT_WR);
        return ;
        }
      }
      else
      {
        send_error(client_socket, 404);
        redis_info[redis_index].working=0;
        close_redis_server(redis_socket);
      }
      //printf("task finished\n");
      char *buf[100];
      read(client_socket, buf,100);
      redis_info[redis_index].working=0;
      shutdown(client_socket, SHUT_WR);
  return ;
}


