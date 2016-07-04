#ifndef TRANSFER_H
#define TRANSFER_H

#include <stdint.h>

#define PSTAT_FREQ 32
#define PSTAT_NAMESZ 80

#define MODE_SERVER 1
#define MODE_CLIENT 2
#define MODE_FORCE 4

struct cfg {
	unsigned mode;
	uint16_t port;
	const char *address;
	char **files;
};

extern struct cfg cfg;

#endif
