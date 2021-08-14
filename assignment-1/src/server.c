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
#include "common.h"

void childhandler(int signal)
{
    int status;
    while(waitpid(-1, &status, WNOHANG) > 0);
}

int main(int argc, char *argv[]) 
{

    signal(SIGCHLD, childhandler);

    struct sockaddr_in clientaddr;
    struct sockaddr_in serveraddr;
    int server_socket;
    int client_socket;
    socklen_t clientlen;
    struct packet buf;
    int length;
    int pid;
    int seq;
    char data[2048];
    int MAX_LENGTH = 2048 + 8;
    
    /* Check command */
    if(argc < 2)
	      printf("No Port INPUT");

    /* Create socket */
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket < 0)
    {
	      printf("Create Socket Error");
	      return -1;
    }    

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
	      if((pid=fork())==0)
	      {
	          //close(server_socket);
            /* Receive client hello */
	          read(client_socket, &buf, 8);
            printf("Receive hello\n");
    	      //printf("%x\n", ntohs(buf.command));
    	      if(ntohs(buf.command) != 0x0001)
    	      {
	              printf("0x0001 Error\n");
                return -1;
	          }
            /* Send server hello */
    	      //set first message from server
            buf.version = 0x04;
            buf.userID = 0x08;
            buf.sequence = htons(ntohs(buf.sequence)+1);
            buf.command = htons(0x0002);
            buf.length = htons(8);
	          write(client_socket, &buf, 8);
	          printf("Server HELLO\n");

            /* Receive client file */
            seq = ntohs(buf.sequence);
            // create file
    		    int name = getpid();
		        char file[20];
    		    sprintf(file, "%d.txt", name);
	          int fp = open(file, O_WRONLY | O_CREAT, 0644);
	          int size = 0;
    	      while(1)
	        	{
                /* Read header */
    	          int header_len = read(client_socket, &buf, 8);
	              if(header_len == -1)
	              {
		                perror("read");
		                printf("RECV ERROR\n");
    		            break;
	              }
                else if(header_len != 8)
	              {
		                printf("HEADER is not valid\n");
    		            break;
	              }
                printf("%d\n", htons(buf.sequence));
                /* Check sequence */
    	          if(((seq+1)%10000) != htons(buf.sequence))
	              {
		                printf("SEQ ERROR\n");
		                return -1;
    	          }
	              seq += 1;
	            
                /* Data delivery */
                if(ntohs(buf.command) == 0x0003)
	              {
                    /* Update total file size */
	    	            length = ntohs(buf.length);
    		            size = size + (length - 8);
		                printf("%d\n", size);
		            
                    /* Read payload */
		                read(client_socket, data, length-8);
    		            data[length-8] = '\0';
		                //printf("%s\n", data);
		                int len_write = write(fp, data, length-8);
		                if(len_write != (length-8))
                        printf("data write error\n");
	              } 

                /* Store */
	              if(ntohs(buf.command) == 0x0004)
	              {
	                  close(fp);
    		            length = ntohs(buf.length);
		                printf("%d\n", length);
                    /* Receive file name */
	                  recv(client_socket, data, length-8, 0);
    		            data[length-8]='\0';
    		            printf("%s\n", data);
		                rename(file, data);
		                break;
	              }
    	          memset(&buf, 0, MAX_LENGTH);
	          }
            shutdown(client_socket, SHUT_WR);
            exit(0);
	      }
    	  else if(pid < 0)
	      {
	          printf("fork error\n");
    	      return -1;
	      }
    	  else
	      {
    	    //shutdown(client_socket, SHUT_WR);
	      }
    }
    return 0;
}
