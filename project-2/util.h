#ifndef HEADERFILE_H
#define HEADERFILE_H
#include <time.h>
#include <arpa/inet.h>

typedef struct Packet
{
    unsigned short data_size;
    unsigned short seq_num;
    unsigned short bufferRound;
    unsigned short checksum;
    char data[1472 - sizeof(unsigned short) * 4];
};

long int get_time_diff(struct timeval t1, struct timeval t2);
void host_to_network(struct Packet *p);
void network_to_host(struct Packet *p);
unsigned short cksum(unsigned short *buf, int count);

#endif