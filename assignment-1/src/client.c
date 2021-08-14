#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include "common.h"

int main(int argc, char *argv[]) 
{
   
    struct sockaddr_in clientaddr;
    struct sockaddr_in serveraddr;
    char data[2048];
    int server_socket;
    int client_socket;
    int serverlen;
    int num_read;
    struct packet buf;
    int MAX_LENGTH = 2048 + 8;

    /* check command */
    if(argc < 4)
    {
        	printf("Not enough argument\n");
    }

    /* Create socket */
    if(((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0))
    {
	      printf("Create Client Error\n");
	      return -1;
    }
    
    /* Connect server */
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
    serveraddr.sin_port = htons(atoi(argv[2]));
    
    if(connect(client_socket, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
	      printf("Connect Error\n");
	      return -1;
    }

    /* Task*/


    /* Send client hello */
    	//set first message from client

    srand(time(NULL));
    int conn_seq = rand() % 10000;

    buf.version = 0x04;
    buf.userID = 0x08;
    buf.sequence = htons(conn_seq);
    buf.command = htons(0x0001);
    buf.length = htons(8);
    
    send(client_socket, &buf, 8, 0);
    printf("Client HELLO\n");

    /* Receive server hello */
    int check = read(client_socket, &buf, 8);
    printf("%d Receive hello\n", check);
    printf("%x\n", ntohs(buf.command)); 
    if(ntohs(buf.command) != 0x0002)
    {
	      printf("0x0002 error\n");
	      return -1;
    }
    if(ntohs(buf.sequence) != conn_seq + 1)
    {
	      printf("sequence error\n");
    	  return -1;
    }
    conn_seq += 1;
    printf("Server Hello Check\n");

    /* Send server file */
    int fp;
    // open file
    fp = open(argv[3], O_RDONLY, 0644);
    if(fp < 0)
    {
	      printf("NO FILE\n");
	      return -1;
    }

    int size = 0;
    
    /* create packets  */
    // read file
    while(num_read = read(fp, buf.data, MAX_LENGTH-8))
    {
        /* Data delivery */
	      conn_seq+=1;
	      buf.version = 0x04;
	      buf.userID = 0x08;
	      buf.sequence = htons(conn_seq%10000);
	      buf.command = htons(0x0003);
	
	      if(num_read < 0)
	      {
	          printf("File read error\n");
	          break;
	      }
	      else if(num_read > 0)
	      {
	          buf.length = htons(num_read+8);
	      }
	      send(client_socket, &buf, num_read + 8, 0);
	      size += num_read;
	      //printf("%d\n", num_read);
	      printf("send %d bytes\n", size);
    }
    
    /* Send data store packet */
    memset(&buf, 0, MAX_LENGTH);
    conn_seq+=1;
    buf.version = 0x04;
    buf.userID = 0x08;
    buf.sequence = htons(conn_seq%10000);
    buf.command = htons(0x0004);
    
    strcpy(buf.data, argv[3]);
    buf.length = htons(strlen(argv[3])+8);
    //printf("%d\n", ntohs(buf.length)-8);
    send(client_socket, &buf, ntohs(buf.length), 0);
    //printf("send NAME of the file\n");
    
    close(fp); 
    shutdown(client_socket, SHUT_WR);
}
