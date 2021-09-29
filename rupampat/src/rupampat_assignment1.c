/**
 * @rupampat_assignment1
 * @author  Rupam Patir <rupampat@buffalo.edu>
 * @version 1.0
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION
 *
 * This contains the main function. Add further description here....
 */
#include <stdio.h>
#include <stdlib.h>

// There may be some overlap with beej code, specifically
// https://beej.us/guide/bgnet/examples/selectserver.c
// However, the code was just used as is in cases where the code could
// not be any different because it is the cleanest implementation.

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>

#include "../include/global.h"
#include "../include/logger.h"


#define MAXDATASIZE 256
#define MAXDATASIZEBACKGROUND 3000
//TODO: Check if stdin and STDIN are different
#define STDIN 0

struct message {
    char text[MAXDATASIZE];
    struct host *from_client;
    struct message *next_message;
};

struct host {
    char hostname[MAXDATASIZE];
    char ip_addr[MAXDATASIZE];
    char port_num[MAXDATASIZE];
    int num_msg_sent;
    int num_msg_rcv;
    char status[MAXDATASIZE];
    int fd; // TODO: See if this is required
    struct host *blocked;
    struct host *next_host;
    bool is_logged_in;
    bool is_server;
    struct message *queued_messages;
};

// initialise global variables
struct host *clients;
struct host *localhost;
struct host *server; // this is used only by the clients to store server info
int yes = 1; // this is used for setsockopt

// TODO: add all function prototypes here later
void execute_command(char *command, int requesting_client_fd);
void host__send_command(int fd, char *msg);

void *host__get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// The following function has been used from https://www.geeksforgeeks.org/c-program-display-hostname-ip-address/
void host__set_hostname_and_ip(struct host *h) {
    char hostbuffer[256];
    char *IPbuffer;
    struct hostent *host_entry;
    int hostname;
  
    // To retrieve hostname
    hostname = gethostname(hostbuffer, sizeof(hostbuffer));
    if (hostname == -1) {
        printf("DONOTLOG: Could not get hostname");
		exit(EXIT_FAILURE);
    }
  
    // To retrieve host information
    host_entry = gethostbyname(hostbuffer);
    if (host_entry == NULL) {
        printf("DONOTLOG: Could not get host by name");
		exit(EXIT_FAILURE);
    }
    memcpy(h->ip_addr, inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0])), sizeof(h->ip_addr));
    memcpy(h->hostname, hostbuffer, sizeof(h->hostname));
  
    return;
}

void server__init() {
    int listener = 0, status;
    struct addrinfo hints, *localhost_ai, *temp_ai;

	// get a socket and bind it
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if (status = getaddrinfo(NULL, localhost->port_num, &hints, &localhost_ai) != 0) {
        printf("DONOTLOG: Could not get addrinfo");
		exit(EXIT_FAILURE);
	}
    
    for(temp_ai = localhost_ai; temp_ai != NULL; temp_ai = temp_ai->ai_next) {
    	listener = socket(temp_ai->ai_family, temp_ai->ai_socktype, temp_ai->ai_protocol);
		if (listener < 0) { 
			continue;
		}		
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
		if (bind(listener, temp_ai->ai_addr, temp_ai->ai_addrlen) < 0) {
			close(listener);
			continue;
		}
		break;
	}

    // exit if could not bind
    if (temp_ai == NULL) {
        printf("DONOTLOG: Could not bind");
		exit(EXIT_FAILURE);
	}

    // listen
    if (listen(listener, 10) == -1) {
        printf("DONOTLOG: Could not listen");
        exit(EXIT_FAILURE);
    }
    
    localhost->fd = listener;

    freeaddrinfo(localhost_ai);

    // Now we have a listener_fd. We add it to he master list of fds along with stdin.
    fd_set master;                          // master file descriptor list
    fd_set read_fds;                        // temp file descriptor list for select()
    FD_ZERO(&master);                       // clear the master and temp sets
    FD_ZERO(&read_fds);
    FD_SET(listener, &master);              // Add listener to the master list
    FD_SET(STDIN, &master);            // Add STDIN to the master list
    int fdmax = listener>STDIN?listener:STDIN;                   // maximum file descriptor number. initialised to listener    
    // variable initialisations
    int new_client_fd;                      // newly accept()ed socket descriptor
    struct sockaddr_storage new_client_addr;// client address
    socklen_t addrlen;                      // address length
    char data_buffer[MAXDATASIZE];          // buffer for client data
    int data_buffer_bytes;                             // holds number of bytes received and stored in data_buffer
	char newClientIP[INET6_ADDRSTRLEN];     // holds the ip of the new client
    int fd;

    // main loop
    while(true) {
        read_fds = master;              // make a copy of master set
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            printf("DONOTLOG: Could not select()");
            exit(EXIT_FAILURE);
        }

        // run through the existing connections looking for data to read
        for(fd = 0; fd <= fdmax; fd++) {
            if (FD_ISSET(fd, &read_fds)) {
                // if fd == listener, a new connection has come in.
                
                if (fd == listener) {
                    addrlen = sizeof new_client_addr;
					new_client_fd = accept(listener, (struct sockaddr *)&new_client_addr, &addrlen);

					if (new_client_fd == -1) {
                        printf("DONOTLOG: Could not accept new connection.");
                    } else {
                        // We register the new client onto our system here.
                        struct host *new_client = malloc(sizeof(struct host));
                        FD_SET(new_client_fd, &master); // add to master set
                        if (new_client_fd > fdmax) {    // keep track of the max
                            fdmax = new_client_fd;
                        }
                        memcpy(new_client->ip_addr,
                                inet_ntop(  
                                    new_client_addr.ss_family, 
                                    host__get_in_addr((struct sockaddr*)&new_client_addr), // even though new_client_addr is of type sockaddr_storage, they can be cast into each other. Refer beej docs.
                                    newClientIP, 
                                    INET6_ADDRSTRLEN
                                ), sizeof(new_client->ip_addr));
                        new_client->fd = new_client_fd;
                        new_client->num_msg_sent = 0;
                        new_client->num_msg_rcv = 0;                        
                        new_client->is_logged_in = true;
                        memcpy(new_client->port_num, "UNSET", sizeof(new_client->port_num));
                        if (clients == NULL) {
                            clients = malloc(sizeof(struct host));
                            clients = new_client;
                        } else {
                            struct host *temp = clients;
                            while(temp->next_host !=NULL) {
                                temp = temp->next_host;
                            }
                            temp->next_host = new_client;
                        }
                        // printf("DONOTLOG: New connection from %s on socket %d\n", new_client->ip_addr, new_client->fd);
                    }
                } else if (fd == STDIN) {
                    // handle data from standard input
                    char *command = (char*) malloc(sizeof(char)*MAXDATASIZE);
                    memset(command, '\0', MAXDATASIZE);
                    if(fgets(command, MAXDATASIZE - 1, stdin) == NULL) { // -1 because of new line
                        printf("DONOTLOG: Something went wrong reading stdin");
                    } else {
                        execute_command(command, fd);
                    }
                    fflush(stdout);

                } else {
                    // handle data from a client
                    data_buffer_bytes = recv(fd, data_buffer, sizeof data_buffer, 0);
                    // when recv returns 0, the client has closed the connection;
                    // when recv returns -1, an error occured
                    // when recv returns a value > 0, it is the number of bytes of data
                    if (data_buffer_bytes == 0) {   
                        printf("DONOTLOG: Socket %d hung up\n", fd);
                        close(fd);                  // Close the connection
                        FD_CLR(fd, &master);        // Remove the fd from master set
                    } else if (data_buffer_bytes == -1) {
                        printf("DONOTLOG: Reveive data failed");
                        close(fd);                  // Close the connection
                        FD_CLR(fd, &master);        // Remove the fd from master set
                    } else {
                        // char *command = (char*) malloc(sizeof(char)*MAXDATASIZE);
                        // memset(command, '\0', MAXDATASIZE);
                        // printf("%s", data_buffer);
                        execute_command(data_buffer, fd);
                        // host__send_command(fd, "Hello Client");
                    }
                
                } 
            }
        }
        fflush(stdout);
    }
    
    return;
}

void client__init() {
    while(true)
	{
        // handle data from standard input
        char *command = (char*)malloc(sizeof(char)*MAXDATASIZE);
        memset(command, '\0', MAXDATASIZE);
        // TODO: Check if there is a newline at the end of the buffer
        if(fgets(command, MAXDATASIZE, stdin) == NULL) {
            printf("DONOTLOG: Something went wrong reading stdin");
        } else {
            execute_command(command, STDIN);
        }
	}
}


void host__init(bool is_server, char *port) {
    localhost = malloc(sizeof(struct host));
    memcpy(localhost->port_num, port, sizeof(localhost->port_num));
    localhost->is_server = is_server;    
    host__set_hostname_and_ip(localhost);

    if (is_server) {
        server__init();
    } else {
        client__init();
    }

}

// AUTHOR
void host__print_author() {
    printf("[AUTHOR:SUCCESS]\n");
    printf("I, rupampat, have read and understood the course academic integrity policy.\n");
    printf("[AUTHOR:END]\n");
}

// IP
void host__print_ip_address() {
    printf("[IP:SUCCESS]\n");
    printf("IP:%s\n", localhost->hostname);
    printf("[IP:END]\n");  
}

void host__print_port() {
    printf("[PORT:SUCCESS]\n");
    printf("PORT:%s\n", localhost->port_num);
    printf("[PORT:END]\n"); 
}

void host__print_list_of_clients() {
    printf("[LIST:SUCCESS]\n");

    struct host *temp = clients;
    int id = 1;
    while(temp!=NULL) {
        printf("%-5d%-35s%-20s%-8s\n", id, temp->hostname, temp->ip_addr, (temp->port_num));
        id = id + 1;
        temp = temp->next_host;
    }
    
    printf("[LIST:END]\n"); 
}

void server__print_statistics() {
    printf("[STATISTICS:SUCCESS]\n");

    struct host *temp = clients;
    int id = 1;
    while(temp!=NULL) {
        printf("%-5d%-35s%-8d%-8d%-8s\n", id, temp->hostname, temp->num_msg_sent, temp->num_msg_rcv, temp->is_logged_in?"logged-in":"logged-out");
        id = id + 1;
        temp = temp->next_host;
    }
    
    printf("[STATISTICS:END]\n"); 
}

void server__print_blocked(char *blocker_ip_addr) {
    printf("[BLOCKED:SUCCESS]\n");

    struct host *temp = clients;
    while(temp!=NULL && strcmp(blocker_ip_addr, temp->ip_addr) != 0) {
        temp = temp->next_host;
    }
    temp = temp->blocked;
    int id = 1;
    while(temp!=NULL) {
        printf("%-5d%-35s%-20s%-8d\n", id, temp->hostname, temp->ip_addr, atoi(temp->port_num));
        id = id + 1;
        temp = temp->next_host;
    }

    printf("[BLOCKED:END]\n"); 
}

void client__register_server(char server_ip[], char server_port[]) {
        server = malloc(sizeof(struct host));
        memcpy(server->ip_addr, server_ip, sizeof(server->ip_addr));
        memcpy(server->port_num, server_port, sizeof(server->port_num));
        // TODO: Check valid IP

        int server_fd = 0, status;
        struct addrinfo hints, *server_ai, *temp_ai;

        // get a socket and bind it
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        if (status = getaddrinfo(server->ip_addr, server->port_num, &hints, &server_ai) != 0) {
            printf("DONOTLOG: Could not get addrinfo");
            exit(EXIT_FAILURE);
        }
        
        for(temp_ai = server_ai; temp_ai != NULL; temp_ai = temp_ai->ai_next) {
            server_fd = socket(temp_ai->ai_family, temp_ai->ai_socktype, temp_ai->ai_protocol);
            if (server_fd < 0) { 
                continue;
            }		
            setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
            if (connect(server_fd, temp_ai->ai_addr, temp_ai->ai_addrlen) < 0) {
                printf("DONOTLOG: Could not connect");
                close(server_fd);
                continue;
            }
            break;
        }

        // exit if could not bind
        if (temp_ai == NULL) {
            printf("DONOTLOG: Could not connect");
            exit(EXIT_FAILURE);
        }
        
        server->fd = server_fd;
        
        freeaddrinfo(server_ai);
}
/// Following are for client only
void client__login(char server_ip[], char server_port[]) {
    
    // Register the server if it's their first time. Client, will store,
    // server information
    if (server == NULL) {
        client__register_server(server_ip, server_port);
    }

    
    // At this point the localhost has successfully logged in
    // we need to make sure everything reflects this

    // The client will send a login message to server with it's details here
    localhost->is_logged_in = true;
    
    char msg[MAXDATASIZEBACKGROUND];
    sprintf(msg, "LOGIN %s %s %s", localhost->ip_addr, localhost->port_num, localhost->hostname);
    host__send_command(server->fd, msg);
    printf("[LOGIN:SUCCESS]\n");
    printf("[LOGIN:END]\n"); 

    // Now we have a server_fd. We add it to he master list of fds along with stdin.
    fd_set master;                          // master file descriptor list
    fd_set read_fds;                        // temp file descriptor list for select()
    FD_ZERO(&master);                       // clear the master and temp sets
    FD_ZERO(&read_fds);
    FD_SET(server->fd, &master);              // Add server->fd to the master list
    FD_SET(STDIN, &master);            // Add STDIN to the master list
    int fdmax = server->fd>STDIN?server->fd:STDIN;                   // maximum file descriptor number. initialised to listener    
    // variable initialisations
    char data_buffer[MAXDATASIZE];          // buffer for client data
    int data_buffer_bytes;                             // holds number of bytes received and stored in data_buffer
    int fd;

    // main loop
    while(localhost->is_logged_in) {
        read_fds = master;              // make a copy of master set
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            printf("DONOTLOG: Could not select()");
            exit(EXIT_FAILURE);
        }

        // run through the existing connections looking for data to read
        for(fd = 0; fd <= fdmax; fd++) {
            if (FD_ISSET(fd, &read_fds)) {
                // if fd == listener, a new connection has come in.
                
                if (fd == server->fd) {
                    // handle data from the server
                    data_buffer_bytes = recv(fd, data_buffer, sizeof data_buffer, 0);
                    // when recv returns 0, the client has closed the connection;
                    // when recv returns -1, an error occured
                    // when recv returns a value > 0, it is the number of bytes of data
                    if (data_buffer_bytes == 0) {   
                        printf("DONOTLOG: Socket %d hung up\n", fd);
                        close(fd);                  // Close the connection
                        FD_CLR(fd, &master);        // Remove the fd from master set
                    } else if (data_buffer_bytes == -1) {
                        printf("DONOTLOG: Reveive data failed");
                        close(fd);                  // Close the connection
                        FD_CLR(fd, &master);        // Remove the fd from master set
                    } else {
                        // char *command = (char*) malloc(sizeof(char)*MAXDATASIZE);
                        // memset(command, '\0', MAXDATASIZE);
                        // printf("%s", data_buffer);
                        execute_command(data_buffer, fd);
                    }
                } else if (fd == STDIN) {
                    // handle data from standard input
                    char *command = (char*) malloc(sizeof(char)*MAXDATASIZE);
                    memset(command, '\0', MAXDATASIZE);
                    if(fgets(command, MAXDATASIZE - 1, stdin) == NULL) { // -1 because of new line
                        printf("DONOTLOG: Something went wrong reading stdin");
                    } else {
                        execute_command(command, STDIN);
                    }
                }
            }
        }

        fflush(stdout);
    }
    
    return;
    
}

void client__refresh_client_list(char clientListString[MAXDATASIZEBACKGROUND]) {
    char first[MAXDATASIZE], newLine[MAXDATASIZE];
    int read;
    clients = malloc(sizeof(struct host));
    struct host *head = clients;
    const char delimmiter[2] = "\n";
    char *token = strtok(clientListString, delimmiter);
    if (token != NULL) {
        token = strtok(NULL, delimmiter);
        char client_ip[MAXDATASIZE], client_port[MAXDATASIZE], client_hostname[MAXDATASIZE];
        while (token != NULL) {
            struct host *new_client = malloc(sizeof(struct host));
            sscanf(token, "%s %s %s\n", client_ip, client_port, client_hostname);
            token = strtok(NULL, delimmiter);
            clientListString += read;
            memcpy(new_client->port_num, client_port, sizeof(new_client->port_num));
            memcpy(new_client->ip_addr, client_ip, sizeof(new_client->ip_addr));
            memcpy(new_client->hostname, client_hostname, sizeof(new_client->hostname));
            clients->next_host = new_client;
            clients = clients->next_host;
        }
        clients = head->next_host;
        if (strstr(first, "NOTFIRST")) {
            printf("[REFRESH:SUCCESS]\n");  
            printf("[REFRESH:END]\n");
        }
    } else if (strstr(first, "NOTFIRST")) {
        printf("[REFRESH:ERROR]\n");  
        printf("[REFRESH:END]\n");
    }
}

void host__send_command(int fd, char msg[]) {
    // event should be sent here for server
    int rv;
    if (rv = send(fd, msg, strlen(msg)+1, 0) == -1) {
        printf("DONOTLOG:Something went wrong while sending");
    }
}

void client__broadcast(int fd, char *msg) {
    // we got some data from a client
    // for(j = 0; j <= fdmax; j++) {
    //     // send to everyone!
    //     if (FD_ISSET(j, &master)) {
    //         // except the listener and ourselves
    //         if (j != listener && j != i) {
    //             if (send(j, buf, data_buffer_bytes, 0) == -1) {
    //                 perror("send");
    //             }
    //         }
    //     }
    // }
    printf("[SEND:SUCCESS]\n");  
    printf("[SEND:END]\n");
}

void block_or_unblock(char *msg, bool is_a_block) {
    
}

void client__logout() {
    // destroy server info
    localhost->is_logged_in = false;
    printf("[LOGOUT:SUCCESS]\n");  
    printf("[LOGOUT:END]\n");
    host__send_command(server->fd, "LOGOUT");
}

void client_exit() {
    host__send_command(server->fd, "EXIT");
    printf("[EXIT:SUCCESS]\n");  
    printf("[EXIT:END]\n");
    exit(0);
}

void send_file(char *client_ip, char *file_path) {

}

void server__handle_login(char client_ip[MAXDATASIZE], char client_port[MAXDATASIZE], char client_hostname[MAXDATASIZE], int requesting_client_fd) {
    char clientListString[MAXDATASIZEBACKGROUND] = "REFRESHRESPONSE FIRST\n";                
        struct host *temp = clients;
        struct host *requesting_client;
        
        while(temp!=NULL) {
            char clientString[MAXDATASIZEBACKGROUND];
            if (temp->fd == requesting_client_fd) { // TODO: Remove the second check
                memcpy(temp->hostname, client_hostname, sizeof(temp->hostname));
                memcpy(temp->port_num, client_port, sizeof(temp->port_num));
                requesting_client = temp;
            }
            sprintf(clientString, "%s %s %s\n", temp->ip_addr, temp->port_num, temp->hostname);
            strcat(clientListString, clientString);
            temp = temp->next_host;
        }
        requesting_client->is_logged_in = true;
        host__send_command(requesting_client_fd, clientListString);


         // TODO : REMOVE PORT
        
        struct message *temp_message = requesting_client->queued_messages;
        char receive[MAXDATASIZE*3];

        while(temp_message!= NULL) {
            sprintf(receive, "RECEIVE %s %s\n", temp_message->from_client->ip_addr, temp_message->text);
            printf("%d %s\n", requesting_client_fd, receive);
            host__send_command(requesting_client_fd, receive);
            printf("[EVENT:SUCCESS]\n");  
            printf("msg from:%s, to:%s\n[msg]:%s\n", temp_message->from_client->ip_addr, requesting_client->ip_addr, temp_message->text);
            printf("[EVENT:END]\n");
            temp_message = temp_message->next_message;
        }
}

void server__handle_refresh(char client_hostname[MAXDATASIZE], char client_port[MAXDATASIZE], char client_ip[MAXDATASIZE], int requesting_client_fd) {
    char clientListString[MAXDATASIZEBACKGROUND] = "REFRESHRESPONSE NOTFIRST\n";                
        struct host *temp = clients;
        while(temp!=NULL) {
            char clientString[MAXDATASIZEBACKGROUND];
            sprintf(clientString, "%s %s %s\n", temp->ip_addr, temp->port_num, temp->hostname);
            strcat(clientListString, clientString);
            temp = temp->next_host;
        }
        printf("%d, %s",requesting_client_fd, clientListString);
        host__send_command(requesting_client_fd, clientListString);
}

void server__handle_send(char client_ip[MAXDATASIZE], char msg[MAXDATASIZE], char client_port[MAXDATASIZE], int requesting_client_fd) {

    char receive[MAXDATASIZE*3];
    struct host *temp = clients;
        struct host *from_client, *to_client;
        while(temp!=NULL) {
            if (strstr(client_ip, temp->ip_addr) != NULL && strstr(client_port, temp->port_num)) { // TODO: Remove the second check
                to_client = temp;
            } else if (requesting_client_fd == temp->fd) {
                from_client = temp;
            }
            temp = temp->next_host;
        }
        if (to_client->is_logged_in) {
            sprintf(receive, "RECEIVE %s %s\n", from_client->ip_addr, msg);
            host__send_command(to_client->fd, receive);
        } else {                        
            struct message *new_message = malloc(sizeof(struct message));
            memcpy(new_message->text, msg, sizeof(new_message->text));
            new_message->from_client = from_client;

            if (to_client->queued_messages == NULL) {
                to_client->queued_messages = new_message;
            } else {
                struct message *temp_message = to_client->queued_messages;
                while(temp_message->next_message != NULL) {
                    temp_message = temp_message->next_message;
                }
                temp_message->next_message = new_message;
            }
        }

        // TODO: ASK if this needs to be sent after or before for queued messages
        if (to_client->is_logged_in) {
            printf("[EVENT:SUCCESS]\n");  
            printf("msg from:%s, to:%s\n[msg]:%s\n", from_client->ip_addr, to_client->ip_addr, msg);
            printf("[EVENT:END]\n");
        }

}

void server__handle_logout(int requesting_client_fd) {
    struct host *temp = clients;
    while(temp!=NULL) {
        if (temp->fd == requesting_client_fd) { // TODO: Remove the second check
            temp->is_logged_in = false;
            break;
        }
        temp = temp->next_host;
    }
}

void client__handle_receive(char client_ip[MAXDATASIZE], char msg[MAXDATASIZE]) {
    printf("[EVENT:SUCCESS]\n");  
    printf("msg from:%s\n[msg]:%s\n", client_ip, msg);
    printf("[EVENT:END]\n");
}

void server__handle_exit(int requesting_client_fd) {
    struct host *temp = clients;
        if (temp->fd == requesting_client_fd) {
            clients = clients->next_host;
        } else {
            struct host *previous = temp;
            while(temp!=NULL) {
                if (temp->fd == requesting_client_fd) { // TODO: Remove the second check
                    previous->next_host = temp->next_host;
                    break;
                }
                temp = temp->next_host;
            }
        }
}

void client__send(char command[MAXDATASIZEBACKGROUND]) {
    host__send_command(server->fd, command); 
    printf("[SEND:SUCCESS]\n");  
    printf("[SEND:END]\n");
}

void common__execute_command(char command[], int requesting_client_fd) {
    if (strstr(command, "AUTHOR") != NULL) {
        host__print_author();
    } else if (strstr(command, "IP") != NULL) {
        host__print_ip_address();
    } else if (strstr(command, "PORT") != NULL) {
        host__print_port();
    } else if (strstr(command, "LIST") != NULL) {
        host__print_list_of_clients();
    }
}

void server__execute_command(char command[], int requesting_client_fd) {
    if (strstr(command, "STATISTICS") != NULL) {
        server__print_statistics();
    } else if (strstr(command, "BLOCKED") != NULL) {
        //split the command into the two arguments. may beed to return pointer from strstr
        server__print_blocked(command);
    } else if (strstr(command, "LOGIN") != NULL) { // takes two arguments server ip and server port
        char client_hostname[MAXDATASIZE], client_port[MAXDATASIZE], client_ip[MAXDATASIZE];
        sscanf(command, "LOGIN %s %s %s", client_ip, client_port, client_hostname);
        server__handle_login(client_ip, client_port, client_hostname, requesting_client_fd);
    }else if (strstr(command, "REFRESH") != NULL) {
        char client_hostname[MAXDATASIZE], client_port[MAXDATASIZE], client_ip[MAXDATASIZE];
        sscanf(command, "LOGIN %s %s %s", client_ip, client_port, client_hostname);
        server__handle_refresh(client_ip, client_port, client_hostname, requesting_client_fd);
    } else if (strstr(command, "SEND") != NULL) {
        // TODO : REMOVE PORT
        char client_ip[MAXDATASIZE], msg[MAXDATASIZE], client_port[MAXDATASIZE];
        sscanf(command, "SEND %s %s %s", client_ip, client_port, msg);
        server__handle_send(client_ip, client_port, msg, requesting_client_fd);
    } else if (strstr(command, "LOGOUT") != NULL) {
        server__handle_logout(requesting_client_fd);
    } else if (strstr(command, "EXIT") != NULL) {
        server__handle_exit(requesting_client_fd);
    }
}

void client__execute_command(char command[], int requesting_client_fd) {
    if (strstr(command, "LOGIN") != NULL) { // takes two arguments server ip and server port
        char server_ip[MAXDATASIZE], server_port[MAXDATASIZE];
        sscanf(command, "LOGIN %s %s", server_ip, server_port);
        client__login(server_ip, server_port);
    } else if (strstr(command, "REFRESHRESPONSE") != NULL) {
        client__refresh_client_list(command);
    } else if (strstr(command, "REFRESH") != NULL) {
        host__send_command(server->fd, "REFRESH");
    } else if (strstr(command, "SEND") != NULL) {
        client__send(command);
    } else if (strstr(command, "BROADCAST") != NULL) {
        client__broadcast(server->fd, command); 
    } else if (strstr(command, "SEND") != NULL) {
        host__send_command(server->fd, command);
    } else if (strstr(command, "RECEIVE") != NULL) {
        char client_ip[MAXDATASIZE], msg[MAXDATASIZE];
        sscanf(command, "RECEIVE %s %s\n", client_ip, msg);
        client__handle_receive(client_ip, msg);
    } else if (strstr(command, "BROADCAST") != NULL) {
        client__broadcast(server->fd, command); 
    } else if (strstr(command, "BLOCK") != NULL) {
        //split the command into the two arguments. may beed to return pointer from strstr
        block_or_unblock(command, true); 
    } else if (strstr(command, "UNBLOCK") != NULL) {
        //split the command into the two arguments. may beed to return pointer from strstr
        block_or_unblock(command, false); 
    } else if (strstr(command, "LOGOUT") != NULL) {
        client__logout(); 
    } else if (strstr(command, "EXIT") != NULL) {
        client_exit(); 
    } else if (strstr(command, "SENDFILE") != NULL) {
        //split the command into the two arguments. may beed to return pointer from strstr
        // client__send(command, command); 
    }
}

void execute_command(char command[], int requesting_client_fd) {

    common__execute_command(command, requesting_client_fd);
    
    if (localhost->is_server) {
        server__execute_command(command, requesting_client_fd);
    } else {
        client__execute_command(command, requesting_client_fd);
    }

}


/**
 * main function
 *
 * @param  argc Number of arguments
 * @param  argv The argument list
 * @return 0 EXIT_SUCCESS
 */
int main(int argc, char **argv)
{
	/*Init. Logger*/
	cse4589_init_log(argv[2]);

	/*Clear LOGFILE*/
	fclose(fopen(LOGFILE, "w"));

	/*Start Here*/

	if (argc!=3) {
        printf("DONOTLOG: Invalid number of arguments");
        exit(EXIT_FAILURE);
    }

    // initialise the host
    host__init(strcmp(argv[1], "s") == 0, argv[2]);

	return 0;
}
