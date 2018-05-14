#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/time.h>
#include <map>
#include <pthread.h>
#include <mutex>
#include <iostream>

using namespace std;

#define SERVERPORT "8888"  
#define CLIENTPORT "6666"	
#define PEERPORT "7777"

#define BACKLOG 10	 

#define MAXTHREADS 3

#define MAXDATASIZE 100

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_RESET   "\x1b[0m"


pthread_t threads[MAXTHREADS]; 
mutex mtx;
FILE *f;
string argument;
bool pingAlive, sendAlive, rcvAlive;	

struct timeval tv;

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}


void *sendPing(void *fd)
{
	int rv;
	int sockfd=(int)fd;
	
	char buf[5];
	while(1)
	{
		pingAlive=true;
		if(send(sockfd, "PING", 5, 0) <0)
		{
			fprintf(stderr,"Error in sending PING to server\n");
		}
		memset(buf, '\0', 5);
		if((rv = recv(sockfd, buf, 5, 0))>0)
		{
			if(strcmp(buf, "ACK"))
				cout<<"Ping ACKed\n";	
		}
		else 
		{
			if(sendAlive || rcvAlive)
			{
				cout<<"Server Down! Application will exit after current chat is over...\n";
				close(sockfd);
				pingAlive=false;
				pthread_exit(NULL);				
			}
			else
			{
				cout<<"Server Down! Application will exit"<<endl;
				exit(1);
			}
		}
		usleep(10000000);
	}
}

bool getOnlineClients(int sockfd)
{
	int rv;
	if(send(sockfd, "LIST", 5, 0) <0)
	{
		fprintf(stderr,"Error in sending LIST to server\n");
	}

	char buf[MAXDATASIZE];
	memset(buf, '\0', MAXDATASIZE);
	if((rv = recv(sockfd, buf, MAXDATASIZE-1, 0))>0)
	{
		cout<<"Online clients :-\n";
		cout<<string(buf);
	}
	else
	{
		cout<<"Server Down! Application will exit after current chat is over...\n";
		close(sockfd);
		return false;
	}
	return true;

}


void *chatSend(void *fd)
{
	int socket=(int)fd;
	int msgNo=1;
	while(1)
	{
		if(!rcvAlive)		
		{
			cout<<"Connection to peer closed."<<endl;
			close(socket);		
			
			sendAlive=false;
			pthread_exit(NULL);
		}

		sendAlive=true;
		string msg;
		string buf;

		
		msgNo++;
		
		getline(cin, msg);
		
		if(msg=="/exit")	
		{
			cout<<"Received exit msg from user. Application will exit.\n";
			send(socket, (char *)(msg.c_str()), size_t(msg.size()), 0);
			exit(1);
		}

		buf="#"+to_string(msgNo-1)+":";
		buf+=msg;

		if(send(socket, (char *)(buf.c_str()), size_t(buf.size()), 0) <0)
		{
			fprintf(stderr,"Error in sending msg to peer\n");
			close(socket);		
			
			sendAlive=false;
			pthread_exit(NULL);
		}

		
	/*	mtx.lock();
		if (argument=="file")
		{
			f=fopen("chat.txt", "a");
			fprintf(f, "ME%s\n", (char *)(buf.c_str()));
			fclose(f);
		}
		mtx.unlock();*/

	}
}

void *chatRcv(void *fd)
{
	int socket=(int)fd, rv;
	while(1)
	{
		if(!sendAlive)		
		{
			cout<<"Connection to peer closed."<<endl;
			close(socket);		
			
			rcvAlive=false;
			pthread_exit(NULL);
		}

		rcvAlive=true;
		char msg[MAXDATASIZE];
		string buf;
		memset(msg, '\0', MAXDATASIZE);
		if((rv=recv(socket, msg, MAXDATASIZE, 0))>0)
		{
			string sMsg= string(msg);
			
			if(sMsg=="/exit") 
			{
				cout<<"Connection closed by peer. Application will exit"<<endl;
				exit(1);
			}
			if(sMsg.substr(0,3)=="ACK")		
			{
				
				buf+="\t\t\tMSG:"+sMsg.substr(3,sMsg.size()-3)+" seen.\n";
				
			}
			
			else
			{
				string ack="ACK";	
				int i=1;	
				while(sMsg[i]!=':')
				{
					ack+=sMsg[i];	
					i++;
				}
				buf="\tPEER"+sMsg+"\n";


				if(send(socket, (char *)(ack.c_str()), size_t(ack.size()), 0) <0)
				{
					fprintf(stderr,"Error in sending ACK to peer\n");
					cout<<"Connection to peer closed."<<endl;
					close(socket);		
					
					rcvAlive=false;
					pthread_exit(NULL);
				}

			}
		}
		else if (rv<0)
		{
			perror("rcvsdfsdf");
			exit(1);
		}
		else //rv==0
		{
			cout<<"Connection closed by peer. Chat will exit.\n";
			exit(1);
			
		}

		/*
		mtx.lock();
		if (argument=="file")
		{
			f=fopen("chat.txt", "a");
			fprintf(f, "%s", (char *)(buf.c_str()));
			fclose(f);
		}
		else cout<<buf<<endl;
		mtx.unlock();*/
	}

}


int main(int argc, char const *argv[])
{
	signal(SIGPIPE, SIG_IGN);
	int sockfd, numbytes, yes=1, rc;
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
	string peerIP;
	struct sigaction sa;
	socklen_t sin_size;
	
	if (argc != 3) 
	{
		cout<<"Usage: ./client serverIP {stdin/file}\n";
		exit(1);
	}
	
	if(strcmp(argv[2],"stdin")==0)
		f=0;
	else
	{
		cout<<"Usage: ./client serverIP {stdin/file}\n";
		return 1;
	}
	argument=string(argv[2]);


	

	
	tv.tv_sec = 3; 
	tv.tv_usec = 0;  

	

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(argv[1], SERVERPORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

		
	for(p = servinfo; p != NULL; p = p->ai_next) 
	{
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
			p->ai_protocol)) == -1) 
		{
			perror("client: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		
		if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval)))
		{
			perror("setsockopt");
			exit(1);
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}

		break;
	}
	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
		s, sizeof s);
	printf("client: connecting to server at %s\n", s);

	freeaddrinfo(servinfo); 
	char serverMsg[5];
	memset(serverMsg,'\0', 5);

	
	if((rv = recv(sockfd, serverMsg, 5, 0)) <= 0 )
	{
		cout<<"Code Receive from server failed. Application will exit..."<<endl;
		exit(1);
	}

	if(strcmp(serverMsg, "DISC")==0)	
	{
		cout<<"Thread overflow at server. Application will exit..."<<endl;
		exit(1);
	}

	if(strcmp(serverMsg, "CONN")==0)	
	{
		cout<<"Successfully connected to server"<<endl;
	}
	
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}


	if((rc = pthread_create(&threads[0], NULL , sendPing, (void*)sockfd))!=0) 
	{
		fprintf(stderr,"Error:unable to create thread, %d\n",rc);
		return 1;
	}
	

	
	int clientSocket, new_fd;
	// char buf[MAXDATASIZE];
	struct addrinfo *clientInfo;
	struct sockaddr_storage their_addr; // connector's address information
	// char s[INET6_ADDRSTRLEN];
	// string peerIP;
	
	
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, CLIENTPORT, &hints, &clientInfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = clientInfo; p != NULL; p = p->ai_next) 
	{
		if ((clientSocket = socket(p->ai_family, p->ai_socktype,
			p->ai_protocol)) == -1) 
		{
			perror("client: socket");
			continue;
		}

		if (setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(clientSocket, p->ai_addr, p->ai_addrlen) == -1) {
			close(clientSocket);
			perror("server: bind");
			continue;
		}

		break;
	}
	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	freeaddrinfo(clientInfo); 
	
	if (listen(clientSocket, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; 
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
	

	
	fd_set readfds;
	FD_ZERO(&readfds);	

	while(1)
	{
		
		
		FD_SET(clientSocket, &readfds);		//used for adding given fd to a set
		FD_SET(0, &readfds);

		int choice;
		cout<<"Press appropriate key:"<<endl;
		cout<<"Press 1 to get Online Clients List."<<endl;
		cout<<"Press 2 to connect to a peer (need its IP address)."<<endl;
		
		select(clientSocket+1, &readfds, NULL, NULL, NULL);

		
		if(FD_ISSET(clientSocket, &readfds))
		{
			
			char peerMsg[5];
			memset(peerMsg, '\0', 5);

			sin_size = sizeof their_addr;
			new_fd = accept(clientSocket, (struct sockaddr *)&their_addr, &sin_size);
			if (new_fd == -1) 
			{
				perror("accept");
				continue;
			}

			inet_ntop(their_addr.ss_family,
				get_in_addr((struct sockaddr *)&their_addr),
				s, sizeof s);
			printf("got connection from %s\n", s);
			peerIP = s;
			string ans;
			cout<<"Do you want to chat with "<<peerIP<<"?[y/n]:";
			cin>>ans;
			if(ans == "y")
			{
				send(new_fd, (char*)(ans.c_str()), 1, 0);
			}
			else
			{
				send(new_fd, (char*)(ans.c_str()), 1, 0);
				close(new_fd);
				continue;
			}
			cout<<"Connected successfully to peer. You may now start chatting\n\n";

			
			getchar();
			rcvAlive=true, sendAlive=true;
			if(pthread_create(&threads[1], NULL , chatSend, (void*)new_fd)!=0) //for send
			{
				cout<<"Failed to create new thread for chat. Connection to peer will be closed ";
				close(new_fd);
				rcvAlive=false, sendAlive=false;
				continue;
			}
			if( pthread_create(&threads[2], NULL , chatRcv, (void*)new_fd)!=0)
			{
				cout<<"Failed to create new thread for chat. Connection to peer will be closed ";
				rcvAlive=false, sendAlive=false;
				close(new_fd);
				continue;
			}
			
			while(sendAlive && rcvAlive);
		}

		//if stdin gets first
		else if(FD_ISSET(0, &readfds))
		{
			
			cin>>choice;
			// cout<<"choice="<<choice<<endl;
			int peerSocket;
			// char buf[MAXDATASIZE];
			struct addrinfo *peerinfo;
			struct sockaddr_storage their_addr; // connector's address information
			char s[INET6_ADDRSTRLEN];
			// string peerIP;
			struct sigaction sa;


			switch(choice)
			{
				case 1:		
						if(getOnlineClients(sockfd)) continue;
						else exit(1);
						break;

				case 2:		
						cout<<"Enter the IP address you want to connect to: ";
						cin>>peerIP;
						memset(&hints, 0, sizeof hints);
						hints.ai_family = AF_UNSPEC;
						hints.ai_socktype = SOCK_STREAM;

						
						if ((rv = getaddrinfo((char *)(peerIP.c_str()), CLIENTPORT, &hints, &peerinfo)) != 0) {
							fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
							return 1;
						}

							
						for(p = peerinfo; p != NULL; p = p->ai_next) 
						{
							if ((peerSocket = socket(p->ai_family, p->ai_socktype,
								p->ai_protocol)) == -1) 
							{
								perror("client: socket");
								continue;
							}

							if (setsockopt(peerSocket, SOL_SOCKET, SO_REUSEADDR, &yes,
									sizeof(int)) == -1) {
								perror("setsockopt");
								exit(1);
							}

							if (connect(peerSocket, p->ai_addr, p->ai_addrlen) == -1) {
								close(peerSocket);
								perror("client: connect");
								continue;
							}

							break;
						}
						if (p == NULL) {
							fprintf(stderr, "client: failed to connect\n");
							return 2;
						}

						inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
							s, sizeof s);
						printf("connecting to peer at %s\n", s);

						freeaddrinfo(peerinfo); 
			
						memset(buf, '\0', sizeof(buf));
						if((rv = recv(peerSocket, buf, 1, 0))>0)
						{
						
							if(buf[0]== 'y')
								cout<<"Connected successfully to peer. You may now start chatting\n\n\n";
							else
							{
								fprintf(stderr, "Peer denied connection request\n");
								close(peerSocket);
								continue;
							}
						}
						else
						{
							fprintf(stderr, "Peer failed to connect\n");
							close(peerSocket);
							continue;
						}

						rcvAlive=true, sendAlive=true;
						getchar();
						if(pthread_create(&threads[1], NULL , chatSend, (void*)peerSocket)!=0) //for send
						{
							cout<<"Failed to create new thread for chat. Connection to peer will be closed ";
							close(peerSocket);
							rcvAlive=false, sendAlive=false;
							continue;
						}
						if( pthread_create(&threads[2], NULL , chatRcv, (void*)peerSocket)!=0)
						{
							cout<<"Failed to create new thread for chat. Connection to peer will be closed ";
							close(peerSocket);
							rcvAlive=false, sendAlive=false;
							continue;
						}

						//wait for both threads to complete
						while(sendAlive && rcvAlive);
						
						break;	//break of switch case statement
				default:
						cout<<"Enter a valid choice..."<<endl;
			}
		}
		
		if(!pingAlive)	
		{
			cout<<"Connection to server lost. ";
			return 1;
		}
		
	}

	return 0;

}

