## Authors
'''
Hólmfríður Magnea Hákonardóttir (holmfridurh17@ru.is)
Sindri Snær Þorsteinsson (sindrit17@ru.is)
'''

## Bonus Points
'''
    Server is notrunning on skel.ru.is or any campus machine.  (2 bonus points)
'''

## What is needed to install or change if anything
''' 
    Nothing was installed but to be able to connect to other servers than on local internet, Nat Port Forwarding need to be open on some routers.

    In line 84 and 85 in server.cpp we have IP Address of our router and port 4754 which we opened for traffic outside our local network which forwards to port 4080 locally. For this program to work IP and port need to be changed, according to your own router and a port that's open.
    it's these two lines.
    std::string IP = "85.220.119.43";
    std::string PORT = "4753";

'''

## What OS was this project done on
'''
    This project was built on MacOs Big Sur
'''

## How to compile & Run
'''
    In root of file: 
    Compile Client: make client
    Run client: ./client <Server IP addres> <Server local Port>
    to run we used 127.0.0.1 port 4080

    Compile Server: make server
    Run Server: ./tsamgroup80 <Client Local Port> <IP address of server to connect> <port of server to connect>
    We used ./tsamgroup80 4080 130.208.243.61 4003 where the IP address is IP of skel, port 4003 is one of instructors servers.

'''
