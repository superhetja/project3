// 4001 - 4005
// parsing use regex
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

#include <fstream>
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

#define BACKLOG  10          // Allowed length of queue of waiting connections
#define BUFFER_SIZE 1024 // Maximum size of buffer

// Simple class for handling connections from clients.
//
// Client(int socket) - socket to send/receive traffic from client.
std::map<std::string, std::vector<std::string>> messages; // Lookup table for messages avaiable in the system now..
class Client
{
	public:
		int sock;              // socket of client connection
    	std::string name;           // Limit length of name of client's user

    	Client(int socket) : sock(socket){} 

    	~Client(){}            // Virtual destructor defined for base class
};

class Server
{
	public:
		int sock; // socket of server connection
		std::string name;
		std::string ip;
		std::string port;
		
		Server(int socket) : sock(socket){}

		~Server(){}
};

// Note: map is not necessarily the most efficient method to use here,
// especially for a server with large numbers of simulataneous connections,
// where performance is also expected to be an issue.
//
// Quite often a simple array can be used as a lookup table, 
// (indexed on socket no.) sacrificing memory for speed.

std::map<int, Client*> clients; // Lookup table for per Client information
std::map<int, Server*> servers; // Lookup table for per Server information


std::string GROUP_ID = "P3_GROUP_80";
std::string IP = "85.220.119.43";
std::string PORT = "4753";

bool serversContainName(std::string name){
	if(name == GROUP_ID) return true;

	for(const auto& pair: servers) {
		if(pair.second->name == name)
		{
			return true;
		}
	}
	return false;
}

std::vector<std::string> splitCommands(std::string buffer){
	std::vector<std::string> commands;
	
	
	int from = 0;
    for(unsigned int i = 0; i < buffer.size(); i++) 
    {
        if (buffer[i] == '\x03')
	    {
	        commands.push_back(buffer.substr(from+1, i-1));
	        from = i+1;
	    }
    }
	return commands;
}

// Returns true if max servers connected has been reached, else false
bool maxServerReached(){
	return servers.size() >= 15;
}

// Wraps string in STX, ETX before sending 
std::string wrapString(std::string msg) {
	return '\x02' + msg + '\x03';
}

void writeToFile(std::string buffer) {
	time_t current_time = time(NULL);
	std::ofstream out;
	out.open("messages.log", std::ios_base::app);
	out << ctime(&current_time) << " " << buffer << std::endl;
	out.close();
}

// send QUERYSERVERS to other servers when connecting to them.
void sendQueryserversReq(int socket){
	std::string msg = "QUERYSERVERS," + GROUP_ID + "," + IP + "," + PORT + ";";
	msg = wrapString(msg);
	std::cout << "SENDING: " << msg << std::endl;
	send(socket, msg.c_str(), msg.length(), 0);

}

void sendServersRes(int socket) {
	std::string msg = "SERVERS," + GROUP_ID + "," + IP + "," + PORT + ";";
	for(auto const &pair: servers) {
		msg += pair.second->name + "," + pair.second->ip + "," + pair.second->port + ";";
	}
	msg = wrapString(msg);
	std::cout << "SENDING: " << msg << std::endl;
	send(socket, msg.c_str(), msg.length(), 0);
}

// responding to STATUSREQ
void sendStatusrespRes(int socket, std::string group) {
	// STATUSRESP,<our group id>,<the group we got the request from>,<group_x>,<number of messages for group_x>,<group_y>,<number of messages for group_y>,…
	std::string msg = "STATUSRESP," + GROUP_ID + "," + group + ",";
	for(auto const &pair: messages) {
		msg += pair.first + "," + std::to_string(pair.second.size());
	}
	msg = wrapString(msg);
	send(socket, msg.c_str(), msg.length(), 0);
}

// responding to FETCH_MSGS
void sendMessage(int socket, std::string group) {
	if(messages[group].size() == 0) {
		return;
	}
	std::string msg = "SEND_MSG" + group + "," + GROUP_ID + ",";
	msg += messages[group].back();
	messages[group].pop_back();
	msg = wrapString(msg);
	send(socket, msg.c_str(), msg.length(), 0);
}

void cSendMessage(std::string group, std::string message) {

	std::string msg = "SEND_MSG," + group + "," + GROUP_ID + "," + message;
	msg = wrapString(msg);
	std::cout << "SENDING: " << msg << std::endl;
	for( auto const &pair : servers ) {
		if (pair.second->name == group) {
			send(pair.second->sock, msg.c_str(), msg.length(), 0);
		}
	}
}

void fetchMessage(std::string group) {
	std::string msg = "FETCH_MSGS," + GROUP_ID;
	msg = wrapString(msg);
	std::cout << "SENDING: " << msg << std::endl;
	for( auto const &pair : servers ) {
		if (pair.second->name == group) {
			send(pair.second->sock, msg.c_str(), msg.length(), 0);
		}
	}
}

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

void connectToMoreServers(char *buffer, fd_set *openSockets);

void closeSocket(int clientSocket, fd_set *openSockets, int *maxfds)
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



void serverCommand(int serverSocket, fd_set *openSockets, char *buffer)
{
	std::vector<std::string> commands = splitCommands(buffer);
	
	for (auto command : commands) {
		std::string msg;
		std::vector<std::string> tokens;
		std::string token;
		std::stringstream stream(command);

		while(std::getline(stream, token, ',')) {
			tokens.push_back(token);
		}
		

		if(tokens[0].compare("QUERYSERVERS") == 0 && tokens.size() == 4) {
		std::cout << "responding to QUERYSERVERS to " << command << std::endl;
		if (!servers[serverSocket]) {
			servers[serverSocket] = new Server(serverSocket);
			printf("NEW CONNECTION TO SERVER: %s, %s, %s on socket: %d\n", tokens[1].c_str(), tokens[2].c_str(), tokens[3].c_str(), serverSocket);
		}
		servers[serverSocket]->name = tokens[1];
		servers[serverSocket]->ip = tokens[2];
		servers[serverSocket]->port = tokens[3];
		
		sendServersRes(serverSocket);
		
		} 
		else if (tokens[0].compare("QUERYSERVERS") == 0 && tokens.size() == 2) 
		{
			servers[serverSocket]->name = tokens[1];
			sendServersRes(serverSocket);
		}
		else if (tokens[0].compare("KEEPALIVE") == 0 && stoi(tokens[1]) > 0) {
			printf("Sending KEEPALIVE\n");
			msg = "FETCH_MSGS,P3_GROUP_80";
			msg = wrapString(msg);
			std::cout << msg << std::endl;
			send(serverSocket, msg.c_str(), msg.length(), 0);

		} else if (tokens[0].compare("STATUSREQ") == 0 && tokens.size() == 2)
		{
			// send STATUSRESP to group(tokens[1])
			std::cout << "Sending STATUSREQ" << std::endl;
			sendStatusrespRes(serverSocket, tokens[1]);
		}
		else if (tokens[0].compare("SEND_MSG") == 0 && tokens.size() == 4) {
			// if to_group(token[1]) !== OUR_GROUP_ID then store in map to be send when the group fetches the message(tokens[3])
			// if it is to us the writetoFile...
			// SEND_MSG,P3_GROUP_<to group number>,P3_GROUP_<from group number>,<message content>
			std::cout << "SAVING MESSAGE TO FILE!" << std::endl;
			if(tokens[1] == GROUP_ID) {
				writeToFile(command);
			} else {
				messages[tokens[1]].push_back(tokens[3]);
				writeToFile(command);
			}

		}
		else if (tokens[0].compare("FETCH_MSGS") == 0) {
			// For every message that we have for the requesting group(tokens[1]), 
			// we will remove it from our storage and send it via a SEND_MSG request,
			// If we have no message for the requesting group, we will not send anything.
			sendMessage(serverSocket, tokens[1]);
		}
		else if (tokens[0].compare("IM_CLIENT") == 0) {
			std::cout << "CHANGING TO CLIENT : " << token[0] << std::endl;
			clients[serverSocket] = new Client(serverSocket);
			servers.erase(serverSocket);
		} 
		else if (tokens[0].compare("SERVERS") == 0){
			if (maxServerReached()) {
				printf("Received SERVERS message but we are full we do not need to connect to more servers\n");
			} else {
				printf("Received SERVERS connecting to more servers\n");
				connectToMoreServers(buffer, openSockets);
			}
		}
		else
		{
			std::cout << "Unknown command from server:" << command << std::endl;
		}
	}

}

int connectToServer(std::string ip,int portno, fd_set *openSockets)
{
	struct sockaddr_in serv_addr;
	int serverSocket, nwrite;
	int set = 1;                              // Toggle for setsockopt
	char buffer[1025];
	struct timeval timeout;      
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
	char *ptr;

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(portno);
	serv_addr.sin_addr.s_addr = inet_addr(ip.c_str());

	serverSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (setsockopt (serverSocket,SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
	{
      	printf("Failed to set SO_SNDTIMEO for port %d\n", portno);
		perror("setsockopt failed\n");
		return -1;
	}

	if(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
	{ 
       	printf("Failed to set SO_REUSEADDR for> port %d\n", portno);
       	perror("setsockopt failed: ");
		return -1;
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
			return -1;
		}
			return -1;
	}
	FD_SET(serverSocket, openSockets);
	std::cout << "Connected to server!!" << std::endl;
	// create new Server here since isntructior server doesn't send ip and port
	servers[serverSocket] = new Server(serverSocket);
	servers[serverSocket]->ip = ip;
	servers[serverSocket]->port = std::to_string(portno);

	memset(buffer, 0, sizeof(buffer));
	//read the name, from the queryServers and set to servers
	read(serverSocket, buffer, sizeof(buffer));
	std::cout << buffer << std::endl;
	
	serverCommand(serverSocket, openSockets, buffer);

	sendQueryserversReq(serverSocket);




	return serverSocket;
	
}

// Process command from client on the server
void clientCommand(int clientSocket, fd_set *openSockets, int *maxfds, 
                  char *buffer) 
{
	std::vector<std::string>commands = splitCommands(buffer);
  	std::vector<std::string> tokens;
  	std::string token;

  // Split command from client into tokens for parsing
  	std::stringstream stream(commands[0]);
	while(std::getline(stream, token,',')) {
		tokens.push_back(token);
	}

	if((tokens[0].compare("SEND_MSG") == 0) && (tokens.size() == 3))
	{
		// send message(token[2]) to group_id(token[1])
		cSendMessage(tokens[1], tokens[2]);
		
	}
	else if(tokens[0].compare("QUERYSERVERS") == 0 && (tokens.size() == 2)) {
		// SERVERS,P3 GROUP 1,130.208.243.61,8888;P3 GROUP 2,10.2.132.12,10042;
		std::string msg;
		msg = "SERVERS,";
		for(auto const &pair: servers) {
			msg += pair.second->name + "," + pair.second->ip + "," + pair.second->port + ";";
		}
		send(clientSocket, msg.c_str(), msg.length(), 0);

	}
  	else if(tokens[0].compare("FETCH_MSG") == 0 && tokens.size() == 2)
  	{
      	// search for message from groupid (token[1])
		//send that message to client
		fetchMessage(tokens[1]);
  	}
	else
	{
		std::cout << "Unknown command from client:" << buffer << std::endl;
	}
     
}



void connectToMoreServers(char *buffer, fd_set *openSockets) {
	std::vector<std::string> commands = splitCommands(buffer);

	char *ip, *port, *name, *tptr;
	int tmpSock;
	// std::cout << "inside ConnectToMoreSErvers" << std::endl;
	// std::cout << "Buffer: " << buffer << std::endl;
	for(auto command : commands) {
		// std::cout << "command: " << command << std::endl;
		
		std::string token, tmp;
		// check if this is the SERVERS response..
		if(command.find("SERVERS") != std::string::npos) {
			std::stringstream stream(command);
			// split on ;
			while(std::getline(stream, tmp, ';') && servers.size() < 3) {
				std::vector<std::string> tokens;
				// std::cout << "Server connecting to " << tmp << std::endl;
				std::stringstream stream2(tmp);
				// then split on , and put as token in tokens.
				while(std::getline(stream2, token, ',')) {
					tokens.push_back(token);
				}
				if ( tokens.size() == 3) {
					if(!(tokens[0].compare("SERVERS") == 0) && !serversContainName(tokens[0]) && servers.size() < 3) {
						connectToServer(tokens[1], stoi(tokens[2]), openSockets);
					}
				}
			}
		}


	}


//   // Split command from client into tokens for parsing
//   std::stringstream stream(buffer);

//   	while(std::getline(stream, tmp, ';')) {
// 		if (tmp.find("SERVERS") != std::string::npos) {

// 			std::cout << "NOT" << std::endl;
// 			std::cout << tmp << std::endl;
// 		} else {
// 			tptr = (char *)alloca(tmp.size() + 1);
// 			memcpy(tptr, tmp.c_str(), tmp.size() + 1);
// 			name = strtok(tptr, ","); //name
// 			ip = strtok(NULL, ","); //ip
// 			port = strtok(NULL, ","); // port
// 			if (!serversContainName(name) && !maxServerReached()) {
// 				std::cout << "connecting to server..." << std::endl;
// 				tmpSock = connectToServer(ip, atoi(port), openSockets);
// 			}
// 		}
		
// 	}
	
}

int main(int argc, char* argv[])
{
    bool finished, connected;
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
	int tmpSock;

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


	finished = false;
	connected = false;
		
	

    while(!finished)
    {
		if(!connected){
			printf("Connecting to instructor server\n");
			tmpSock = connectToServer(argv[2], atoi(argv[3]), &openSockets);
			
			// here Im getting QueryServers,id
			connected = true;
		}
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
				if(maxServerReached()) {
					int sock = servers.begin()->first;
					closeSocket(sock, &openSockets, &maxfds);
					servers.erase(sock);
					
				}
				// sockets are actually serversocket unless it is my client...
				clientSock = accept(listenSock, (struct sockaddr *)&client,
									&clientLen);
				printf("accept***\n");
				// Add new client to the list of open sockets
				FD_SET(clientSock, &openSockets);

				// And update the maximum file descriptor
				maxfds = std::max(maxfds, clientSock) ;

				// create a new client to store information.
				servers[clientSock] = new Server(clientSock);

				memset(buffer, 0, sizeof(buffer));
				
				read(clientSock, buffer, sizeof(buffer));
				sendQueryserversReq(clientSock);
				serverCommand(clientSock, &openSockets, buffer);


				// get the SERVERS respond
				memset(buffer, 0, sizeof(buffer));
				read(clientSock, buffer,sizeof(buffer));

				connectToMoreServers(buffer, &openSockets);
				



				// Decrement the number of sockets waiting to be dealt with
				n--;

				printf("Client connected on server: %d\n", clientSock);
            }
            // Now zts
            std::list<Client *> disconnectedClients;  
            std::list<Server *> disconnectedServers;  
            while(n-- > 0)
            {
            	for(auto const& pair : clients)
               	{
                	Client *client = pair.second;

                  	if(FD_ISSET(client->sock, &readSockets))
                  	{
                      	// recv() == 0 means client has closed connection
						memset(buffer, 0, sizeof(buffer));
                      	if(recv(client->sock, buffer, sizeof(buffer), MSG_DONTWAIT) == 0)
                      	{
                          	disconnectedClients.push_back(client);
                          	closeSocket(client->sock, &openSockets, &maxfds);

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
				for(auto const& pair : servers) {
					Server *server = pair.second;
					if(FD_ISSET(server->sock, &readSockets)) {
						// std::cout <<"BEFORE BUFFER" << std::endl;
						memset(buffer, 0, sizeof(buffer));
						// std::cout <<"AFTER BUFFER" << std::endl;
						if(recv(server->sock, buffer, sizeof(buffer), MSG_DONTWAIT) == 0) 
						{
							// std::cout << "DISCONNECTING SERVER" << std::endl;
							disconnectedServers.push_back(server);
                          	closeSocket(server->sock, &openSockets, &maxfds);
							// std::cout << "AFTER DISCONNECTING SERVER" << std::endl;
						}
						else
						{
							// std::cout << "PRINTING BUFFER" << std::endl;
							std::cout << buffer << std::endl;
							// std::cout << "AFTER PRINTING BUFFER" << std::endl;
							// std::cout << "SERVER COMMAND" << std::endl;
							serverCommand(server->sock, &openSockets, buffer);
							// std::cout << "AFTER SERVER COMMAND" << std::endl;
						}
					}

				}
               // Remove client from the clients list
				// std::cout << "DELETEING CLIENT" << std::endl;
               	for(auto const& c : disconnectedClients)
                  	clients.erase(c->sock);
				// std::cout << "AFTER DELETEING CLIENT" << std::endl;
               	for(auto const& s : disconnectedServers)
                  	servers.erase(s->sock);
				// std::cout << "AFTER DISCONNECTING SERVER" << std::endl;
            }
        }
    }
}
