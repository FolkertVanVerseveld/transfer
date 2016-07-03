#ifndef NET_H
#define NET_H

#include <stddef.h>
#include <stdint.h>
#include <endian.h>

#define PORT 25659
#define BACKLOG 1

#define NS_OK 0
#define NS_LEFT 1
#define NS_PROT 2
#define NS_ERR 3

#define NT_ACK 0
#define NT_ERR 1
#define NT_STAT 2
#define NT_FBLK 3
#define NT_MAX 3

#define N_HDRSZ 8
#define N_NAMESZ 256
#define N_CHUNKSZ 4096

struct npkg {
	uint16_t length;
	uint8_t prot, type;
	uint8_t res[4]; // currently used for alignment
	union {
		struct {
			uint64_t size;
			char name[N_NAMESZ];
		} stat;
		struct {
			uint64_t index;
			char data[N_CHUNKSZ];
		} chunk;
		uint8_t ack;
	} data;
};

void pkginit(struct npkg *pkg, uint8_t type);
int pkgout(struct npkg *pkg, int fd);
int pkgin(struct npkg *pkg, int fd);

int noclaim(int fd);
int hack(int fd);

#define nschk(x) \
	if (x != NS_OK) {\
		switch (x) {\
		case NS_LEFT:fputs("other left\n",stderr);return 1;\
		default:fprintf(stderr,"network error: code %u\n",x);return x;\
		}\
	}

#endif
