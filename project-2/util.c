#include "util.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdbool.h>

/* current t1 - previous t2 */
long int get_time_diff(struct timeval t1, struct timeval t2)
{
    long int diff = (t1.tv_sec * 1000000 + t1.tv_usec) - (t2.tv_sec * 1000000 + t2.tv_usec);
    return diff;
}

void host_to_network(struct Packet *p)
{
    p->data_size = htons(p->data_size);
    p->seq_num = htons(p->seq_num);
    // p -> checksum = htons(p -> checksum);
}

void network_to_host(struct Packet *p)
{
    p->data_size = ntohs(p->data_size);
    p->seq_num = ntohs(p->seq_num);
    // p -> checksum = ntohs(p -> checksum);
}

unsigned short cksum(unsigned short *buf, int count)
{
    register unsigned long sum = 0;
    while (count--)
    {
        sum += *buf++;
        // carry occurred, so wrap around
        if (sum & 0XFFFF0000)
        {
            sum &= 0XFFFF;
            sum++;
        }
    }
    return ~(sum & 0XFFFF);
}