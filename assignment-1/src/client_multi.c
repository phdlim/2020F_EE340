#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/wait.h>
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
    int server_socket;
    int client_socket;
    int pid;
    char data[100];
    int length;
    int serverlen;
    struct packet buf;
    int MAX_LENGTH = 2048 + 8;
    int request;

    /* check command */
    if(argc < 4)
    {   
        printf("Not enough argument\n");
        return -1;
    }

    request = atoi(argv[3]);
    printf("%d\n", request);
    for (int i = 0; i < request;i++)
    {
        if((pid = fork()) == 0)
	      {
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
    	      //printf("Client HELLO\n");

    	      /* Receive server hello */
    	      recv(client_socket, &buf, 8, 0);
    	      //printf("Receive hello\n"); 
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
            //printf("Server Hello Check\n");

	          conn_seq+=1;
	          buf.version = 0x04;
	          buf.userID = 0x08;
	          buf.sequence = htons(conn_seq%10000);
	          buf.command = htons(0x0003);
	
	          pid = (int)getpid();
	          sprintf(data, "I am a process %d", pid);
	          printf("%d %s\n", (int)strlen(data), data);
	          length = strlen(data);
	          buf.length = htons(length+8);
	          send(client_socket, &buf, length + 8, 0);
	          //printf("%d\n", num_read);
	          //printf("send %d bytes\n", length+8);
    
    
    	      /* Send data store packet */
    	      memset(&buf, 0, MAX_LENGTH);
    	      conn_seq+=1;
    	      buf.version = 0x04;
    	      buf.userID = 0x08;
    	      buf.sequence = htons(conn_seq%10000);
    	      buf.command = htons(0x0004);
 	          sprintf(data, "%d.txt", pid);   
    	      printf("%s\n", data);
	          buf.length = htons(strlen(data)+8);
    	      //printf("%d\n", ntohs(buf.length)-8);
    	      send(client_socket, &buf, ntohs(buf.length), 0);
    	      //printf("send NAME of the file\n");
    
    	      shutdown(client_socket, SHUT_WR);
	          exit(0);
        }
	      else if(pid > 0)
	      {
		        usleep(100*1000);
            printf("CHECK\n");
            continue; 
	      }
    }
}
