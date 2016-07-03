#include "net.h"
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

static const uint16_t nt_ltbl[NT_MAX + 1] = {
	[NT_ACK] = 1,
	[NT_ERR] = 1,
	[NT_STAT] = sizeof(uint64_t) + N_NAMESZ,
	[NT_FBLK] = sizeof(uint64_t) + N_CHUNKSZ,
};

int pkgout(struct npkg *pkg, int fd)
{
	uint16_t length;
	ssize_t n;
	assert(pkg->type <= NT_MAX);
	length = nt_ltbl[pkg->type] + N_HDRSZ;
	pkg->length = htobe16(length);
	n = send(fd, pkg, length, 0);
	if (!n) return NS_LEFT;
	return n == length ? NS_OK : NS_ERR;
}

int pkgin(struct npkg *pkg, int fd)
{
	ssize_t n;
	uint16_t length, t_length;
	n = recv(fd, pkg, N_HDRSZ, 0);
	if (!n) return NS_LEFT;
	if (n == -1 || n != N_HDRSZ) return NS_ERR;
	length = be16toh(pkg->length);
	if (length < 4) {
		fprintf(stderr, "impossibru: length=%u\n", length);
		return NS_ERR;
	}
	if (pkg->type > NT_MAX) {
		fprintf(stderr, "bad type: type=%u\n", pkg->type);
		return NS_ERR;
	}
	t_length = nt_ltbl[pkg->type];
	n = recv(fd, &pkg->data, t_length, 0);
	length -= N_HDRSZ;
	if (length - N_HDRSZ > t_length)
		return NS_ERR;
	return NS_OK;
}

void pkginit(struct npkg *pkg, uint8_t type)
{
	assert(type <= NT_MAX);
	pkg->type = type;
	pkg->prot = 0;
	pkg->length = htobe16(nt_ltbl[type] + N_HDRSZ);
}

int noclaim(int fd)
{
	register int level, optname;
	int optval;
	level = SOL_SOCKET;
	optname = SO_REUSEADDR;
	optval = 1;
	return setsockopt(fd, level, optname, &optval, sizeof(int));
}
