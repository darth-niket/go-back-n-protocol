#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#define MAX 256
#define MSGLEN 128
#define WINSIZE 4
#include <sys/time.h>
#include <errno.h>
#include "gobackn.h"


typedef struct packet{
 char data[MSGLEN];
}Packet;

typedef struct frame{
 int seq; // 0 or 1
 int ack; //0 or 1
 int sign_type;//ACK:0 SEQ:1 FIN:2
 ssize_t msg_len;
 Packet packet;
}Frame;

int hostname_to(char *hostname, char *ip)
{
  struct hostent *he;
  struct in_addr **addr_list;
  int i;
  
  if ((he = gethostbyname(hostname)) == NULL)
  {
          // get the host info
          herror("gethostbyname");
          return 1;
  }
  
  addr_list = (struct in_addr **)he->h_addr_list;
  
  for (i = 0; addr_list[i] != NULL; i++)
  {
          //Return the first one;
          strcpy(ip, inet_ntoa(*addr_list[i]));
          return 0;
  }
  
  return 1;
}
void gbn_server(char* iface, long port, FILE* fp) {
	int sockfd;
    struct sockaddr_in serveraddress;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    serveraddress.sin_family = AF_INET;
    serveraddress.sin_addr.s_addr =  htonl(INADDR_ANY);
    serveraddress.sin_port = htons(port);
	
    int opt_val = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));
    int n = bind(sockfd, (const struct sockaddr *)&serveraddress, sizeof(serveraddress));
    if(n<0)
    {
     perror("Bind Failed\n");
     exit(1);
    }
    int nextseqnum = 0;
    Frame frame_recv;
    Frame frame_send;
    fflush(fp);
    while(1)
    {
     struct sockaddr_in cli_address;
     socklen_t address_length = sizeof(cli_address);
     int bytes_read = 0;
     bzero(frame_recv.packet.data, sizeof(frame_recv.packet.data));
     struct timeval tv;
     tv.tv_sec = 0;
     tv.tv_usec = 50000;
    
     setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
     while((bytes_read = recvfrom(sockfd, &frame_recv, sizeof(Frame), 0, (struct sockaddr *)&cli_address, &address_length))>0)
     {
		printf("THis is buffer: %s\n", frame_recv.packet.data);
        if (frame_recv.sign_type==2)
         {
                 sleep(2);
                 fclose(fp);
                 close(sockfd);
                 exit(0);
         }
        if (frame_recv.seq == nextseqnum)
        {
          printf("%s\n", frame_recv.packet.data);
         fwrite(frame_recv.packet.data, sizeof(char), frame_recv.msg_len, fp);
         if(ferror(fp))
         {
           fprintf(stderr, "Error writing ......\n");
           close(sockfd);
           fclose(fp);
           exit(1);
         }
        bzero(frame_recv.packet.data, sizeof(frame_recv.packet.data));
        frame_send.seq = nextseqnum;
        frame_send.sign_type = 0;
        frame_send.ack = frame_recv.seq;
        sendto(sockfd, &frame_send, sizeof(frame_send), 0, (struct sockaddr*)&cli_address, address_length);
        nextseqnum = 1 + nextseqnum;
        }
        else
        {
         frame_send.ack = nextseqnum - 1;
         sendto(sockfd, &frame_send, sizeof(frame_send), 0, (struct sockaddr*)&cli_address, address_length);
        }
   }

  }
}

void gbn_client(char* host, long port, FILE* fp) {
  int sock_conn;
  struct sockaddr_in servaddr;
  socklen_t address_length = sizeof(servaddr);
  socklen_t addr_len = sizeof(servaddr);
  sock_conn = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_conn == -1) 
  {
   printf("[-] Socket creation failed\n");
   exit(0);
  }
  
  char ip[20];
  
  hostname_to(host, ip);

  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(ip);
  servaddr.sin_port = htons(port);

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  
  Frame frame_recv;
  Frame frame_send;
  Frame *window[4000];
  int base = 0;
  int nextsumseq = 0;
  size_t bytes_read = 0;
  while(1)
  {
  bzero(frame_send.packet.data, sizeof(frame_send.packet.data));
  bytes_read = fread(frame_send.packet.data, sizeof(char), sizeof(frame_send.packet.data), fp);
  if(bytes_read > 0 )
  {   
	  frame_send.seq = nextsumseq;
	  window[nextsumseq]=&frame_send;
	  frame_send.msg_len = bytes_read;
	  if(ferror(fp))
      {
        printf("Error reading from file.\n");
        close(sock_conn);
        fclose(fp);
        exit(1);
      }
	  if(nextsumseq < (base + WINSIZE))
	  {
		  sendto(sock_conn, &frame_send, sizeof(frame_send), 0, (struct sockaddr *)&servaddr, addr_len);
		  if(nextsumseq == base)
		  {
			  setsockopt(sock_conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		  }
		  nextsumseq += 1;
	  }
	  int n = recvfrom(sock_conn, &frame_recv, sizeof(Frame), 0, (struct sockaddr *)&servaddr, &address_length);
	  if(frame_recv.ack)
	  {
		  base = frame_recv.ack + 1;
		  if(n < 0)
		  {
			setsockopt(sock_conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));  
		  }
	  }
	  if(errno==EAGAIN)
	  {
		setsockopt(sock_conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        for(int i=0; i < nextsumseq; i++)
        {
		  frame_send=*window[base+i];
		  sendto(sock_conn, &frame_send, sizeof(frame_send), 0, (struct sockaddr *)&servaddr, addr_len);
		}  
	  }   		  
    }
	else
	{
	 break;
	}
  }
  fclose(fp);
  frame_send.sign_type=2;
  sendto(sock_conn, &frame_send, sizeof(frame_send), 0, (struct sockaddr *)&servaddr, addr_len);
  close(sock_conn);
  exit(0);
}
