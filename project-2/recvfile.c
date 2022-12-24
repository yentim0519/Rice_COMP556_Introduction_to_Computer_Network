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
#include "util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>

#define MAXLINE 1024
#define DELAY_SECONDS_BEFORE_EXIT 10

int main(int argc, char **argv)
{ // command line takes in one arg: port

	int sock;
	unsigned short server_port = atoi(argv[2]);

	if (server_port < 18000 || server_port > 18200)
	{
		perror("Please provide a valid port number");
		abort();
	}

	int max_connections = 5;
	int optval = 1;
	const int BACKLOG = 5;

	/* Vars for storing server socket info, and fill info in */
	struct sockaddr_in sin;							 // addr for storing client's addr
	socklen_t addr_len = sizeof(struct sockaddr_in); // provided in socket.h

	memset(&sin, 0, sizeof(sin));

	/* Buffer for read & write */
	int buffer_len = 690;
	struct Packet recv_buffer[buffer_len];
	bool check_receive[buffer_len];
	memset(check_receive, 0, buffer_len);

	if (!recv_buffer || !check_receive)
	{
		perror("Failure allocating buffer...\n");
		abort();
	}

	/* Create socket to listen for UDP connection req */
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("Failed opening UDP socket\n");
		abort();
	}

	struct timeval read_timeout;
	read_timeout.tv_sec = 1;
	read_timeout.tv_usec = 0;

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &read_timeout, sizeof(read_timeout)) < 0)
	{
		perror("Failure setting UDP socket option\n");
		abort();
	}

	sin.sin_family = AF_INET;		   // But use PF_INET when calling socket()
	sin.sin_addr.s_addr = INADDR_ANY;  // IP addr for server socket, use random one for now
	sin.sin_port = htons(server_port); // socket takes big endian format

	/* Bind server socket to the addr */
	if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{ // (socket, sockadd*, socklen_t addrlen)
		perror("Failed binding socket to address\n");
		abort();
	}

	int recvBytes;
	int total_time_recv_nothing;
	FILE *filePtr = NULL;
	struct Packet recv_packet, ack_packet;
	bool getDummy = false;
	const int PACKET_SIZE = sizeof(recv_packet);
	char fileName[256];
	struct timeval curr_t, recv_start_t;
	bool filePtrOpen = false;
	bool start_to_recv_none = false;
	const int buffer_size = sizeof(recv_packet.data) * 690;

	int window_size = 5; // need to get from sender
	int start = 0;
	int end = start + window_size - 1;
	int round;

	while (1)
	{
		recvBytes = recvfrom(sock, &recv_packet, PACKET_SIZE,
							 MSG_DONTWAIT, (struct sockaddr *)&sin,
							 &addr_len);

		if (recvBytes == -1 && !start_to_recv_none)
		{
			gettimeofday(&recv_start_t, NULL);
			start_to_recv_none = true;
		}
		else if (recvBytes != -1)
		{
			start_to_recv_none = false;
		}

		// if receive nothing for too long, it means sender is closed
		gettimeofday(&curr_t, NULL);

		if (start_to_recv_none && get_time_diff(curr_t, recv_start_t) > (DELAY_SECONDS_BEFORE_EXIT * 1000000))
		{
			printf("-- End of file, breaking the receiving loop...\n");
			if (filePtr != NULL)
			{
				if (fclose(filePtr) == 0)
					printf("-- Successfully close file\n");
			}
			break;
		}

		if (getDummy && recvBytes == -1)
		{
			continue;
		}

		if (recvBytes != -1)
		{
			if (cksum((unsigned short *)&recv_packet, sizeof(struct Packet) / sizeof(unsigned short)) != 0)
			{
				printf("[recv corrupt packet]\n");
				continue;
			}
		}

		network_to_host(&recv_packet);
		// 1. ready to receive data
		// 2. wrong dummy packet
		// 3. ready to reset

		if (recvBytes != -1 && recv_packet.seq_num == 65535)
		{
			printf("-- Receiving dummy packet...\n");
			// reset our window when get dummy
			if (start > 689 && getDummy == true && recv_packet.bufferRound == round + 1)
			{
				printf("-- Reset window given new dummy...\n");
				// printf("buffer round: %d, packet round: %d\n", round, recv_packet.bufferRound);
				// memset(&recv_buffer, '0', buffer_len);
				memset(check_receive, 0, buffer_len);
				start = 0;
				end = start + window_size - 1;
				getDummy = false;
				round++;
			}
			else if (start == 0)
			{

				getDummy = true;
				strcpy(fileName, recv_packet.data);
				round = recv_packet.bufferRound;

				// Extract directory -> from C official document
				char *dirc, *dname;
				dirc = strdup(&fileName);
				dname = dirname(dirc);

				// Check if file path exists, otherwise create new one
				struct stat st = {0};
				if (stat(dname, &st) == -1)
				{
					printf("-- Creating new directory...\n");
					mkdir(dname, 0777);
				}

				if (!filePtrOpen)
				{
					printf("-- Opening file...\n");
					char *outputFileName = strcat(fileName, ".recv");
					filePtr = fopen(outputFileName, "w+");
					filePtrOpen = true;
				}

				ack_packet.seq_num = 65535;
				ack_packet.bufferRound = round;
				ack_packet.checksum = 0;

				host_to_network(&ack_packet);
				ack_packet.checksum = cksum((unsigned short *)&ack_packet, sizeof(struct Packet) / sizeof(unsigned short));

				sendto(sock, &ack_packet, PACKET_SIZE, 0, (const struct sockaddr *)&sin, sizeof(sin));
			}
		}

		if (!getDummy)
		{
			continue;
		}

		// ignore receive packet that is not in this round
		if (recv_packet.bufferRound != round)
		{
			// printf("packet not in this round\n");
			// printf("round: %d, packet round: %d\n", round, recv_packet.bufferRound);
			continue;
		}

		// check seq_num in window
		bool inWindow = (recv_packet.seq_num >= start && recv_packet.seq_num <= end);

		if (recv_packet.seq_num == 65535 || !inWindow)
		{
			printf("[recv data] %d (%d) IGNORED\n", (round * buffer_size) + recv_packet.seq_num * sizeof(recv_packet.data), recv_packet.data_size);
			// have to send the latest ack back when out of window or else it might stuck sometimes
		}

		// if receive packet seq num equals window start, move sliding window
		if (recv_packet.seq_num == start)
		{
			check_receive[start] = true;
			recv_buffer[recv_packet.seq_num] = recv_packet;
			check_receive[recv_packet.seq_num] = 1;

			//  printf("[recv data] %d (%d) ACCEPTED(in-order)\n", recv_packet.seq_num * sizeof(recv_packet.data), recv_packet.data_size);
			printf("[recv data] %d (%d) ACCEPTED(in-order)\n", (round * buffer_size) + recv_packet.seq_num * sizeof(recv_packet.data), recv_packet.data_size);

			while (start < 690 && check_receive[start])
			{
				fwrite(recv_buffer[start].data, 1, recv_buffer[start].data_size, filePtr);
				// printf("Writing data packet seq: %d\n", start);
				start++;
				end++;
			}
		}

		// accept packet in window but not at the start place of window
		if (inWindow && !check_receive[recv_packet.seq_num])
		{
			// printf("[recv data] %d (%d) ACCEPTED(out-of-order)\n", recv_packet.seq_num * sizeof(recv_packet.data), recv_packet.data_size);
			printf("[recv data] %d (%d) ACCEPTED(out-of-order)\n", (round * buffer_size) + recv_packet.seq_num * sizeof(recv_packet.data), recv_packet.data_size);
			recv_buffer[recv_packet.seq_num] = recv_packet;
			check_receive[recv_packet.seq_num] = 1;
		}

		// send ack(start - 1)
		ack_packet.seq_num = start - 1;
		// printf("-- send ack %hu\n", ack_packet.seq_num);
		ack_packet.bufferRound = round;
		ack_packet.checksum = 0; // checksum

		host_to_network(&ack_packet);
		ack_packet.checksum = cksum((unsigned short *)&ack_packet, sizeof(struct Packet) / sizeof(unsigned short));
		sendto(sock, &ack_packet, PACKET_SIZE, 0, (const struct sockaddr *)&sin, sizeof(sin));
	}

	printf("[completed]\n");
	close(sock);
	exit(0);
}