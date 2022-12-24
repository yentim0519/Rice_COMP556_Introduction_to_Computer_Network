#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "util.h"
#define MAXLINE 1024

int main(int argc, char **argv)
{
    // intialize arguments (Still need to edit)
    if (argc < 5 || argc > 5)
    {
        printf("The command format is incorrect. \n");
        return 0;
    }

    // <recv_host>:<recv_port>
    char *recv = argv[2];
    char *recv_host = strtok(recv, ":");
    unsigned short recv_port;
    while (recv != NULL)
    {
        recv_port = atoi(recv);
        recv = strtok(NULL, ":");
    }

    // <subdir>/<filename>
    char *filePath = argv[4];
    int path_len = strlen(filePath);

    struct Packet packet;

    // initialize variables
    FILE *filePtr = fopen(filePath, "r");
    fseek(filePtr, 0, SEEK_SET);
    struct timeval timeStruct;
    int sock; // socket to used for connection to the server

    struct addrinfo *getaddrinfo_result, hints;
    unsigned int recv_addr; // ip adress
    struct sockaddr_in sin;
    socklen_t addr_len = sizeof(struct sockaddr_in); // provided in socket.h

    // Convert server domain name to IP
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;

    if (getaddrinfo(recv_host, NULL, &hints, &getaddrinfo_result) == 0)
    {
        recv_addr = (unsigned int)((struct sockaddr_in *)(getaddrinfo_result->ai_addr))->sin_addr.s_addr; // ip adress
    }
    else
    {
        fprintf(stderr, "Could not get the IP address of %s\n", recv_host);
    }

    // Create a socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("failed to open a UDP socket\n");
        abort();
    }

    // set socket options
    struct timeval read_timeout;
    read_timeout.tv_sec = 1;
    read_timeout.tv_usec = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));

    // Fill in servers address
    memset(&sin, 0, sizeof(struct sockaddr_in));

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = recv_addr;
    sin.sin_port = htons(recv_port);

    // Open and check file size
    struct stat info;
    stat(filePath, &info);
    size_t fileSize = info.st_size;
    size_t total_bytes_read;
    size_t bytes_read;
    printf("File Size: %zu\n", fileSize);

    // Read file to buffer in parts
    bool stopRead = false;
    const int PACKET_SIZE = sizeof(packet);
    int data_size = sizeof(packet.data);
    const int buffer_size = data_size * 690;
    char file_buffer[buffer_size]; // data_size * 690
    int round = -1;

    while (!stopRead)
    {   
        // Read file in parts and fill up the buffer every time
        bytes_read = fread(file_buffer, 1, buffer_size, filePtr);
        round++;

        // printf("new Round of Buffer\n");
        total_bytes_read += bytes_read;
        
        if (total_bytes_read == fileSize)
        {
            stopRead = true;
            // printf("This is the last round\n");
        }

        // Initialize variables
        const int TIMEOUT = 10000; // 0.01 sec
        const int window_size = 5;
        int ack_cnt = 0, recv_bytes = 0;
        size_t ack_size = 0;
        struct timeval send_t, curr_t;
        int buffer_length = 690;

        if (stopRead)
        {
            // last iteration, buffer_length can be shrinked
            buffer_length = bytes_read / data_size + (bytes_read % data_size != 0);
        }

        // buffers
        gettimeofday(&curr_t, NULL);
        struct timeval window_sendtime[buffer_length]; // 1000960 / 1472, but it could also be 2144/1472

        int i;
        for (i = 0; i < buffer_length; i++)
        { // init window_sendtime with current time
            window_sendtime[i] = curr_t;
        }

        struct Packet recv_packet; // use to get recv packet

        int start = 0; // everything before start idx don't need to be send
        int cur_end = window_size - 1;
        int new_end = window_size - 1;

        if (window_size > buffer_length - 1)
        {
            cur_end = new_end = buffer_length - 1;
        }

        int cur_ack = -1;
        int latest_ack = -1;
        int last_packet_size = data_size;

        if (bytes_read % data_size != 0)
        { // we only have to change the last_packet_size when bytes_read is not the multple of data_size
            last_packet_size = bytes_read - data_size * (bytes_read / data_size);
        }

        // send dummy packet
        int get_dummy_ack = -1;
        int dummy_bytes_sent;

        struct Packet dummy_packet;
        dummy_packet.data_size = path_len;
        dummy_packet.seq_num = -1;
        strcpy(dummy_packet.data, filePath); // put file path in data
        dummy_packet.bufferRound = round;

        dummy_packet.checksum = 0;
        host_to_network(&dummy_packet);
        dummy_packet.checksum = cksum((unsigned short *)&dummy_packet, sizeof(struct Packet) / sizeof(unsigned short));


        // send dummy packet until recv dummy ack
        bool start_round = false;
        do
        {
            dummy_bytes_sent = sendto(sock, &dummy_packet, PACKET_SIZE, 0, (const struct sockaddr *)&sin, sizeof(sin));
            get_dummy_ack = recvfrom(sock, &recv_packet, PACKET_SIZE, MSG_DONTWAIT, (struct sockaddr *)&sin, &addr_len);

            if (get_dummy_ack != -1)
            {
                if (cksum((unsigned short *)&recv_packet, sizeof(struct Packet) / sizeof(unsigned short)) != 0)
                {
                    continue;
                }
                network_to_host(&recv_packet);
                if (recv_packet.seq_num == 65535)
                {
                    start_round = true;
                }
            }

        } while (get_dummy_ack == -1 || recv_packet.seq_num != 65535); // have to make sure it gets the right ack to let it through

        // start doing sliding window
        while (cur_ack == 65535 || cur_ack != buffer_length - 1)
        { // Keep trying to send as long as the last ack hasn't been received
            // Receive the latest ack
            recv_bytes = recvfrom(sock, &recv_packet, PACKET_SIZE,
                                  MSG_DONTWAIT, (struct sockaddr *)&sin,
                                  &addr_len);

            // printf("start: %d, cur_end: %d\n", start, cur_end);

            if (recv_bytes != -1)
            {
                if (cksum((unsigned short *)&recv_packet, sizeof(struct Packet) / sizeof(unsigned short)) != 0)
                {
                    continue;
                }
                network_to_host(&recv_packet);
            }
            
            // ignore packets that are not in this buffer round
            if (recv_packet.bufferRound != round) {
                continue;
            }

            cur_ack = recv_packet.seq_num;
            // printf("cur_ack: %d\n", cur_ack);
            // printf("round: %d, ack round: %d\n", round, recv_packet.bufferRound);


            if (recv_packet.seq_num != 65535)
            {
                if (recv_packet.seq_num == buffer_length - 1)
                {
                    break;
                }
            }

            gettimeofday(&curr_t, NULL);

            // didn't recv anything
            if (cur_end == buffer_length - 1 || (recv_bytes == -1 || recv_packet.seq_num == latest_ack) && (get_time_diff(curr_t, window_sendtime[latest_ack + 1]) > TIMEOUT))
            {
                // Didn't receive any ack  -> ack is either lost, or I didn't send anything to the receiver
                // Anyway, resent all packets in window
                int i;
                for (i = start; i <= cur_end; i++)
                {

                    int real_data_size = data_size;

                    if (i == buffer_length - 1) // last packet might not have size 1470
                    {
                        real_data_size = last_packet_size;
                    }

                    struct Packet packet;
                    packet.data_size = real_data_size;
                    packet.checksum = 0; // checksum
                    packet.seq_num = i;
                    packet.bufferRound = round;
                    memcpy(packet.data, file_buffer + i * data_size, real_data_size);

                    // Loggin info
                    printf("[send data] %d (%d)\n", (round * buffer_size) + packet.seq_num * sizeof(packet.data), packet.data_size);

                    // Switch to network ordering
                    host_to_network(&packet);
                    packet.checksum = cksum((unsigned short *)&packet, sizeof(struct Packet) / sizeof(unsigned short));
                    window_sendtime[i] = send_t;

                    int send_bytes = sendto(sock, &packet, PACKET_SIZE, 0,
                                            (const struct sockaddr *)&sin, sizeof(sin));
                }
                if (cur_ack > buffer_length - 1)
                {
                    continue;
                }
            }
            else
            {
                if ((cur_ack == 65535 && (latest_ack != 65535 || latest_ack < (buffer_length - 1))) || (cur_ack < start || cur_ack > cur_end))
                {
                    continue;
                }
                else
                {
                    // printf("latest ack in else: %d\n", latest_ack);
                    if (cur_ack == 65535)
                    {
                        cur_ack = -1;
                    }

                    latest_ack = cur_ack;
                    start = latest_ack + 1;

                    new_end = start + window_size - 1; // make sure new_end to surpass buffer limit

                    if (new_end > buffer_length - 1)
                    {
                        new_end = buffer_length - 1;
                    }

                    if (cur_end == new_end)
                    {
                        continue; // if this happens, we don't have to resend packets between start and end
                    }

                    // send file from cur_end+1 to new_end
                    int i;
                    for (i = cur_end + 1; i <= new_end; i++)
                    {
                        // TODO: Not sure about the formatting of the packet

                        int real_data_size = data_size;
                        if (i == buffer_length - 1) // last packet might not have size 1470
                        {
                            real_data_size = last_packet_size;
                        }

                        struct Packet packet;
                        packet.data_size = real_data_size;
                        packet.seq_num = i;
                        packet.checksum = 0; // checksum
                        packet.bufferRound = round;
                        memcpy(packet.data, file_buffer + i * data_size, real_data_size);

                        printf("[send data] %d (%d)\n", (round * buffer_size) + packet.seq_num * sizeof(packet.data), packet.data_size);

                        host_to_network(&packet);
                        packet.checksum = cksum((unsigned short *)&packet, sizeof(struct Packet) / sizeof(unsigned short));

                        window_sendtime[i] = send_t;
                        int send_bytes = sendto(sock, &packet, PACKET_SIZE, 0, (const struct sockaddr *)&sin, sizeof(sin));
                    }

                    cur_end = new_end;
                }
            }
        }
    }

    // close file and socket
    // printf("total bytes read: %zu, file size: %zu\n", total_bytes_read, fileSize);
    fclose(filePtr);
    close(sock);
    printf("[completed]\n");
    exit(0);
}