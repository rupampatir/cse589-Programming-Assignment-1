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
// There may be some overlap with beej code, specifically
// https://beej.us/guide/bgnet/examples/selectserver.c
// However, the code was just used as is in cases where the code could
// not be any different because it is the cleanest implementation.

#include <stdio.h>
#include <stdlib.h>
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

#define MAXDATASIZE 500
#define MAXDATASIZEBACKGROUND 500*120
#define STDIN 0

struct message {
    char text[MAXDATASIZE];
    struct host *from_client;
    struct message *next_message;
    bool is_broadcast;
};
struct host {
    char hostname[MAXDATASIZE];
    char ip_addr[MAXDATASIZE];
    char port_num[MAXDATASIZE];
    int num_msg_sent;
    int num_msg_rcv;
    char status[MAXDATASIZE];
    int fd;
    struct host *blocked;
    struct host *next_host;
    bool is_logged_in;
    bool is_server;
    struct message *queued_messages;
};

// initialise global variables
struct host *new_client = NULL;
struct host *clients = NULL;
struct host *localhost = NULL;
struct host *server = NULL; // this is used only by the clients to store server info
int yes = 1; // this is used for setsockopt
char received_file_name[MAXDATASIZE];
// TODO: add all function prototypes here later
void execute_command(char *command, int requesting_client_fd);
void client__execute_command(char *command);

void host__send_command(int fd, char *msg);

void *host__get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

bool host__check_valid_ip_addr(char ip_addr[MAXDATASIZE]) {
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ip_addr, &(sa.sin_addr));
    return result != 0;
}

// The following function has been used from https://www.geeksforgeeks.org/c-program-display-hostname-ip-address/
void host__set_hostname_and_ip(struct host *h) {
    char hostbuffer[MAXDATASIZE];
    char *IPbuffer;
    struct hostent *host_entry;
    int hostname;
  
    // To retrieve hostname
    hostname = gethostname(hostbuffer, sizeof(hostbuffer));
    if (hostname == -1) {
        // changePrint("DONOTLOG: Could not get hostname");
		exit(EXIT_FAILURE);
    }
  
    // To retrieve host information
    host_entry = gethostbyname(hostbuffer);
    if (host_entry == NULL) {
        // changePrint("DONOTLOG: Could not get host by name");
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
        // changePrint("DONOTLOG: Could not get addrinfo");
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
        // changePrint("DONOTLOG: Could not bind");
		exit(EXIT_FAILURE);
	}

    // listen
    if (listen(listener, 10) == -1) {
        // changePrint("DONOTLOG: Could not listen");
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
            // changePrint("DONOTLOG: Could not select()");
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
                        // changePrint("DONOTLOG: Could not accept new connection.");
                    } else {
                        // We register the new client onto our system here.
                        new_client = malloc(sizeof(struct host));
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
                        new_client->next_host = NULL;
                        new_client->blocked = NULL;
                        // changePrint("DONOTLOG: New connection from %s on socket %d\n", new_client->ip_addr, new_client->fd);
                    }
                    fflush(stdout);
                } else if (fd == STDIN) {
                    // handle data from standard input
                    char *command = (char*) malloc(sizeof(char)*MAXDATASIZE);
                    memset(command, '\0', MAXDATASIZE);
                    if(fgets(command, MAXDATASIZE - 1, stdin) == NULL) { // -1 because of new line
                        // changePrint("DONOTLOG: Something went wrong reading stdin");
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
                        // changePrint("DONOTLOG: Socket %d hung up\n", fd);
                        close(fd);                  // Close the connection
                        FD_CLR(fd, &master);        // Remove the fd from master set
                    } else if (data_buffer_bytes == -1) {
                        // changePrint("DONOTLOG: Reveive data failed");
                        close(fd);                  // Close the connection
                        FD_CLR(fd, &master);        // Remove the fd from master set
                    } else {
                        // char *command = (char*) malloc(sizeof(char)*MAXDATASIZE);
                        // memset(command, '\0', MAXDATASIZE);
                        // changePrint("%s", data_buffer);
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
        if(fgets(command, MAXDATASIZE, stdin) == NULL) {
            // changePrint("DONOTLOG: Something went wrong reading stdin");
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
   cse4589_print_and_log("[AUTHOR:SUCCESS]\n");
   fflush(stdout);
   cse4589_print_and_log("I, rupampat, have read and understood the course academic integrity policy.\n");
   fflush(stdout);
   cse4589_print_and_log("[AUTHOR:END]\n");
   fflush(stdout);
}

// IP
void host__print_ip_address() {
   cse4589_print_and_log("[IP:SUCCESS]\n");
   fflush(stdout);
   cse4589_print_and_log("IP:%s\n", localhost->ip_addr);
   fflush(stdout);
   cse4589_print_and_log("[IP:END]\n");  
   fflush(stdout);
}

void host__print_port() {
   cse4589_print_and_log("[PORT:SUCCESS]\n");
   fflush(stdout);
   cse4589_print_and_log("PORT:%s\n", localhost->port_num);
   fflush(stdout);
   cse4589_print_and_log("[PORT:END]\n"); 
   fflush(stdout);
}

void host__print_list_of_clients() {
   cse4589_print_and_log("[LIST:SUCCESS]\n");
   fflush(stdout);

    struct host *temp = clients;
    int id = 1;
    while(temp!=NULL) {
        // SUSPICIOUS FOR REFRESH
        if (temp->is_logged_in || !temp->is_server) {
            cse4589_print_and_log("%-5d%-35s%-20s%-8s\n", id, temp->hostname, temp->ip_addr, (temp->port_num));
            fflush(stdout);
            id = id + 1;
        }
        temp = temp->next_host;
    }
    
   cse4589_print_and_log("[LIST:END]\n"); 
   fflush(stdout);
}

void server__print_statistics() {
   cse4589_print_and_log("[STATISTICS:SUCCESS]\n");
   fflush(stdout);

    struct host *temp = clients;
    int id = 1;
    while(temp!=NULL) {
       cse4589_print_and_log("%-5d%-35s%-8d%-8d%-8s\n", id, temp->hostname, temp->num_msg_sent, temp->num_msg_rcv, temp->is_logged_in?"logged-in":"logged-out");
       fflush(stdout);
        id = id + 1;
        temp = temp->next_host;
    }
    
   cse4589_print_and_log("[STATISTICS:END]\n"); 
   fflush(stdout);
}

void server__print_blocked(char blocker_ip_addr[MAXDATASIZE]) {
    struct host *temp = clients;
    while(temp!=NULL) {
        if (strstr(blocker_ip_addr, temp->ip_addr) != NULL) {
            break;
        }
        temp = temp->next_host;
    }
    if(host__check_valid_ip_addr(blocker_ip_addr) && temp) {
           cse4589_print_and_log("[BLOCKED:SUCCESS]\n");
           fflush(stdout);
            struct host *temp_blocked = clients;
            temp_blocked = temp->blocked;
            int id = 1;
            while(temp_blocked!=NULL) {
               cse4589_print_and_log("%-5d%-35s%-20s%-8d\n", id, temp_blocked->hostname, temp_blocked->ip_addr, atoi(temp_blocked->port_num));
               fflush(stdout);
                id = id + 1;
                temp_blocked = temp_blocked->next_host;
            }
    } else {
       cse4589_print_and_log("[BLOCKED:ERROR]\n");
       fflush(stdout);
    }
       
   cse4589_print_and_log("[BLOCKED:END]\n"); 
   fflush(stdout);
}

int client__register_server(char server_ip[], char server_port[]) {
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
            // changePrint("DONOTLOG: Could not get addrinfo");
            return 0;
        }
        
        for(temp_ai = server_ai; temp_ai != NULL; temp_ai = temp_ai->ai_next) {
            server_fd = socket(temp_ai->ai_family, temp_ai->ai_socktype, temp_ai->ai_protocol);
            if (server_fd < 0) { 
                continue;
            }		
            setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
            if (connect(server_fd, temp_ai->ai_addr, temp_ai->ai_addrlen) < 0) {
                // changePrint("DONOTLOG: Could not connect");
                close(server_fd);
                continue;
            }
            break;
        }

        // exit if could not bind
        if (temp_ai == NULL) {
            // changePrint("DONOTLOG: Could not connect");
            return 0;
        }
        
        server->fd = server_fd;
        
        freeaddrinfo(server_ai);

        // Initalisze a listener as well to listen for P2P cibbectuibs
        int listener = 0;
        struct addrinfo *localhost_ai;
        if (status = getaddrinfo(NULL, localhost->port_num, &hints, &localhost_ai) != 0) {
            // changePrint("DONOTLOG: Could not get addrinfo");
            return 0;
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
            // changePrint("DONOTLOG: Could not bind");
            return 0;
        }

        // listen
        if (listen(listener, 10) == -1) {
            // changePrint("DONOTLOG: Could not listen");
            return 0;
        }
        
        localhost->fd = listener;

        freeaddrinfo(localhost_ai);

        return 1;
}

int client__P2P_file_transfer (char peer_ip[], char file_name[MAXDATASIZE]) {
        
    struct host *to_client = clients;
    while(to_client!=NULL) {
        if (strstr(to_client->ip_addr, peer_ip) != NULL) {
            break;
        }
        to_client = to_client->next_host;
    }

    // Connect if not already connected before
    // if (!to_client->fd) {

        int to_client_fd = 0, status;
        struct addrinfo hints, *to_client_ai, *temp_ai;

        // get a socket and bind it
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        if (status = getaddrinfo(to_client->ip_addr, to_client->port_num, &hints, &to_client_ai) != 0) {
            // changePrint("DONOTLOG: Could not get addrinfo");
            exit(EXIT_FAILURE);
        }
        
        for(temp_ai = to_client_ai; temp_ai != NULL; temp_ai = temp_ai->ai_next) {
            to_client_fd = socket(temp_ai->ai_family, temp_ai->ai_socktype, temp_ai->ai_protocol);
            if (to_client_fd < 0) { 
                continue;
            }		
            setsockopt(to_client_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
            if (connect(to_client_fd, temp_ai->ai_addr, temp_ai->ai_addrlen) < 0) {
                // changePrint("DONOTLOG: Could not connect");
                close(to_client_fd);
                continue;
            }
            break;
        }

        // exit if could not bind
        if (temp_ai == NULL) {
            // changePrint("DONOTLOG: Could not connect");
            exit(EXIT_FAILURE);
        }
        
        to_client->fd = to_client_fd;
        
        freeaddrinfo(to_client_ai);
    // }

    // send the filename
    char msg[MAXDATASIZEBACKGROUND];
    sprintf(msg, "FILENAME %s\n", file_name);
    host__send_command(to_client->fd, msg);

    // Send the file
    char buffer[MAXDATASIZE] = {0};
    FILE *file_pointer = fopen(file_name, "r");
    while(fgets(buffer, MAXDATASIZE, file_pointer) != NULL) {
        if (send(to_client->fd , buffer, sizeof(buffer), 0) == -1) {
        cse4589_print_and_log("[DONOTLOG]Error in sending file.");
        fflush(stdout);//    
        }
        bzero(buffer, MAXDATASIZE);
    }

   cse4589_print_and_log("[SENDFILE:SUCCESS]\n");  
   fflush(stdout);
   cse4589_print_and_log("[SENDFILE:END]\n");
   fflush(stdout);

}

void receive_file_from_peer(int peer_fd) {
    int n;
    char buffer[MAXDATASIZE];
    while (1) {
        n = recv(peer_fd, buffer, MAXDATASIZE, 0);
        if (n <= 0){
            break;
        }
        if (strstr(buffer, "FILENAME") != NULL) {
            // sscanf(buffer, "FILENAME %s\n", received_file_name);
        } else {
            FILE *file_pointer = fopen(received_file_name, "w");
            fprintf(file_pointer, "%s", buffer);
        }
        bzero(buffer, MAXDATASIZE);
    }
   cse4589_print_and_log("[RECIEVE:SUCCESS]\n");  
   fflush(stdout);
   cse4589_print_and_log("[RECIEVE:END]\n");
   fflush(stdout);
}

/// Following are for client only
void client__login(char server_ip[], char server_port[]) {
    
    // Register the server if it's their first time. Client, will store,
    // server information
    if (server_ip == NULL || server_port == NULL) {
        cse4589_print_and_log("[LOGIN:ERROR]\n"); 
        cse4589_print_and_log("[LOGIN:END]\n");
        return;
    }
    if (server == NULL) {
        if (!host__check_valid_ip_addr(server_ip) || client__register_server(server_ip, server_port) == 0) {
            cse4589_print_and_log("[LOGIN:ERROR]\n"); 
            cse4589_print_and_log("[LOGIN:END]\n");
            return;
        }
    } else {
        if (strstr(server->ip_addr, server_ip) == NULL || strstr(server->port_num, server_port) == NULL) {
            cse4589_print_and_log("[LOGIN:ERROR]\n"); 
            cse4589_print_and_log("[LOGIN:END]\n");
            return;
        }
    }

    
    // At this point the localhost has successfully logged in
    // we need to make sure everything reflects this

    // The client will send a login message to server with it's details here
    localhost->is_logged_in = true;
    
    char msg[MAXDATASIZEBACKGROUND];
    sprintf(msg, "LOGIN %s %s %s\n", localhost->ip_addr, localhost->port_num, localhost->hostname);
    host__send_command(server->fd, msg);
    
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
    struct sockaddr_storage new_peer_addr;// client address
    socklen_t addrlen = sizeof new_peer_addr;
    
    // main loop
    while(localhost->is_logged_in) {
        read_fds = master;              // make a copy of master set
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            // changePrint("DONOTLOG: Could not select()");
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
                        // changePrint("DONOTLOG: Socket %d hung up\n", fd);
                        close(fd);                  // Close the connection
                        FD_CLR(fd, &master);        // Remove the fd from master set
                    } else if (data_buffer_bytes == -1) {
                        // changePrint("DONOTLOG: Reveive data failed");
                        close(fd);                  // Close the connection
                        FD_CLR(fd, &master);        // Remove the fd from master set
                    } else {
                        // char *command = (char*) malloc(sizeof(char)*MAXDATASIZE);
                        // memset(command, '\0', MAXDATASIZE);
                        // changePrint("%s", data_buffer);
                        execute_command(data_buffer, fd);
                    }
                } else if (fd == STDIN) {
                    // handle data from standard input
                    char *command = (char*) malloc(sizeof(char)*MAXDATASIZE);
                    memset(command, '\0', MAXDATASIZE);
                    if(fgets(command, MAXDATASIZE - 1, stdin) == NULL) { // -1 because of new line
                        // changePrint("DONOTLOG: Something went wrong reading stdin");
                    } else {
                        execute_command(command, STDIN);
                    }
                } else {
                      
                    int new_peer_fd = accept(fd, (struct sockaddr *)&new_peer_addr, &addrlen);

					if (new_peer_fd == -1) {
                        // changePrint("DONOTLOG: Could not accept new connection.");
                    } else {
                        receive_file_from_peer(new_peer_fd);
                    }

                } 
            }
        }

        fflush(stdout);
    }
    
    return;
    
}

void client__refresh_client_list(char clientListString[MAXDATASIZEBACKGROUND]) {
    char newLine[MAXDATASIZE];
    bool is_refresh = false;
    int read;
    clients = malloc(sizeof(struct host));
    struct host *head = clients;
    const char delimmiter[2] = "\n";
    char *token = strtok(clientListString, delimmiter);
    if (strstr(token, "NOTFIRST")) {
        is_refresh = true;
    } 
    if (token != NULL) {
        token = strtok(NULL, delimmiter);
        char client_ip[MAXDATASIZE], client_port[MAXDATASIZE], client_hostname[MAXDATASIZE], command[MAXDATASIZE];
        while (token != NULL) {
            if (strstr(token, "RECEIVE") != NULL) {
                sscanf(token, "%[^\n]", command);
                token = strtok(NULL, delimmiter);
                strcat(command, "\n");
                client__execute_command(command);
            } else {
                struct host *new_client = malloc(sizeof(struct host));
                sscanf(token, "%s %s %s\n", client_ip, client_port, client_hostname);
                token = strtok(NULL, delimmiter);
                clientListString += read;
                memcpy(new_client->port_num, client_port, sizeof(new_client->port_num));
                memcpy(new_client->ip_addr, client_ip, sizeof(new_client->ip_addr));
                memcpy(new_client->hostname, client_hostname, sizeof(new_client->hostname));
                new_client->is_logged_in = true;
                clients->next_host = new_client;
                clients = clients->next_host;
            }
        }
        clients = head->next_host;
        if (is_refresh) {
            cse4589_print_and_log("[REFRESH:SUCCESS]\n");  
            fflush(stdout);
            cse4589_print_and_log("[REFRESH:END]\n");
            fflush(stdout);
        } else {
            client__execute_command("SUCCESSLOGIN");
        }
    }
}

void host__send_command(int fd, char msg[]) {
    int rv;
    if (rv = send(fd, msg, strlen(msg)+1, 0) == -1) {
        // changePrint("DONOTLOG:Something went wrong while sending");
    }
}

void server__broadcast(char msg[], int requesting_client_fd) {
    struct host *temp = clients;
    struct host *from_client = malloc(sizeof(struct host));
    while(temp!=NULL) {
        if (requesting_client_fd == temp->fd) {
            from_client = temp;
        }
        temp = temp->next_host;
    }
    struct host *to_client = clients;
    int id = 1;
    from_client->num_msg_sent++;
    while(to_client!=NULL) {
        if (to_client->fd == requesting_client_fd) {
            to_client = to_client->next_host;
            continue;
        }

        // CHECK IF SENDER IS BLOCKED (FROM IS BLOCKED BY TO)

        bool is_blocked = false;

        struct host *temp_blocked = to_client->blocked;
        while(temp_blocked!=NULL) {
            if (temp_blocked->fd == requesting_client_fd) {
                is_blocked = true;
                break;
            }
            temp_blocked = temp_blocked->next_host;
        }

        if (is_blocked) {
            to_client = to_client->next_host;
            continue;
        }

        char receive[MAXDATASIZE*3];
        
        if (to_client->is_logged_in) {
            to_client->num_msg_rcv++;
            sprintf(receive, "RECEIVE %s %s\n", from_client->ip_addr, msg);
            host__send_command(to_client->fd, receive);
        } else {                        
            struct message *new_message = malloc(sizeof(struct message));
            memcpy(new_message->text, msg, sizeof(new_message->text));
            new_message->from_client = from_client;
            new_message->is_broadcast = true;
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
        to_client = to_client->next_host;
    }
    
   cse4589_print_and_log("[RELAYED:SUCCESS]\n"); 
   fflush(stdout);
   cse4589_print_and_log("msg from:%s, to:255.255.255.255\n[msg]:%s\n", from_client->ip_addr, msg);
   fflush(stdout);
   cse4589_print_and_log("[RELAYED:END]\n");
   fflush(stdout);
   host__send_command(from_client->fd, "SUCCESSBROADCAST\n");
}

void client__block_or_unblock(char command[MAXDATASIZE], bool is_a_block) {
    char client_ip[MAXDATASIZE];
    if (is_a_block) {
        sscanf(command, "BLOCK %s\n", client_ip);
    } else {
        sscanf(command, "UNBLOCK %s\n", client_ip);
    }

    // To check if its in the LIST
    struct host *temp = clients;
    while(temp!=NULL) {
        if (strstr(client_ip, temp->ip_addr) != NULL) {
            break;
        }
        temp = temp->next_host;
    }
    struct host *blocked_client = temp;

    // To check if it's already blocked
    temp = localhost->blocked;
    while(temp!=NULL) {
        if (strstr(client_ip, temp->ip_addr) != NULL) {
            break;
        }
        temp = temp->next_host;
    }
    struct host *blocked_client_2 = temp;

    if (blocked_client != NULL && blocked_client_2 == NULL && is_a_block) {
        struct host *new_blocked_client = malloc(sizeof(struct host));
        memcpy(new_blocked_client->ip_addr, blocked_client->ip_addr, sizeof(new_blocked_client->ip_addr));
        memcpy(new_blocked_client->port_num, blocked_client->port_num, sizeof(new_blocked_client->port_num));
        memcpy(new_blocked_client->hostname, blocked_client->hostname, sizeof(new_blocked_client->hostname));
        new_blocked_client->fd = blocked_client->fd;
        new_blocked_client->next_host = NULL;
        if (localhost->blocked != NULL) {
            struct host *temp_blocked = localhost->blocked;
            while(temp_blocked->next_host !=NULL) {
                temp_blocked = temp_blocked->next_host;
            }
            temp_blocked->next_host = new_blocked_client;
        } else {
            localhost->blocked = new_blocked_client;
        }
        host__send_command(server->fd, command);
    } else if (blocked_client != NULL && blocked_client_2 != NULL && !is_a_block) {
        struct host *temp_blocked = localhost->blocked;
        if (strstr(blocked_client->ip_addr, temp_blocked->ip_addr) != NULL) {
            localhost->blocked = localhost->blocked->next_host;
        } else {
            struct host *previous = temp_blocked;
            while(temp_blocked!=NULL) {
                if (strstr(temp_blocked->ip_addr, blocked_client->ip_addr) != NULL) {
                    previous->next_host = temp_blocked->next_host;
                    break;
                }
                temp_blocked = temp_blocked->next_host;
            }
        }
        host__send_command(server->fd, command);

    } else {
        if (is_a_block) {
           cse4589_print_and_log("[BLOCK:ERROR]\n");  
           fflush(stdout);
           cse4589_print_and_log("[BLOCK:END]\n");
           fflush(stdout);
        } else {
           cse4589_print_and_log("[UNBLOCK:ERROR]\n");  
           fflush(stdout);
           cse4589_print_and_log("[UNBLOCK:END]\n");
           fflush(stdout);
        }
    }
}

void server__block_or_unblock(char command[MAXDATASIZE], bool is_a_block, int requesting_client_fd) {
    char client_ip[MAXDATASIZE], client_port[MAXDATASIZE];;
    if (is_a_block) {
        sscanf(command, "BLOCK %s %s\n", client_ip, client_port);
    } else {
        sscanf(command, "UNBLOCK %s %s\n", client_ip, client_port);
    }
    struct host *temp = clients;
    struct host *requesting_client = malloc(sizeof(struct host));
    struct host *blocked_client = malloc(sizeof(struct host));
    
    while(temp!=NULL) {
        char clientString[MAXDATASIZEBACKGROUND];
        if (temp->fd == requesting_client_fd) {
            requesting_client = temp;
        }
        if (strstr(client_ip, temp->ip_addr) != NULL) {
            blocked_client = temp;
        }
        temp = temp->next_host;
    }

    if (blocked_client != NULL) {
        if (is_a_block) {
            struct host *new_blocked_client = malloc(sizeof(struct host));
            memcpy(new_blocked_client->ip_addr, blocked_client->ip_addr, sizeof(new_blocked_client->ip_addr));
            memcpy(new_blocked_client->port_num, blocked_client->port_num, sizeof(new_blocked_client->port_num));
            memcpy(new_blocked_client->hostname, blocked_client->hostname, sizeof(new_blocked_client->hostname));
            new_blocked_client->fd = blocked_client->fd;
            new_blocked_client->next_host = NULL;
            int new_blocked_client_port_value = atoi(new_blocked_client->port_num);
            if (requesting_client->blocked == NULL) {
                requesting_client->blocked = malloc(sizeof(struct host));
                requesting_client->blocked = new_blocked_client;
            } else if (new_blocked_client_port_value < atoi(requesting_client->blocked->port_num)) {
                new_blocked_client->next_host = requesting_client->blocked;
                requesting_client->blocked = new_blocked_client;
            } else {
                struct host *temp = requesting_client->blocked;
                while (temp->next_host != NULL && atoi(temp->next_host->port_num) < new_blocked_client_port_value) {
                    temp = temp->next_host;
                }
                new_blocked_client->next_host = temp->next_host;
                temp->next_host = new_blocked_client;
            }
        
            host__send_command(requesting_client_fd, "SUCCESSBLOCK\n");
        } else {
            struct host *temp_blocked = requesting_client->blocked;
            if (strstr(temp_blocked->ip_addr, blocked_client->ip_addr) != NULL) {
                requesting_client->blocked = requesting_client->blocked->next_host;
            } else {
                struct host *previous = temp_blocked;
                while(temp_blocked!=NULL) {
                    if (strstr(temp_blocked->ip_addr, blocked_client->ip_addr) != NULL) {
                        previous->next_host = temp_blocked->next_host;
                        break;
                    }
                    temp_blocked = temp_blocked->next_host;
                }
            }
            host__send_command(requesting_client_fd, "SUCCESSUNBLOCK\n");
        }
    } else {
        if (is_a_block) {
            host__send_command(requesting_client_fd, "ERRORBLOCK\n");
        } else {
            host__send_command(requesting_client_fd, "ERRORUNBLOCK\n");
        }
    }
}


void client_exit() {
    host__send_command(server->fd, "EXIT");
    cse4589_print_and_log("[EXIT:SUCCESS]\n");  
    fflush(stdout);
    cse4589_print_and_log("[EXIT:END]\n");
    fflush(stdout);
    exit(0);
}

void send_file(char *client_ip, char *file_path) {

}

void server__handle_login(char client_ip[MAXDATASIZE], char client_port[MAXDATASIZE], char client_hostname[MAXDATASIZE], int requesting_client_fd) {
        char client_return_msg[MAXDATASIZEBACKGROUND*100] = "REFRESHRESPONSE FIRST\n";     
        struct host *temp = clients;
        bool is_new = true;
        struct host *requesting_client = malloc(sizeof(struct host));

        while(temp!= NULL) {
            if(temp->fd == requesting_client_fd) {
                requesting_client = temp;
                is_new = false;
                break;
            }
            temp = temp->next_host;
        }
        
        if (is_new) {
            memcpy(new_client->hostname, client_hostname, sizeof(new_client->hostname));
            memcpy(new_client->port_num, client_port, sizeof(new_client->port_num));
            requesting_client = new_client;
            int client_port_value = atoi(client_port);
            if (clients == NULL) {
                clients = malloc(sizeof(struct host));
                clients = new_client;
            } else if (client_port_value < atoi(clients->port_num)) {
                new_client->next_host = clients;
                clients = new_client;
            } else {
                struct host *temp = clients;
                while (temp->next_host != NULL && atoi(temp->next_host->port_num) < client_port_value) {
                    temp = temp->next_host;
                }
                new_client->next_host = temp->next_host;
                temp->next_host = new_client;
            }

        }
        requesting_client->is_logged_in = true;

        temp = clients;
        while(temp!=NULL) {
            if (temp->is_logged_in) {
                char clientString[MAXDATASIZEBACKGROUND];
                sprintf(clientString, "%s %s %s\n", temp->ip_addr, temp->port_num, temp->hostname);
                strcat(client_return_msg, clientString);
            }
            temp = temp->next_host;
        }

        
        struct message *temp_message = requesting_client->queued_messages;
        char receive[MAXDATASIZE*3];

        while(temp_message!= NULL) {

            requesting_client->num_msg_rcv++;
            sprintf(receive, "RECEIVE %s %s\n", temp_message->from_client->ip_addr, temp_message->text);
            strcat(client_return_msg, receive);

            if (!temp_message->is_broadcast) {
               cse4589_print_and_log("[RELAYED:SUCCESS]\n");  
               fflush(stdout);
               cse4589_print_and_log("msg from:%s, to:%s\n[msg]:%s\n", temp_message->from_client->ip_addr, requesting_client->ip_addr, temp_message->text);
               fflush(stdout);
               cse4589_print_and_log("[RELAYED:END]\n");
               fflush(stdout);
            }
            temp_message = temp_message->next_message;
        }
        host__send_command(requesting_client_fd, client_return_msg);
        requesting_client->queued_messages = temp_message;
}

void server__handle_refresh(int requesting_client_fd) {
        char clientListString[MAXDATASIZEBACKGROUND] = "REFRESHRESPONSE NOTFIRST\n";                
        struct host *temp = clients;
        while(temp!=NULL) {
            if (temp->is_logged_in) {
                char clientString[MAXDATASIZEBACKGROUND];
                sprintf(clientString, "%s %s %s\n", temp->ip_addr, temp->port_num, temp->hostname);
                strcat(clientListString, clientString);
            }
            temp = temp->next_host;
        }
        // changePrint("%d, %s",requesting_client_fd, clientListString);
        host__send_command(requesting_client_fd, clientListString);
}

void server__handle_send(char client_ip[MAXDATASIZE], char msg[MAXDATASIZE] , int requesting_client_fd) {

    char receive[MAXDATASIZE*3];
    struct host *temp = clients;
    struct host *from_client = malloc(sizeof(struct host)), *to_client = malloc(sizeof(struct host));;
    while(temp!=NULL) {
        if (strstr(client_ip, temp->ip_addr) != NULL) {
            to_client = temp;
        } 
        if (requesting_client_fd == temp->fd) {
            from_client = temp;
        }
        temp = temp->next_host;
    }
    if (to_client == NULL || from_client == NULL) {
        // TODO: CHECK IF THIS IS REQUIRED
       cse4589_print_and_log("[RELAYED:ERROR]\n");  
       fflush(stdout);
       cse4589_print_and_log("[RELAYED:END]\n");
       fflush(stdout);
       
        return;
    }

    from_client->num_msg_sent++;
    // CHECK IF SENDER IS BLOCKED (FROM IS BLOCKED BY TO)

    bool is_blocked = false;

    temp = to_client->blocked;
    while(temp!=NULL) {
        if (strstr(from_client->ip_addr, temp->ip_addr) != NULL) {
            is_blocked = true;
            break;
        }
        temp = temp->next_host;
    }
    host__send_command(from_client->fd, "SUCCESSSEND\n");
    if (is_blocked) {
        cse4589_print_and_log("[RELAYED:SUCCESS]\n");  
        fflush(stdout);
        cse4589_print_and_log("msg from:%s, to:%s\n[msg]:%s\n", from_client->ip_addr, to_client->ip_addr, msg);
        fflush(stdout);
        cse4589_print_and_log("[RELAYED:END]\n");
        fflush(stdout);
        host__send_command(from_client->fd, "SUCCESSSEND\n");
        return;
    }

    if (to_client->is_logged_in) {
        to_client->num_msg_rcv++;
        sprintf(receive, "RECEIVE %s %s\n", from_client->ip_addr, msg);
        host__send_command(to_client->fd, receive);

        // TODO: CHECK IF THIS NEEDS TO BE SENT WHEN BLOCKED
        cse4589_print_and_log("[RELAYED:SUCCESS]\n");  
        fflush(stdout);
        cse4589_print_and_log("msg from:%s, to:%s\n[msg]:%s\n", from_client->ip_addr, to_client->ip_addr, msg);
        fflush(stdout);
        cse4589_print_and_log("[RELAYED:END]\n");
        fflush(stdout);
    } else {                        
        struct message *new_message = malloc(sizeof(struct message));
        memcpy(new_message->text, msg, sizeof(new_message->text));
        new_message->from_client = from_client;
        new_message->is_broadcast = false;
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


}

void server__handle_logout(int requesting_client_fd) {
    struct host *temp = clients;
    while(temp!=NULL) {
        if (temp->fd == requesting_client_fd) {
            host__send_command(requesting_client_fd, "SUCCESSLOGOUT\n"); 
            temp->is_logged_in = false;
            break;
        }
        temp = temp->next_host;
    }
    if (temp==NULL) {
        host__send_command(requesting_client_fd, "ERRORLOGOUT\n");
    }
}

void client__handle_receive(char client_ip[MAXDATASIZE], char msg[MAXDATASIZE]) {
   cse4589_print_and_log("[RECEIVED:SUCCESS]\n");  
   fflush(stdout);
   cse4589_print_and_log("msg from:%s\n[msg]:%s\n", client_ip, msg);
   fflush(stdout);
   cse4589_print_and_log("[RECEIVED:END]\n");
   fflush(stdout);
}

void client__print_success_login() {
   cse4589_print_and_log("[LOGIN:SUCCESS]\n");  
   fflush(stdout);
   cse4589_print_and_log("[LOGIN:END]\n");
   fflush(stdout);
}

void server__handle_exit(int requesting_client_fd) {
    struct host *temp = clients;
    if (temp->fd == requesting_client_fd) {
        clients = clients->next_host;
    } else {
        struct host *previous = temp;
        while(temp!=NULL) {
            if (temp->fd == requesting_client_fd) {
                previous->next_host = temp->next_host;
                temp = temp->next_host;
                break;
            }
            // struct host *temp_blocked = temp->blocked;
            // if (temp_blocked->fd == requesting_client_fd) {
            //     temp->blocked = temp->blocked->next_host;
            // } else {
            //     struct host *previous_blocked = temp_blocked;
            //     while(temp_blocked!=NULL) {
            //         if (temp_blocked->fd == requesting_client_fd) {
            //             previous_blocked->next_host = temp_blocked->next_host;
            //             break;
            //         }
            //         temp_blocked = temp_blocked->next_host;
            //     }
            // }
            temp = temp->next_host;
        }
    }
}

void client__send(char command[MAXDATASIZEBACKGROUND]) {
    host__send_command(server->fd, command); 
}

void common__execute_command(char command[], int requesting_client_fd) {
    if (strstr(command, "AUTHOR") != NULL) {
        host__print_author();
    } else if (strstr(command, "IP") != NULL) {
        host__print_ip_address();
    } else if (strstr(command, "PORT") != NULL) {
        host__print_port();
    } 
    fflush(stdout);
}

void server__execute_command(char command[], int requesting_client_fd) {
    if (strstr(command, "LIST") != NULL) {
        host__print_list_of_clients();
    } else if (strstr(command, "STATISTICS") != NULL) {
        server__print_statistics();
    } else if (strstr(command, "BLOCKED") != NULL) {
        char client_ip[MAXDATASIZE];
        sscanf(command, "BLOCKED %s", client_ip);
        server__print_blocked(client_ip);
    } else if (strstr(command, "LOGIN") != NULL) {
        char client_hostname[MAXDATASIZE], client_port[MAXDATASIZE], client_ip[MAXDATASIZE];
        sscanf(command, "LOGIN %s %s %s", client_ip, client_port, client_hostname);
        server__handle_login(client_ip, client_port, client_hostname, requesting_client_fd);
    } else if (strstr(command, "BROADCAST") != NULL) {
        char message[MAXDATASIZE];
        int cmdi = 10;
        int msgi = 0;
        while(command[cmdi]!='\0') {
            message[msgi] = command[cmdi];
            cmdi+=1;
            msgi+=1;
        }
        message[msgi-1]='\0';
        server__broadcast(message, requesting_client_fd); 
    } else if (strstr(command, "REFRESH") != NULL) {
        server__handle_refresh(requesting_client_fd);
    } else if (strstr(command, "SEND") != NULL) {
        char client_ip[MAXDATASIZE], message[MAXDATASIZE];
        sscanf(command, "SEND %s", client_ip);
        int cmdi = 6 + strlen(client_ip);
        int msgi = 0;
        while(command[cmdi]!='\0') {
            message[msgi] = command[cmdi];
            cmdi+=1;
            msgi+=1;
        }
        message[msgi-1]='\0';
        server__handle_send(client_ip, message, requesting_client_fd);
    } else if (strstr(command, "UNBLOCK") != NULL) {
        server__block_or_unblock(command, false, requesting_client_fd); 
    } else if (strstr(command, "BLOCK") != NULL) {
        server__block_or_unblock(command, true, requesting_client_fd); 
    } else if (strstr(command, "LOGOUT") != NULL) {
        server__handle_logout(requesting_client_fd);
    } else if (strstr(command, "EXIT") != NULL) {
        server__handle_exit(requesting_client_fd);
    }
    fflush(stdout);
}

void client__execute_command(char command[]) {
    if (strstr(command, "LIST") != NULL) {
        if (localhost->is_logged_in) {
            host__print_list_of_clients();
         } else {
           cse4589_print_and_log("[LIST:ERROR]\n");
           fflush(stdout);
           cse4589_print_and_log("[LIST:END]\n");
           fflush(stdout);
        }
    } else if (strstr(command, "SUCCESSLOGIN") != NULL) {
        cse4589_print_and_log("[LOGIN:SUCCESS]\n");  
        fflush(stdout);
        cse4589_print_and_log("[LOGIN:END]\n");
        fflush(stdout);
    } else if (strstr(command, "ERRORLOGIN") != NULL) {
        cse4589_print_and_log("[LOGIN:ERROR]\n");  
        fflush(stdout);
        cse4589_print_and_log("[LOGIN:END]\n");
        fflush(stdout);
    } else if (strstr(command, "SUCCESSLOGOUT") != NULL) {
        localhost->is_logged_in = false;
        cse4589_print_and_log("[LOGOUT:SUCCESS]\n");  
        fflush(stdout);
        cse4589_print_and_log("[LOGOUT:END]\n");
        fflush(stdout);
    } else if (strstr(command, "ERRORLOGOUT") != NULL) {
        cse4589_print_and_log("[LOGOUT:ERROR]\n");  
        fflush(stdout);
        cse4589_print_and_log("[LOGOUT:END]\n");
        fflush(stdout);
    } else if (strstr(command, "SUCCESSBROADCAST") != NULL) {
        cse4589_print_and_log("[BROADCAST:SUCCESS]\n");
        fflush(stdout);
        cse4589_print_and_log("[BROADCAST:END]\n");
        fflush(stdout);
    } else if (strstr(command, "SUCCESSUNBLOCK") != NULL) {
        cse4589_print_and_log("[UNBLOCK:SUCCESS]\n");  
        fflush(stdout);
        cse4589_print_and_log("[UNBLOCK:END]\n");
        fflush(stdout);
    } else if (strstr(command, "SUCCESSBLOCK") != NULL) {
        cse4589_print_and_log("[BLOCK:SUCCESS]\n");  
        fflush(stdout);
        cse4589_print_and_log("[BLOCK:END]\n");
        fflush(stdout);
    } else if (strstr(command, "ERRORUNBLOCK") != NULL) {
        cse4589_print_and_log("[UNBLOCK:ERROR]\n");  
        fflush(stdout);
        cse4589_print_and_log("[UNBLOCK:END]\n");
        fflush(stdout);
    } else if (strstr(command, "ERRORBLOCK") != NULL) {
        cse4589_print_and_log("[BLOCK:ERROR]\n");  
        fflush(stdout);
        cse4589_print_and_log("[BLOCK:END]\n");
        fflush(stdout);
    } else if (strstr(command, "SUCCESSSEND") != NULL) {
        cse4589_print_and_log("[SEND:SUCCESS]\n");
        fflush(stdout);
        cse4589_print_and_log("[SEND:END]\n");
        fflush(stdout);
    } else if (strstr(command, "LOGIN") != NULL) { // takes two arguments server ip and server port
        char server_ip[MAXDATASIZE], server_port[MAXDATASIZE];
        int cmdi = 6;
        int ipi = 0;
        while(command[cmdi]!=' ' && ipi<256) {
            server_ip[ipi] = command[cmdi];
            cmdi+=1;
            ipi+=1;
        }
        server_ip[ipi]='\0';
        
        cmdi+=1;
        int pi = 0;
        while(command[cmdi]!='\0') {
            server_port[pi] = command[cmdi];
            cmdi+=1;
            pi+=1;
        }
        server_port[pi-1]='\0'; // REMOVE THE NEW LINE
        client__login(server_ip, server_port);
    } else if (strstr(command, "REFRESHRESPONSE") != NULL) {
        client__refresh_client_list(command);
    } else if (strstr(command, "REFRESH") != NULL) {
        if (localhost->is_logged_in) {
            host__send_command(server->fd, "REFRESH\n");
        } else {
           cse4589_print_and_log("[REFRESH:ERROR]\n");
           fflush(stdout);
           cse4589_print_and_log("[REFRESH:END]\n");
           fflush(stdout);
        }
    } else if (strstr(command, "SENDFILE") != NULL) {
        if (localhost->is_logged_in) {
            // char peer_ip[MAXDATASIZE], file_name[MAXDATASIZE];
            // sscanf(command, "SENDFILE %s %s", peer_ip, file_name);
            // client__P2P_file_transfer(peer_ip, file_name);
        } else {
           cse4589_print_and_log("[SENDFILE:ERROR]\n");
           fflush(stdout);
           cse4589_print_and_log("[SENDFILE:END]\n");
           fflush(stdout);
        }
    } else if (strstr(command, "SEND") != NULL ) {
        if (localhost->is_logged_in) {
            client__send(command);
        } else {
           cse4589_print_and_log("[SEND:ERROR]\n");
           fflush(stdout);
           cse4589_print_and_log("[SEND:END]\n");
           fflush(stdout);
        }
    } else if (strstr(command, "RECEIVE") != NULL) {
        char client_ip[MAXDATASIZE], message[MAXDATASIZE];
        int cmdi = 7;
        int ipi = 0;
        while(command[cmdi]!=' ' && ipi<256) {
            client_ip[ipi] = command[cmdi];
            cmdi+=1;
            ipi+=1;
        }
        client_ip[ipi]='\0';
        
        cmdi+=1;
        int msgi = 0;
        while(command[cmdi]!='\0') {
            message[msgi] = command[cmdi];
            cmdi+=1;
            msgi+=1;
        }
        message[msgi-1]='\0'; // REMOVE THE NEW LINE
        client__handle_receive(client_ip, message);
    } else if (strstr(command, "BROADCAST") != NULL ) {
        if (localhost->is_logged_in) {
            host__send_command(server->fd, command); 
         } else {
           cse4589_print_and_log("[BROADCAST:ERROR]\n");
           fflush(stdout);
           cse4589_print_and_log("[BROADCAST:END]\n");
           fflush(stdout);
        }
    } else if (strstr(command, "UNBLOCK") != NULL ) {
        if (localhost->is_logged_in) {
            client__block_or_unblock(command, false); 
         } else {
           cse4589_print_and_log("[UNBLOCK:ERROR]\n");
           fflush(stdout);
           cse4589_print_and_log("[UNBLOCK:END]\n");
           fflush(stdout);
        }
    } else if (strstr(command, "BLOCK") != NULL) {
        if (localhost->is_logged_in) {
            client__block_or_unblock(command, true); 
         } else {
            cse4589_print_and_log("[BLOCK:ERROR]\n");
            fflush(stdout);
            cse4589_print_and_log("[BLOCK:END]\n");
            fflush(stdout);
        }
    } else if (strstr(command, "LOGOUT") != NULL) {
        if (localhost->is_logged_in) {
            host__send_command(server->fd, command);
        } else {
           cse4589_print_and_log("[LOGOUT:ERROR]\n");
           fflush(stdout);
           cse4589_print_and_log("[LOGOUT:END]\n");
           fflush(stdout);
        }
    } else if (strstr(command, "EXIT") != NULL) {
        client_exit(); 
    } else if (strstr(command, "SENDFILE") != NULL) {
        //split the command into the two arguments. may beed to return pointer from strstr
        // client__send(command, command); 
    }
    fflush(stdout);
}

void execute_command(char command[], int requesting_client_fd) {

    common__execute_command(command, requesting_client_fd);
    
    if (localhost->is_server) {
        server__execute_command(command, requesting_client_fd);
    } else {
        client__execute_command(command);
    }
    fflush(stdout);

}


/**
 * main function
 *
 * @param  argc Number of arguments
 * @param  argv The argument list
 * @return 0 EXIT_SUCCESS
 */
int main(int argc, char **argv) {
	/*Init. Logger*/
	cse4589_init_log(argv[2]);

	/*Clear LOGFILE*/
	fclose(fopen(LOGFILE, "w"));

	/*Start Here*/

	if (argc!=3) {
        // changePrint("DONOTLOG: Invalid number of arguments");
        exit(EXIT_FAILURE);
    }

    // initialise the host
    host__init(strcmp(argv[1], "s") == 0, argv[2]);

	return 0;
}
