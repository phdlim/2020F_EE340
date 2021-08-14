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

//connect redis server
int redis_server(char *ip, char *port)
{
  struct sockaddr_in clientaddr;
  struct sockaddr_in serveraddr;
  int server_socket;
  int client_socket;
  int serverlen;
  

  /* Create socket */
  if(((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0))
  {
    printf("Create Client Error\n");
    return -1;
  }
  
  /* Connect server */
  memset(&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  //struct hostent *hp;
  //if((hp = gethostbyname(ip)) == NULL)
  //	  return -2;
  serveraddr.sin_addr.s_addr = inet_addr(ip);
  serveraddr.sin_port = htons(atoi(port));
  //serveraddr.sin_addr.s_addr = inet_addr(inet_ntoa(*(struct in_addr*)*hp->h_addr_list));
  if(connect(client_socket, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
  {
    printf("Connect Error\n");
    return -1;
  }
  //printf("redis server connected\n");
  return client_socket;
}

//send redis server set request
int redis_set_request(int sock_id, char* key, char* value, int key_size, int value_size)
{
  char buf[2048];
  //printf("send request\n");
  sprintf(buf, "*3\r\n$3\r\nSET\r\n$%d\r\n", key_size);
  write(sock_id, buf, strlen(buf));
  write(sock_id, key, key_size);
  sprintf(buf, "\r\n$%d\r\n", value_size);
  write(sock_id, buf, strlen(buf));
  write(sock_id, value, value_size);
  write(sock_id, "\r\n", 2);
  return 1;
}

//receive set response by redis server
int redis_set_response(int sock_id)
{
  //printf("Receive Response\n");
  char buf[100];
  int size=0;
  while(1)
  {
    read(sock_id, buf+size, 1);
    if(size > 0)
      if(buf[size] == '\n' && buf[size-1] == '\r')
      {
        break;
      }
    size++;
  }
  //printf("%s\n", buf);
  if(buf[1] == 'O')
  	return 1;
  else
	return 0;
}

//send redis server get request
int redis_get_request(int sock_id, char* key, int size)
{
  char buf[2048];
  sprintf(buf, "*2\r\n$3\r\nGET\r\n$%d\r\n", size);
  write(sock_id, buf, strlen(buf));
  write(sock_id, key, size);
  write(sock_id, "\r\n", 2);
  return 1;
}

//receive get response by redis server
int redis_get_response(int sock_id, char* buf)
{
  char res[2048];
  char* start;
  int length = 0;
  int size=0;
  while(1)
  {
    read(sock_id, res+size, 1);
    if(size > 2)
      if(res[size] == '\n' && res[size-1] == '\r')
      {
        res[size-1]='\0';
        break;
      }
    size++;
  }
  printf("%s\n", res);
  start = res+1;
  length = atoi(start);
  printf("%d\n", length);
  if(length<0)
    return length;
  int check = read(sock_id, buf, length);
  if (check!=length)
    return -1;
  read(sock_id, res, 2048);
  return length;
}

//close client of redis server
int close_redis_server(int sock_id)
{
  shutdown(sock_id, SHUT_WR);
}
