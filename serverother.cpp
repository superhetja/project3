//
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
#include <ifaddrs.h>
#include <net/if.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <map>
#include <fstream>
#include <ctime>
#include <unistd.h>

// fix SOCK_NONBLOCK for OSX
#ifndef SOCK_NONBLOCK
#include <fcntl.h>
#define SOCK_NONBLOCK O_NONBLOCK
#endif

#define BACKLOG 5 // Allowed length of queue of waiting connections

// Simple class for handling connections from clients.
//
// Client(int socket) - socket to send/receive traffic from client.
class Client
{
public:
    int sock;         // socket of client connection
    std::string name;


    Client(int socket) : sock(socket) 
    {
        this->sock = socket;
        this->name = "test";
    }

    ~Client() {} // Virtual destructor defined for base class
};

class Server{
    public:
        int sock;
        std::string groupName;
        std::string ip;
        std::string port;
        std::chrono::high_resolution_clock::time_point lastActive;
        std::vector<std::string> messages;
        
        Server(int socket, std::string ip, std::string port)
        {
            this->sock = socket;
            this->ip = ip;
            this->port = port;
            this->groupName = "";
            this->lastActive = std::chrono::system_clock::now();
        }

        ~Server(){}
};
// Note: map is not necessarily the most efficient method to use here,
// especially for a server with large numbers of simulataneous connections,
// where performance is also expected to be an issue.
//
// Quite often a simple array can be used as a lookup table,
// (indexed on socket no.) sacrificing memory for speed.

std::map<int, Client* > clients; // Lookup table for connected clients.
std::map<int, Server* > servers; // Lookup table for connected servers;
std::vector<std::string> myMessages; // vector to hold messages for my group
int clientPort, serverPort;

int open_socket(int portno)
{
    struct sockaddr_in sk_addr; // address settings for bind()
    int sock;                   // socket opened for this port
    int set = 1;                // for setsockopt

    // Create socket for connection. Set to be non-blocking, so recv will
    // return immediately if there isn't anything waiting to be read.
#ifdef __APPLE__
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Failed to open socket");
        return (-1);
    }
#else
    if ((sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
    {
        perror("Failed to open socket");
        return (-1);
    }
#endif

    // Turn on SO_REUSEADDR to allow socket to be quickly reused after
    // program exit.
#ifdef __APPLE__
    if (setsockopt(sock, SOL_SOCKET, SOCK_NONBLOCK, &set, sizeof(set)) < 0)
    {
        perror("Failed to set SOCK_NOBBLOCK");
    }
#else
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
    {
        perror("Failed to set SO_REUSEADDR:");
    }
#endif
    set = 1;

    memset(&sk_addr, 0, sizeof(sk_addr));

    sk_addr.sin_family = AF_INET;
    sk_addr.sin_addr.s_addr = INADDR_ANY;
    sk_addr.sin_port = htons(portno);

    // Bind to socket to listen for connections from clients
    
    if (bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0)
    {
        perror("Failed to bind to socket:");
        return (-1);
    }
    return (sock);
}

void writeToFile(std::string info)
{
    time_t current_time = time(NULL);
    std::ofstream outfile;
    outfile.open("info.log", std::ios_base::app);
    outfile << ctime(&current_time) <<" " << info << std::endl;
    outfile.close();
}

std::string myIp()
{
    struct ifaddrs *myaddrs, *ifa;
    void *in_addr;
    char buf[64];
    std::string output = "";

    // get every ip address and put them in a linked list
    if(getifaddrs(&myaddrs) != 0)
    {
        perror("getifaddrs");
        exit(1);
    }
    // for every ip address in the linked list
    for(ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next)
    {
        // safety checks
        if (ifa->ifa_addr == NULL)
            continue;
        if (!(ifa->ifa_flags & IFF_UP))
            continue;

        switch (ifa->ifa_addr->sa_family)
        {
            // if its IPv4 then we look further
            case AF_INET:
            {
                struct sockaddr_in *s4 = (struct sockaddr_in *)ifa->ifa_addr;
                in_addr = &s4->sin_addr;
                break;
            }
            // we are not interested in IPv6 (sorry...)
            case AF_INET6:
            {
                continue;
            }
            default:
                continue;
        }

        // we put our IPv4 into char buffer for output
        if (!inet_ntop(ifa->ifa_addr->sa_family, in_addr, buf, sizeof(buf)))
        {
            printf("%s: inet_ntop failed!\n", ifa->ifa_name);
        }
        // make sure we aren't looking at localhost
        else if (std::string(buf).compare("127.0.0.1") != 0)
        {
            // this assumes there are at most two ip addresses and that one of them is localhost
            output = std::string(buf);
            break;
        }
    }

    freeifaddrs(myaddrs);
    return output;
}

struct sockaddr_in getSockaddr_in(const char *host, int port) {
    struct hostent *server;
        struct sockaddr_in serv_addr;

    server = gethostbyname(host);
    if (server == NULL) {
        std::cerr << "Error resolving host" << std::endl;
        return serv_addr;
    }
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    bcopy((char *)server->h_addr,
            (char *)&serv_addr.sin_addr.s_addr,
           server->h_length);
    return serv_addr;
}

void closeClient(int clientSocket, fd_set *openSockets)
{
    int clientFd = 0;
    for(const auto& pair: clients)
    {
        if(pair.second->sock == clientSocket)
        {
            clientFd = pair.first;
            
        }
    }    
    clients.erase(clientFd);
    close(clientSocket);
    FD_CLR(clientFd, openSockets);
    std::cout << "Client disconnected: " << clientSocket << std::endl;

}

void closeServer(int serverSocket, fd_set *openSockets)
{
    // Remove client from the clients list
    servers.erase(serverSocket);
    FD_CLR(serverSocket, openSockets);
    close(serverSocket);
    std::cout << "Server disconnected: " << serverSocket << std::endl;  
}

//Adds SoH and EoT to string before sending
std::string addToString(std::string msg)
{
    return '\1' + msg + '\4';
}

//Splits buffer on commas and returns vector of tokens
std::vector<std::string> splitBuffer(std::string buf)
{
    std::vector<std::string> tokens;
    std::string token;
    std::stringstream stream(buf);

    while (std::getline(stream, token, ','))
    {
        tokens.push_back(token);
    }
    return tokens;
}


void CONNECT(std::string ip, std::string port, fd_set &open)
{
    int servSocket;
    struct sockaddr_in sk_addr;

    sk_addr = getSockaddr_in(ip.c_str(), stoi(port));

    servSocket = socket(AF_INET, SOCK_STREAM, 0);
    int n = connect(servSocket, (struct sockaddr *)&sk_addr, sizeof(sk_addr));
    if(n >= 0)
    {
        std::cout << "Succesfully connected to server: " << ip << " on port " << port << std::endl;
        FD_SET(servSocket, &open);
        servers[servSocket] = new Server(servSocket, ip, port);
    }
    else {
        perror("Connection failed");
    }
}

void newClientConnection(int socket, fd_set &open, fd_set &read)
{
    int fd;
    struct sockaddr_in clientAddress;
    socklen_t clientAddr_size;
    
    if(FD_ISSET(socket, &read)) {
        fd = accept(socket, (struct sockaddr *)&clientAddress, &clientAddr_size);
        clients[fd] = new Client(fd);
        FD_SET(fd, &open);
        printf("Client connected on socket: %d\n", socket);
    }
}

void newServerConnection(int sock, fd_set &open, fd_set &read)
{
    int servSocket;
    struct sockaddr_in serverAddress;
    socklen_t serverAddress_size = sizeof(serverAddress);
    char address[INET_ADDRSTRLEN];
    std::string ip, port, listServers = "LISTSERVERS,P3_GROUP_2",stuffedLS;
    if(FD_ISSET(sock, &read)) 
    {
        if(servers.size() < 5)
        {
            servSocket = accept(sock,(struct sockaddr *)&serverAddress, &serverAddress_size);
            FD_SET(servSocket, &open);
            port = std::to_string(serverAddress.sin_port);
            inet_ntop(AF_INET,&(serverAddress.sin_addr), address, INET_ADDRSTRLEN);
            ip = address;
            servers[servSocket] = new Server(servSocket, ip, port);
            printf("Server connected on socket: %d\n", sock);
            listServers = "LISTSERVERS,P3_GROUP_2";
            stuffedLS = addToString(listServers);
            std::cout << "OUT: " << stuffedLS << std::endl;
            send(servSocket, stuffedLS.c_str(), stuffedLS.length(),0);
        } 
    }
}

std::string LISTSERVERS(int sock)
{
    std::string str = "SERVERS,P3_GROUP_2,";
    str += myIp() + "," + std::to_string(serverPort) + ";";
    
    for (auto const &c : servers)
    {
        Server *cl = c.second;
        str += cl->groupName + "," + cl->ip + "," + cl->port + ";";
    }
    return str;
}

void handleClientCommand(fd_set &open, fd_set &read)
{
    char buf[1024];
    
    int n;
    for(const auto& pair: clients)
    {
        bool isActive = true;
        int sock = pair.second->sock;
        if(FD_ISSET(sock, &read))
        {
            memset(buf, 0, sizeof(buf));
            n = recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
            if (n >= 0)
            {   
                std::string str(buf);
                std::vector<std::string> tokens = splitBuffer(str);

                if(tokens[0].compare("CONNECT") == 0 && tokens.size() == 3)
                {
                    std::string ip = tokens[1];
                    tokens[2].erase(std::remove(tokens[2].begin(), tokens[2].end(), '\n'), tokens[2].end());
                    std::string port = tokens[2];
                    CONNECT(ip, port, open);
                } 
                else if(tokens[0].compare("SENDMSG") == 0 && tokens.size() == 3)
                {
                    std::string grp = tokens[1];
                    std::string msg = tokens[2];

                    for(const auto& pair: servers)
                    {
                        if(pair.second->groupName == grp)
                        {
                            pair.second->messages.push_back(msg);
                        }
                    }

                }
                else if(tokens[0].compare("GETMSG") == 0)
                {
                    std::string retMSG = myMessages.back();
                    send(sock, retMSG.c_str(), strlen(retMSG.c_str()), 0);
                    myMessages.pop_back();
                }
                else if( strncmp(buf, "LISTSERVERS", 11) == 0)
                {
                    std::string str = "SERVERS,P3_GROUP_2,";
                    str += myIp() + "," + std::to_string(serverPort) + ";";
                    for (auto const &c : servers)
                    {
                        Server *cl = c.second;
                        str += cl->groupName + "," + cl->ip + "," + cl->port + ";";
                    }
                    send(sock, str.c_str(), strlen(str.c_str()), 0);
                    
                }
            } 
            else {
                isActive = false;
            }
        }
        if(!isActive)
        {
            closeClient(sock, &open);
        }
    }
    
}
std::vector<std::string> splitAndSnitize(std::string buf){
    std::vector<std::string> ret;
    int lastStart = 0;
    for(unsigned int i = 0; i < buf.size(); i++) {
        if(buf[i] == '\4') {
            ret.push_back(buf.substr(lastStart + 1,i - 1));
            lastStart = i+1;
           
        }
    }
    
    return ret;
}
void handleServerCommand(fd_set &open_set, fd_set &read_set)
{
    char buf[1024];
    int n;
    
    for(const auto& pair: servers)
    {
        bool isActive = true;
        int sock = pair.second->sock;
        if(FD_ISSET(sock, &read_set))
        {
            memset(buf, 0, sizeof(buf));
            n = read(sock,buf,sizeof(buf));
            if (n >= 0)
            {
                std::vector<std::string> commands = splitAndSnitize(buf);
                for(const auto& cmd: commands)
                {
                    servers[sock]->lastActive = std::chrono::system_clock::now();
                    //std::string clean = sanitizeMsg(buf);
                    std::vector<std::string> tokens = splitBuffer(cmd);
                    std::cout << std::endl << "IN: " << cmd << std::endl;
                    
                    if (tokens[0].compare("LISTSERVERS") == 0)
                    {
                        servers[sock]->groupName = tokens[1];
                        std::string sendStr = LISTSERVERS(sock);
                        std::cout << "OUT: " << sendStr << std::endl;
                        sendStr = addToString(sendStr);
                        send(sock, sendStr.c_str(), strlen(sendStr.c_str()), 0);
                        std::cout << "OUT: LISTSERVERS,P3_GROUP_2" << std::endl;
                        send(sock, "LISTSERVERS,P3_GROUP_2", strlen("LISTSERVERS,P3_GROUP_2"), 0);
                    }
                    else if(tokens[0].compare("GET_MSG") == 0)
                    {
                        while(servers[sock]->messages.size() > 0)
                        {
                            std::string tmp = "SEND_MSG,P3_GROUP_2," + servers[sock]->groupName + "," + servers[sock]->messages.back();
                            servers[sock]->messages.pop_back();
                            std::cout << "OUT: " << tmp << std::endl;
                            writeToFile(tmp);
                            std::string message = addToString(tmp);
                            send(sock, message.c_str(), strlen(message.c_str()), 0);
                        }
                    }
                    else if(tokens[0].compare("KEEPALIVE") == 0){
                        
                        if(stoi(tokens[1]) > 0){
                            std::string getMSG = "GET_MSG,P3_GROUP_2"; 
                            getMSG = addToString(getMSG);
                            std::cout << "OUT: " << getMSG << std::endl;
                            send(sock, getMSG.c_str(),strlen(getMSG.c_str()), 0);
                        }
                    }   
                    else if(tokens[0].compare("SEND_MSG") == 0){
                        std::string newMsg = "Message from: " + tokens[1] + "\nMessage: " + tokens[3] + "\n";
                        myMessages.push_back(newMsg);
                        writeToFile(newMsg);
                    } 
                    else if(tokens[0].compare("STATUSREQ") == 0){
                        std::string statusresp = "STATUSRESP,";

                        for(const auto& pair: servers)
                        {
                            statusresp += pair.second->groupName + "," + std::to_string(pair.second->messages.size()) + ",";
                        }
                        std::cout << "OUT: " << statusresp << std::endl;
                        statusresp = addToString(statusresp);
                        send(sock, statusresp.c_str(), strlen(statusresp.c_str()), 0);
                    }
                    else if(tokens[0].compare("LEAVE") == 0)
                    {
                        closeServer(sock, &open_set);
                        close(sock);
                    }  

                }  
            } 
            else
            {
                isActive = false;
            }
        }
        if(!isActive)
        {
            closeServer(sock, &open_set);
        }
    }
}
void LEAVE(int sock, fd_set* open_set)
{
    std::string tmp = "LEAVE," + myIp() + "," + std::to_string(serverPort);
    std::string leaveCommand = addToString(tmp);
    send(sock, leaveCommand.c_str(), strlen(leaveCommand.c_str()), 0);
    closeServer(sock, open_set);
}
void keepAlive(fd_set* open_set)
{  
    while(true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        for (auto const &c : servers)
        {
            std::chrono::high_resolution_clock::time_point lastActive = c.second->lastActive;
            std::chrono::high_resolution_clock::time_point timeNow = std::chrono::system_clock::now();
            std::chrono::duration<double> diff = timeNow-lastActive;
            if(diff.count() > 120){
                std::cout << "KICK: " << c.second->groupName;
                LEAVE(c.first, open_set);
                
            }
            std::string tmp = "KEEPALIVE," + std::to_string(c.second->messages.size());
            std::string kaCommand = addToString(tmp);
            send(c.first, kaCommand.c_str(), strlen(kaCommand.c_str()), 0);
            std::cout << "OUT: " << kaCommand.c_str() << std::endl;
        } 
    }
}

int main(int argc, char *argv[])
{
    bool finished;
    int clientSock, serverSock;
    fd_set openSockets, readSockets;

    if (argc != 2)
    {
        printf("Usage: ./P3_GROUP_2 <serverPort>\n");
        exit(0);
    }
    clientPort = 4093;                      //Hardcoded client port - CHANGE THIS IF PORT IS TAKEN
    serverPort = atoi(argv[1]);
    // Setup socket for server to listen to
    serverSock = open_socket(serverPort);
    clientSock = open_socket(clientPort);

    if (listen(serverSock, BACKLOG) < 0)
    {
        printf("Listen failed on port %d\n", serverPort);
        return (-1);
    }
    if (listen(clientSock, BACKLOG) < 0)
    {
        printf("Listen failed on port %d\n", clientPort);
        return (-1);
    }

    FD_ZERO(&openSockets);
    FD_SET(clientSock, &openSockets);
    FD_SET(serverSock, &openSockets);
    finished = false;

    std::thread threadKeepAlive(keepAlive, &openSockets);
    threadKeepAlive.detach();
    
    while (!finished)
    {
        // Get modifiable copy of readSockets
        readSockets = openSockets;

        if (select(FD_SETSIZE, &readSockets, NULL, 0, NULL) < 0)
        {
            perror("select failed - closing down\n");
            finished = true;
        }
        else
        {
            //Handle new connection from client
            newClientConnection(clientSock, openSockets, readSockets);
            //Handle new connection from server
            newServerConnection(serverSock, openSockets, readSockets);
            //Handle command from connected servers
            handleServerCommand(openSockets, readSockets);  
            //Handle command from connected clients
            handleClientCommand(openSockets, readSockets);
        }
        
        

    }
}