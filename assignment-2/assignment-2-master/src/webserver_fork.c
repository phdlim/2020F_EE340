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

#include "redis_parse.h"
#include "http_parse.h"
#include "urlencode.h"

void childhandler(int signal)
{
  int status;
  while(waitpid(-1, &status, WNOHANG) > 0);
}

int main(int argc, char *argv[]) 
{
  struct sockaddr_in clientaddr;
  struct sockaddr_in serveraddr;
  int server_socket;
  int client_socket;
  socklen_t clientlen = sizeof(clientaddr);
  int pid;
    
  /* Use waitpid to reap zombie process */
  signal(SIGCHLD, childhandler);

  /* Check command */
  if(argc < 4)
  {
    printf("No Port INPUT\n");
    return -1;
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
  if(listen(server_socket, 10000) < 0)
  {
    printf("listen ERROR\n");
    return -1;
  }

  /* Task */
  while(1)
  {
    /* Accept client connection request */
    client_socket = accept(server_socket, (struct sockaddr *)&clientaddr, &clientlen);
    if(client_socket < 0)
    {
      printf("accept ERROR\n");
      return -1;
    }

    /* Child Process */
    if((pid=fork())==0)
    {
      int redis_socket = redis_server(argv[2], argv[3]);
      int content_size;
      int encode;
      char method[1000];
      char target[1000];
      
      if(redis_socket<0)
      {
        printf("REDIS Server Connect error\n");
        return -1;
      }
      //get request from client server 
      content_size = http_read_request(client_socket, method, target, &encode);
      //printf("encode%d\n", encode);
      if(content_size < 0)
      {
        send_error(client_socket, 404);
        close_redis_server(redis_socket);
        shutdown(client_socket, SHUT_WR);
        exit(0);
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
            free(buf);
            send_error(client_socket, 404);
            close_redis_server(redis_socket);
            shutdown(client_socket, SHUT_WR);
            exit(0);
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
                close_redis_server(redis_socket);
                send_error(client_socket, 404);
                shutdown(client_socket, SHUT_WR);
                exit(0);
                break;
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
            close_redis_server(redis_socket);
            send_error(client_socket, 404);
            shutdown(client_socket, SHUT_WR);
            exit(0);
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
        char *buf = (char *)malloc(512*1024*1025); 
        if(target[0] == '/')
        {
    	      memcpy(key_buf, target+1, strlen(target));
        }
        int size=0;
        int i;
        for(i=0;i<strlen(key_buf);i++)
        {
	  //url decoding
          if(key_buf[i] == '%')
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
          free(buf);
          send_error(client_socket, 404);
          shutdown(client_socket, SHUT_WR);
          exit(0);
        }
        http_get_response(client_socket, redis_response, buf);
        free(buf);
      }
      else
      {
        send_error(client_socket, 404);
        close_redis_server(redis_socket);
        shutdown(client_socket, SHUT_WR);
        exit(0);
      }
      shutdown(client_socket, SHUT_WR);
      exit(0);
    }
    /* fork error handle */
    else if(pid < 0)
    {
      printf("fork error\n");
      return -1;
    }
    /* Parent Process */
    else
    {
      //shutdown(client_socket, SHUT_WR);
    }
  }
  return 0;
}
