#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>


// takes 4 command line param: hostname, server_port, size, count
int main(int argc, char **argv)
{   
    FILE *fp;
    struct timeval timeStruct;
    int sock; // socket to used for connection to the server

    // intialize parameters
    char* hostname = argv[1]; 
    unsigned short server_port = atoi(argv[2]);
    unsigned short size = atoi(argv[3]);
    unsigned short count = atoi(argv[4]);
   
    
    struct addrinfo *getaddrinfo_result, hints;
    unsigned int server_addr; // ip adress

    // Convert server domain name to IP
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;

    if (getaddrinfo(hostname, NULL, &hints, &getaddrinfo_result) == 0) {
        server_addr = (unsigned int)((struct sockaddr_in *)(getaddrinfo_result->ai_addr))->sin_addr.s_addr; // ip adress
    } else {
        fprintf(stderr, "Could not get the IP address of %s", hostname);
    }

    // Set up receiving/sending buffer
    char receive_buffer[18 + 65535], send_buffer[18 + 65535];

    printf("Size: %hu\n", size);
    if (atoi(argv[3]) > 65535 || atoi(argv[3]) < 0) {
        perror("Msg Size cannot be less than 0 bytes or more than 65535 bytes");
        abort();
    }

    // Create a new socket
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("failed to open a TCP socket");
        abort();
    }

    // Fill in servers address
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(struct sockaddr_in));

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = server_addr;
    sin.sin_port = htons(server_port);

    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("connect to server failed");
        abort();
    }

    // printf("I am connected\n");

    // format the ping message
    send_buffer[0] = 18 + size;
    *(long *)(send_buffer + 2) = htonl(timeStruct.tv_sec); // the casting is necessary to avoid compile-time errors
    *(long *)(send_buffer + 10) = htonl(timeStruct.tv_usec);
    memset(send_buffer + 18, 'a', size);
    
    int i; 
    long total_latency = 0;
    for (i = 0; i < count; i++) 
    { // this the loop for each ping message
        gettimeofday(&timeStruct, NULL); // timezone specified to NULL (see man)
        long cli_sent_time = (long)timeStruct.tv_sec * 1000000 + (long)timeStruct.tv_usec;
        // printf("--Client sent time: %ld\n", cli_sent_time);

        // update the timeStruct
        ssize_t sendBytes = send(sock, send_buffer, 18 + size, 0); /* send_buffer+bytes_sent we need to make this addition to make sure, we do not send the same data over and over */
        printf("Send Bytes: %ld\n", sendBytes);

        // printf("Trying to receive...\n");
        int recvSize = recv(sock, receive_buffer, 18 + size, 0);
        // printf("Recv Size: %d\n", recvSize);

        gettimeofday(&timeStruct, NULL);
        total_latency += ((long)timeStruct.tv_sec * 1000000 + (long)timeStruct.tv_usec - cli_sent_time);
        // printf("Latency: %ld\n", latency);
    }

    printf("Message Packet: %d\n", size);
    // printf("Pong Message: %s\n", receive_buffer + 18);
    // printf("Avg Latency: %0.3f\n\n", (float) total_latency / count);

    // Write to txt for data analysis
    // fp = fopen("./latency.txt", "a");
    // fseek(fp, 0, SEEK_END);
    // fprintf(fp, "\n%d, %ld", size, (total_latency / count));
    // fclose(fp);

    close(sock);
    return 0;
}