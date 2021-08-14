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

#include "http_parse.h"

//http request parser
int http_read_request(int sock_id, char* line, char* method, char* target, int* encode)
{
  int state = 0;
  int size = 0;
  char name[100];
  char value[100];
  
  int check;
  int state_host = 0;
  int state_length = 0;
  int state_type = 0;
  int content_length = 0;
  *encode = 0;

  while(1)
  {
    read(sock_id, line+size, 1);
    if(size > 0)
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
        //printf("%s\n", line);
          //printf("First Line Parse\n");
          check = http_parse_first_line(line, method, target);
          //printf("%d %d\n", strlen(method), strlen(target));
          state=1;
          if(check < 0)
          {
            return -1;
          }
          size=-1;
        }
        else if(state == 1)
        {
          check = http_parse_header_line(line, name, value);
          //printf("Name: %s\nValue: %s\n", name, value);
          if(check < 0)
          {
            return -1;
          }
          if(!strcmp(name, "Host"))
          {
            state_host = 1;
          }
          else if(!strcmp(name, "Content-Length") || !strcmp(name, "Content-length"))
          {
	    state_length = 1;
            content_length = atoi(value);
          }
          else if(!strcmp(name, "Content-type") || !strcmp(name, "Content-Type"))
          {	
	    state_type = 1;
            if(!strcmp(value, "application/x-www-form-urlencoded"))
              *encode=1;
          }
          size=-1;
        }
      }
    size++;
    //if(size>2048)
    //{
    //  memset(line, 0, 4096);
    //  memset(name, 0, 4096);
    //  memset(value, 0, 1024);
    //  free(line);
    //  free(name);
    //  free(value);
    //  return -1;
    //}
  }
  //printf("%d\n", size);
  memset(line, 0, 4096);
  size=0;

  //bad request error handle
  if(state_host != 1)
  {
    printf("No host\n");
    return -1;
  }
  if(((state_length) != 1) && !strcmp(method, "POST"))
  {
    printf("No content info in POST\n");
    return -1;
  }
  if(state != 2)
    return -1;
  //printf("Content Length: %d\n", content_length);
  return content_length;
}


//http first line parser
int http_parse_first_line(char* line, char* method, char* target)
{
  int i=0;
  int state = 0;
  char* start=0;
  char* end=0;
  char version[2048];
  start = line;
  //printf("first %s\n", line);
  while(line[i])
  {
    if(line[i] == ' ')
    {
      end = line+i;
      if(state == 0)
      {
        line[i] = '\0';
        memcpy(method, start, end-start+1);
        state = 1;
        start = end+1;
      }
      else if(state == 1)
      {
        line[i] = '\0';
        memcpy(target, start+1, end-start+1);
        state = 2;
        start = end+1;
      }
    }
    i++;
  }
  if(state == 2)
  {
    strcpy(version, start);
    //printf("%s\n", version);
    state = 3;
  }
  if(strcmp(version, "HTTP/1.0") && strcmp(version, "HTTP/1.1"))
  {
	  printf("HTTP version ERROR\n");
	  return -1;
  }
  if(state!=3)
  {
    printf("bad first line parse\n");
    return -1;
  }
  return 0;
}

//http header parser
int http_parse_header_line(char* line, char *name, char *value)
{
  int i=0;
  int state = 0;
  char* start=0;
  while(line[i])
  {
    if(i >= 1)
    {
      if(line[i] == ' ' && line[i-1] == ':')
      {
        line[i-1] = '\0';
        strcpy(name, line);
        start = line+i+1;
        state = 1;
        break;
      }
    }
    i++;
  }
  strcpy(value, start);
  if(state != 1)
  {
    printf("Bad header Parse\n");
    return -1;
  }
  return 0;
}

//send 404 or 400 error to client
int send_error(int sock_id, int number)
{
  char buf[1024];
  if(number == 404)
  {
    sprintf(buf, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
    write(sock_id, buf, strlen(buf));
    return 1;
  }
  else if(number == 400)
  {
    sprintf(buf, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nERROR");
    write(sock_id, buf, strlen(buf));
    return 1;
  }
}

//send 200 ok to client
int http_set_ok(int sock_id)
{
  char buf[1024];
  sprintf(buf, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nOK");
  write(sock_id, buf, strlen(buf));
}


//send 200 ok and value of get method to client
int http_get_response(int sock_id, int length, char* value)
{
  char buf[1024];
  sprintf(buf, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n", length);
  write(sock_id, buf, strlen(buf));
  write(sock_id, value, length);
  return 1;
}
