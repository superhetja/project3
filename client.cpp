//
// Simple chat client for TSAM-409
//
// Command line: ./chat_client 4000 
//
// Author: Jacky Mallett (jacky@ru.is)
//
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <vector>
#include <thread>

#include <iostream>
#include <sstream>
#include <thread>
#include <map>

std::string wrapString(std::string msg) {
	return '\x02' + msg + '\x03';
}

// Threaded function for handling responss from server

void listenServer(int serverSocket)
{
    int nread;                                  // Bytes read from socket
    char buffer[1025];                          // Buffer for reading input

    while(true)
    {
       memset(buffer, 0, sizeof(buffer));
       nread = read(serverSocket, buffer, sizeof(buffer));

       if(nread == 0)                      // Server has dropped us
       {
          printf("Over and Out\n");
          exit(0);
       }
       else if(nread > 0)
       {
          printf("%s\n", buffer);
       }
    }
}

int main(int argc, char* argv[])
{
   struct addrinfo hints, *svr;              // Network host entry for server
   struct sockaddr_in serv_addr;           // Socket address for server
   int serverSocket;                         // Socket used for server 
   int nwrite;                               // No. bytes written to server
   char buffer[1025];                        // buffer for writing to server
   bool finished;                   
   int set = 1;                              // Toggle for setsockopt

   if(argc != 3)
   {
        printf("Usage: chat_client <ip> <port>\n");
        printf("Ctrl-C to terminate\n");
        exit(0);
   }

   hints.ai_family   = AF_INET;            // IPv4 only addresses
   hints.ai_socktype = SOCK_STREAM;

   memset(&hints,   0, sizeof(hints));

   if(getaddrinfo(argv[1], argv[2], &hints, &svr) != 0)
   {
       perror("getaddrinfo failed: ");
       exit(0);
   }

   struct hostent *server;
   server = gethostbyname(argv[1]);

   bzero((char *) &serv_addr, sizeof(serv_addr));
   serv_addr.sin_family = AF_INET;
   bcopy((char *)server->h_addr,
      (char *)&serv_addr.sin_addr.s_addr,
      server->h_length);
   serv_addr.sin_port = htons(atoi(argv[2]));

   serverSocket = socket(AF_INET, SOCK_STREAM, 0);

   // Turn on SO_REUSEADDR to allow socket to be quickly reused after 
   // program exit.

   if(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
   {
       printf("Failed to set SO_REUSEADDR for port %s\n", argv[2]);
       perror("setsockopt failed: ");
   }

   
   if(connect(serverSocket, (struct sockaddr *)&serv_addr, sizeof(serv_addr) )< 0)
   {
       // EINPROGRESS means that the connection is still being setup. Typically this
       // only occurs with non-blocking sockets. (The serverSocket above is explicitly
       // not in non-blocking mode, so this check here is just an example of how to
       // handle this properly.)
       if(errno != EINPROGRESS)
       {
         printf("Failed to open socket to server: %s\n", argv[1]);
         perror("Connect failed: ");
         exit(0);
       }
   }

    std::string msg;
    msg = wrapString("IM_CLIENT");
    send(serverSocket,msg.c_str(), msg.length(),0);
    // Listen and print replies from server
   std::thread serverThread(listenServer, serverSocket);

   finished = false;
   printf("Welcome to the client\n");
    int command;
    int groupID;
    std::string tmp;

    while(!finished)
    {
        command = 0;
        printf("Enter what command you want to do\n1. Fetch Message\n2. Send Message\n3. List Queryservers\n");
        std::cin >> command;
        if (command == 1){  
            printf("Fetch a message, type group ID which you want to receive message from\n");
            std::cin >> groupID;
            msg = "FETCH_HMSG," + std::to_string(groupID);
            msg = wrapString(msg);
            nwrite = send(serverSocket, msg.c_str(), msg.length(),0);
        }
        else if (command == 2){
            printf("Send a message, type group ID to which group you want to send message\n");
            std::cin >> groupID;

            printf("Type message: ");
            std::cin >> tmp;
            msg = "SEND_MSG," + std::to_string(groupID) + "," + tmp;
            msg = wrapString(msg);
            nwrite = send(serverSocket, msg.c_str(), msg.length(),0);
            command = 0;
        }
        else if (command == 3){
            printf("Listing Queryservers...\n");
            msg = wrapString("QUERYSERVERS");
            nwrite = send(serverSocket, msg.c_str(), msg.length(),0);

        }
        else{printf("Invalid command try again\n ");}

       if(nwrite  == -1)
       {
           perror("send() to server failed: ");
           finished = true;
       }

   }
}
