//I'm not sure what you mean. You figure out the IP and port (IP of skel and port by looking at the output of ps aux|grep tsam) and the write some code that opens a socket and calls connect with that IP/port. This should not have anything to do with windows or terminal.
// 4001 - 4005
// Simple chat server for TSAM-409
//
// Command line: ./chat_server 4000 
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
#include <list>

#include <iostream>
#include <sstream>
#include <thread>
#include <map>

#include <unistd.h>

// fix SOCK_NONBLOCK for OSX
#ifndef SOCK_NONBLOCK
#include <fcntl.h>
#define SOCK_NONBLOCK O_NONBLOCK
#endif

#define BACKLOG  5          // Allowed length of queue of waiting connections

// Simple class for handling connections from clients.
//
// Client(int socket) - socket to send/receive traffic from client.
class Client
{
  public:
    int sock;              // socket of client connection
    std::string name;           // Limit length of name of client's user

    Client(int socket) : sock(socket){} 

    ~Client(){}            // Virtual destructor defined for base class
};

// Note: map is not necessarily the most efficient method to use here,
// especially for a server with large numbers of simulataneous connections,
// where performance is also expected to be an issue.
//
// Quite often a simple array can be used as a lookup table, 
// (indexed on socket no.) sacrificing memory for speed.

std::map<int, Client*> clients; // Lookup table for per Client information

// Open socket for specified port.
//
// Returns -1 if unable to create the socket for any reason.

int open_socket(int portno)
{
   struct sockaddr_in sk_addr;   // address settings for bind()
   int sock;                     // socket opened for this port
   int set = 1;                  // for setsockopt

   // Create socket for connection. Set to be non-blocking, so recv will
   // return immediately if there isn't anything waiting to be read.
#ifdef __APPLE__     
   if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
   {
      perror("Failed to open socket");
      return(-1);
   }
#else
   if((sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
   {
     perror("Failed to open socket");
    return(-1);
   }
#endif

   // Turn on SO_REUSEADDR to allow socket to be quickly reused after 
   // program exit.

   if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
   {
      perror("Failed to set SO_REUSEADDR:");
   }
   set = 1;
#ifdef __APPLE__     
   if(setsockopt(sock, SOL_SOCKET, SOCK_NONBLOCK, &set, sizeof(set)) < 0)
   {
     perror("Failed to set SOCK_NOBBLOCK");
   }
#endif
   memset(&sk_addr, 0, sizeof(sk_addr));

   sk_addr.sin_family      = AF_INET;
   sk_addr.sin_addr.s_addr = INADDR_ANY;
   sk_addr.sin_port        = htons(portno);

   // Bind to socket to listen for connections from clients

   if(bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0)
   {
      perror("Failed to bind to socket:");
      return(-1);
   }
   else
   {
      return(sock);
   }
}

// Close a client's connection, remove it from the client list, and
// tidy up select sockets afterwards.

void closeClient(int clientSocket, fd_set *openSockets, int *maxfds)
{

     printf("Client closed connection: %d\n", clientSocket);

     // If this client's socket is maxfds then the next lowest
     // one has to be determined. Socket fd's can be reused by the Kernel,
     // so there aren't any nice ways to do this.

     close(clientSocket);      

     if(*maxfds == clientSocket)
     {
        for(auto const& p : clients)
        {
            *maxfds = std::max(*maxfds, p.second->sock);
        }
     }

     // And remove from the list of open sockets.

     FD_CLR(clientSocket, openSockets);

}

void connectToServer(std::string ip,int portno )
{
	struct sockaddr_in serv_addr;
	int serverSocket;
	int set = 1;                              // Toggle for setsockopt

	sockaddr socket_address;
  sockaddr_in *socket_in = (sockaddr_in*)&socket_address;
	socket_in->sin_family = AF_INET;
	socket_in->sin_port = htons(portno);
	socket_in->sin_addr.s_addr = inet_addr(ip.c_str());

	serverSocket = socket(AF_INET, SOCK_STREAM, 0);

	if(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
	{
       printf("Failed to set SO_REUSEADDR for port %d\n", portno);
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
			printf("Failed to open socket to server: %s\n", ip.c_str());
			perror("Connect failed: ");
			exit(0);
		}
	}
	std::cout << "Connected to server:" << std::endl;
	clients[portno] = new Client(serverSocket);

}

// Process command from client on the server
void clientCommand(int clientSocket, fd_set *openSockets, int *maxfds, 
                  char *buffer) 
{
  std::vector<std::string> tokens;
  std::string token;

  // Split command from client into tokens for parsing
  std::stringstream stream(buffer);

  while(stream >> token)
      tokens.push_back(token);

	// connect to skel instructor server
	// CONNECT 103.203.4.4 4055
  if((tokens[0].compare("CONNECT") == 0) && (tokens.size() == 3))
  {
    connectToServer(tokens[1], stoi(tokens[2]) );
  }
  else if(tokens[0].compare("LEAVE") == 0)
  {
      // Close the socket, and leave the socket handling
      // code to deal with tidying up clients etc. when
      // select() detects the OS has torn down the connection.
 
      closeClient(clientSocket, openSockets, maxfds);
  }
  else if(tokens[0].compare("WHO") == 0)
  {
     std::cout << "Who is logged on" << std::endl;
     std::string msg;

     for(auto const& names : clients)
     {
        msg += names.second->name + ",";

     }
     // Reducing the msg length by 1 loses the excess "," - which
     // granted is totally cheating.
     send(clientSocket, msg.c_str(), msg.length()-1, 0);

  }
  // This is slightly fragile, since it's relying on the order
  // of evaluation of the if statement.
  else if((tokens[0].compare("MSG") == 0) && (tokens[1].compare("ALL") == 0))
  {
      std::string msg;
      for(auto i = tokens.begin()+2;i != tokens.end();i++) 
      {
          msg += *i + " ";
      }

      for(auto const& pair : clients)
      {
          send(pair.second->sock, msg.c_str(), msg.length(),0);
      }
  }
  else if(tokens[0].compare("MSG") == 0)
  {
      for(auto const& pair : clients)
      {
          if(pair.second->name.compare(tokens[1]) == 0)
          {
              std::string msg;
              for(auto i = tokens.begin()+2;i != tokens.end();i++) 
              {
                  msg += *i + " ";
              }
              send(pair.second->sock, msg.c_str(), msg.length(),0);
          }
      }
  }
  else if(tokens[0].compare("QUERYSERVERS") == 0 && (tokens.size() == 2)){
      //Reply with the SERVERS response (below)
    	//This should be the first message sent by any server,
      //after it connects to another server.
			// See more in p3
			std::cout << "QUERYSERVERS" << std::endl;
  }
	else if(tokens[0].compare("KEEPALIVE") == 0 && (tokens.size() == 2)){
    //Periodic message to 1-hop connected servers, indicating the number of messages the server sending the
		//KEEPALIVE message has waiting for the receiver. Do
		//not send more than once per minute.
		std::cout << "KEEPALIVE" << std::endl;
  }
	else if(tokens[0].compare("FETCH_MSGS") == 0 && (tokens.size() == 2)){
    //Request messages for the specified group. This may
		//be for your own group, or another group
		std::cout << "FETCH_MSGS" << std::endl;
  }
	else if(tokens[0].compare("SEND_MSG") ==0 && (tokens.size() == 4)){
		//Send a message to another group
		std::cout << "SEND_MSG" << std::endl;
	}
	else if(tokens[0].compare("STATUSREQ") ==0 && (tokens.size() == 2)){
		//Request an overview of the messages held by the
		//server. Reply with STATUSRESP as below
		std::cout << "STATUSREQ" << std::endl;
	}
	else if(tokens[0].compare("STATUSRESP") ==0 && (tokens.size() == 4)){
		//Send a comma separated list of groups and no. of
		//messages you have for them
		//eg. STATUSRESP,P3 GROUP 2,I 1,P3 GROUP4,20,P3 GROUP71,2
		std::cout << "STATUSRESP" << std::endl;
	}
  else
  {
      std::cout << "Unknown command from client:" << buffer << std::endl;
  }
     
}

int main(int argc, char* argv[])
{
    bool finished;
    int listenSock;                 // Socket for connections to server
    int clientSock;                 // Socket of connecting client
    fd_set openSockets;             // Current open sockets 
    fd_set readSockets;             // Socket list for select()        
    fd_set exceptSockets;           // Exception socket list
    int maxfds;                     // Passed to select() as max fd in set
		int nwrite;                               // No. bytes written to server
    struct sockaddr_in client;
    socklen_t clientLen;
    char buffer[1025];              // buffer for reading from clients

    if(argc < 2)
    {
        printf("Usage: chat_server <ip port>\n");
        exit(0);
    }

    // Setup socket for server to listen to

    listenSock = open_socket(atoi(argv[1])); 
    printf("Listening on port: %d\n", atoi(argv[1]));

    if(listen(listenSock, BACKLOG) < 0)
    {
        printf("Listen failed on port %s\n", argv[1]);
        exit(0);
    }
    else 
    // Add listen socket to socket set we are monitoring
    {
        FD_ZERO(&openSockets);
        FD_SET(listenSock, &openSockets);
        maxfds = listenSock;
    }

	// connect to server
	struct sockaddr_in serv_addr;
	int serverSocket;
	int set = 1;                              // Toggle for setsockopt

	// sockaddr socket_address;
  // sockaddr_in *socket_in = (sockaddr_in*)&socket_address;
	// socket_in->sin_family = AF_INET;
	// socket_in->sin_port = htons(atoi(argv[3]));
	// socket_in->sin_addr.s_addr = inet_addr(argv[2]);
	// sockaddr socket_address;
  // sockaddr_in *socket_in = (sockaddr_in*)&socket_address;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[3]));
	serv_addr.sin_addr.s_addr = inet_addr(argv[2]);

	serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
	{
       printf("Failed to set SO_REUSEADDR for port %s\n", argv[3]);
       perror("setsockopt failed: ");

	}
	std::cout << "here" << std::endl;

	if(connect(serverSocket, (struct sockaddr *)&serv_addr, sizeof(serv_addr) )< 0)
	{
		// EINPROGRESS means that the connection is still being setup. Typically this
		// only occurs with non-blocking sockets. (The serverSocket above is explicitly
		// not in non-blocking mode, so this check here is just an example of how to
		// handle this properly.)
		if(errno != EINPROGRESS)
		{
			printf("Failed to open socket to server: %s\n", argv[2]);
			perror("Connect failed: ");
			exit(0);
		}
	} else {
		// connection did not fail..
		clients[serverSocket] = new Client(serverSocket);
		// send message to server...
		memset(buffer, 0, sizeof(buffer));
		strcpy(buffer, "\x02QUERYSERVERS,P3_GROUP_80\x03");
		nwrite = send(serverSocket, buffer, strlen(buffer),0);

		if(nwrite  == -1)
		{
			perror("send() to server failed: ");
			exit(0);
		}
		memset(buffer, 0, sizeof(buffer));
		read(serverSocket, buffer, sizeof(buffer));
		std::cout << buffer << std::endl;
	}




	finished = false;
		
		

    while(!finished)
    {
        // Get modifiable copy of readSockets
        readSockets = exceptSockets = openSockets;
        memset(buffer, 0, sizeof(buffer));

        // Look at sockets and see which ones have something to be read()
        int n = select(maxfds + 1, &readSockets, NULL, &exceptSockets, NULL);

        if(n < 0)
        {
            perror("select failed - closing down\n");
            finished = true;
        }
        else
        {
            // First, accept  any new connections to the server on the listening socket
            if(FD_ISSET(listenSock, &readSockets))
            {
               clientSock = accept(listenSock, (struct sockaddr *)&client,
                                   &clientLen);
               printf("accept***\n");
               // Add new client to the list of open sockets
               FD_SET(clientSock, &openSockets);

               // And update the maximum file descriptor
               maxfds = std::max(maxfds, clientSock) ;

               // create a new client to store information.
               clients[clientSock] = new Client(clientSock);

               // Decrement the number of sockets waiting to be dealt with
               n--;

               printf("Client connected on server: %d\n", clientSock);
            }
            // Now check for commands from clients
            std::list<Client *> disconnectedClients;  
            while(n-- > 0)
            {
               for(auto const& pair : clients)
               {
                  Client *client = pair.second;

                  if(FD_ISSET(client->sock, &readSockets))
                  {
                      // recv() == 0 means client has closed connection
                      if(recv(client->sock, buffer, sizeof(buffer), MSG_DONTWAIT) == 0)
                      {
                          disconnectedClients.push_back(client);
                          closeClient(client->sock, &openSockets, &maxfds);

                      }
                      // We don't check for -1 (nothing received) because select()
                      // only triggers if there is something on the socket for us.
                      else
                      {
                          std::cout << buffer << std::endl;
                          clientCommand(client->sock, &openSockets, &maxfds, buffer);
                      }
                  }
               }
               // Remove client from the clients list
               for(auto const& c : disconnectedClients)
                  clients.erase(c->sock);
            }
        }
    }
}
