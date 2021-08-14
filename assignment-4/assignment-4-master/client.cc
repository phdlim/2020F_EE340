#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>


int main (int argc, char* argv[]) {
    FILE *fp_r;
    FILE *fp_w;
    char* buf = (char *)malloc(4*1024*1025);
    struct sockaddr_in clientaddr;
	  struct sockaddr_in serveraddr;
    int server_socket;
	  int client_socket;
    int serverlen;


    fp_r = fopen(argv[1], "r");
    fp_w = fopen("result.txt", "w");

    if(((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0))
	  {
		    printf("Create Client Error\n");
		    return -1;
	  }
	
	  /* Connect server */
	  memset(&serveraddr, 0, sizeof(serveraddr));
	  serveraddr.sin_family = AF_INET;
    struct hostent *hp;
    if((hp = gethostbyname(argv[2])) == NULL)
        return -2;
    serveraddr.sin_addr.s_addr = inet_addr(inet_ntoa(*(struct in_addr*)*hp->h_addr_list));
	  //serveraddr.sin_addr.s_addr = inet_addr(argv[2]);
	  serveraddr.sin_port = htons(atoi(argv[3]));
	
	  if(connect(client_socket, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
	  {
		    printf("Connect Error\n");
		    return -1;
	  }

    fseek(fp_r, 0, SEEK_END);
    int fsize = ftell(fp_r);
    rewind(fp_r);

    int check;
    check = fread(buf, 1, fsize, fp_r);

    if(check!=fsize)
    {
        printf("size error\n");
    }

    char content_header[20];
    sprintf(content_header, "%d ", fsize);
    send(client_socket, content_header, strlen(content_header), 0);
    printf("%s\n", content_header);
    send(client_socket, buf, fsize, 0);
    printf("%s", buf);
    int length;
    int size=0;
    char l[20];
    memset(buf, 0, sizeof(buf));
    while(1)
    {
        read(client_socket, (l+size), 1);
        if(*(l+size) == ' ')
        {
            l[size] = '\0';
            break;
        }
            size++;
    }
    length = atoi(l);
    size=0;
    while(read(client_socket, (buf+size), 1))
    {
        size++;
        if(size==length)
            break;
    }
    fwrite(buf, 1, length, fp_w);
    fclose(fp_r);
    fclose(fp_w); 
    free(buf);
    return 0;
}
