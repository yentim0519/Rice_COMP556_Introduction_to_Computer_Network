#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> /* Provides use of select() */
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/time.h>

/* A linked list node data structure to maintain application
   information related to a connected socket */
struct node
{
	int socket;
	struct sockaddr_in client_addr;
	int pending_data; /* flag to indicate whether there is more data to send */
	/*  you will need to introduce some variables here to record
		all the information regarding this socket.
		e.g. what data needs to be sent next 	*/
	struct node *next;
};

/* Delete one node in the linked list associated with a connected socket
   used when tearing down the connection */
void dump(struct node *head, int socket)
{
	struct node *current, *temp;

	current = head;

	while (current->next)
	{
		if (current->next->socket == socket)
		{
			/* remove */
			temp = current->next;
			current->next = temp->next;
			free(temp); /* don't forget to free memory */
			return;
		}
		else
		{
			current = current->next;
		}
	}
}

/* Create the data structure associated with a connected socket
	then add to linked list */
void add(struct node *head, int socket, struct sockaddr_in addr)
{
	// Each socket represented w/ a node
	struct node *new_node;

	new_node = (struct node *)malloc(sizeof(struct node));
	new_node->socket = socket;
	new_node->client_addr = addr;
	new_node->pending_data = 0;
	new_node->next = head->next;
	head->next = new_node;
}

/*****************************************/
/* main program                          */
/*****************************************/
int main(int argc, char **argv)
{ // command line takes in one arg: port

	int sock, new_sock; // new_sock: new socket after accept new connection req from cli
	unsigned short server_port = atoi(argv[1]);
	if (server_port < 18000 || server_port > 18200)
	{
		perror("Please provide a valid port numebr");
		abort();
	}
	int max_connections = 5;
	int optval = 1;
	const int BACKLOG = 5;

	/* Vars for storing server socket info, and fill info in */
	struct sockaddr_in sin, addr;					 // addr for stroing client's addr
	socklen_t addr_len = sizeof(struct sockaddr_in); // provided in socket.h

	/* Initialize linked list for connected sockets */
	struct node head;
	struct node *current, *next;
	head.socket = -1;
	head.next = 0;

	/* Buffer for read & write */
	char *receive_buffer, *send_buffer;
	int BUF_LEN = 70000;
	int MSG_LEN = 70000;
	receive_buffer = (char *) malloc(BUF_LEN);
	send_buffer = (char *) malloc(BUF_LEN);

	if (!receive_buffer)
	{
		perror("Failure allocating buffer...");
		abort();
	}

	/* Create socket to listen for TCP connection req */
	if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		perror("Failed opening TCP socket");
		abort();
	}

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
	{
		perror("Failure setting TCP socket option");
		abort();
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;		   // But use PF_INET when calling socket()
	sin.sin_addr.s_addr = INADDR_ANY;  // IP addr for server socket, use random one for now
	sin.sin_port = htons(server_port); // socket takes big endian format

	/* Bind server socket to the addr */
	if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{ // (socket, sockadd*, socklen_t addrlen)
		perror("Failed binding socket to address");
		abort();
	}

	/* Put server in listening mode */
	if (listen(sock, BACKLOG) < 0)
	{
		perror("listen on socket failed");
		abort();
	}

	/* variables for select */
	fd_set read_set, write_set; //  file descriptor sets for select() -> a bit array.
	struct timeval time_out;
	int max, select_retval; // store the maximum sock

	while (1)
	{
		// printf("Begin of the new iteration ***\n");
		/* In Listen mode now -> Keep waiting for incoming connections,
		 - check for incoming data to receive,
		 - check for ready socket to send more data */

		/* Set up file descripters bit map for select() */
		FD_ZERO(&read_set);
		FD_ZERO(&write_set);

		FD_SET(sock, &read_set); // Turn on the bit sock for read_set
		max = sock;

		/* Put connected sockets into read & write sets to monitor */
		for (current = head.next; current; current = current->next)
		{
			FD_SET(current->socket, &read_set);

			if (current->pending_data)
			{
				/* there is data pending to be sent, monitor the socket
					in write_set so we know when it is ready to take more data */
				FD_SET(current->socket, &write_set);
			}

			if (current->socket > max)
			{
				max = current->socket;
			}
		}

		time_out.tv_usec = 100000; // 1/10 second timeout
		time_out.tv_sec = 0;

		/* invoke select, make sure to pass max+1 !!!
		   int select (int nfds, fd_set *read-fds, fd_set *write-fds, fd_set *except-fds, struct timeval *timeout)
		   select() blocks the calling process until there is activity on any of the specified sets of file descriptors, or until timeout  */
		// printf("Right before select\n");
		select_retval = select(max + 1, &read_set, &write_set, NULL, NULL);
		// printf("Here's value selecting is returing: %i\n", select_retval);
		
		if (select_retval < 0) {
			perror("select failed");
			abort();
		} else if (select_retval == 0) {
			continue;
		} else { // At least one fd is ready
			
			if (FD_ISSET(sock, &read_set)) 
			{ // check if server is ready
				/* there is an incoming connection, try to accept it */
				/* accept() returns a new socket for us to read & write */
				/* second param holds info of ip address of where it's coming from */
				new_sock = accept(sock, (struct sockaddr *)&addr, &addr_len);

				if (new_sock < 0)
				{
					perror("Failed accepting connection");
					abort();
				}

				if (fcntl(new_sock, F_SETFL, O_NONBLOCK) < 0)
				{
					perror("Failed setting socket to non-blocking");
					abort();
				}

				/*  the connection is made, everything is ready
					let's see who's connecting to us 	*/
				printf("Accepted connection. Client IP: %s\n", inet_ntoa(addr.sin_addr));

				/* Add this client connection to linked list */
				add(&head, new_sock, addr);
			}

			/* check other connected sockets, see if there is
			 anything to read or some socket is ready to send
			 more pending data */

			for (current = head.next; current; current = next)
			{	
				next = current->next;

				if (FD_ISSET(current->socket, &read_set))/*we just need to check the read set here, since the "send"
				is triggered by the "recv"*/
				{
					/* Client trying to sent data to us */
					int bytes_recv;

					bytes_recv = recv(current->socket, receive_buffer, BUF_LEN, 0);
					// printf("Bytes Receiverd: %d\n", bytes_recv);
					if (bytes_recv <= 0)//connection closed or error
					{
						/* Sth is wrong */
						if (bytes_recv == 0)
						{
							printf("Client closed connection. Client IP address is: %s\n", inet_ntoa(current->client_addr.sin_addr));
						}
						else
						{
							perror("error receiving from a client");
						}

						/* Conenction closed, clean up */
						close(current->socket);
						dump(&head, current->socket); // Clear from link list
					}
					else
					{
						// directly send the recive_buffer back
						ssize_t sendBytes = send(current->socket, receive_buffer, bytes_recv, MSG_DONTWAIT);
						printf("Send Bytes: %ld\n", sendBytes);
					}
				}
				
			}
		}
	}
	free(send_buffer);
	free(receive_buffer);
}