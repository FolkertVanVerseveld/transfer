#include "net.h"
#include "dbg.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
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
	printf("out: type=%u, length=%hu\n", pkg->type, length);
	while (length) {
		n = send(fd, pkg, length, 0);
		if (!n) return NS_LEFT;
		if (n < 0) return NS_ERR;
		length -= n;
	}
	return NS_OK;
}

static char pb_data[UINT16_MAX];
static uint16_t pb_size = 0;

ssize_t pkgread(int fd, void *buf, uint16_t n)
{
	ssize_t length;
	size_t need;
	if (pb_size) {
		// TODO unit test this
		uint16_t off;
		// use remaining data
		printf("buffered data size=%hu\n", pb_size);
		// if everything is buffered already
		if (pb_size >= n)
			goto copy;
		// we need to copy all buffered data and
		// wait for the next stuff to arrive
		memcpy(buf, pb_data, off = pb_size);
		pb_size = 0;
		for (need = n - pb_size; need; need -= length, pb_size += length) {
			printf("need %lu more byte\n", need);
			length = recv(fd, &pb_data[pb_size], need, 0);
			printf("got %zd bytes\n", length);
			if (length <= 0) return length;
		}
		memcpy((char*)buf + off, pb_data, pb_size);
		if (pb_size > n)
			memmove(pb_data, &pb_data[pb_size], UINT16_MAX - pb_size);
		pb_size -= n;
		return n;
	}
	pb_size = 0;
	for (need = n; need; need -= length, pb_size += length) {
		printf("need %lu more bytes\n", need);
		length = recv(fd, &pb_data[pb_size], need, 0);
		printf("got %zd bytes\n", length);
		if (length <= 0) return length;
	}
	if (pb_size == n)
		puts("buffer emptied");
	else
		printf("buffer size=%hu\n", pb_size - n);
copy:
	memcpy(buf, pb_data, n);
	if (pb_size > n)
		memmove(pb_data, &pb_data[pb_size], UINT16_MAX - pb_size);
	pb_size -= n;
	return n;
}

int pkgin(struct npkg *pkg, int fd)
{
#if 1
	ssize_t n;
	uint16_t length, t_length;
	n = pkgread(fd, pkg, N_HDRSZ);
	if (!n) return NS_LEFT;
	if (n == -1 || n != N_HDRSZ) return NS_ERR;
	length = be16toh(pkg->length);
	if (length < 4) {
		fprintf(stderr, "impossibru: length=%u\n", length);
		return NS_ERR;
	}
	printf("in: length=%hu\n", length);
	if (length > sizeof(struct npkg)) {
		fprintf(stderr, "impossibru: length=%u\n", length);
		length = sizeof(struct npkg);
	}
	if (pkg->type > NT_MAX) {
		fprintf(stderr, "bad type: type=%u\n", pkg->type);
		return NS_ERR;
	}
	t_length = nt_ltbl[pkg->type];
	printf("in: type=%u, t_length=%hu\n", pkg->type, t_length);
	n = pkgread(fd, &pkg->data, t_length);
	if (n == -1 || n != t_length) {
		fprintf(stderr, "impossibru: n=%zu\n", n);
		return NS_ERR;
	}
	length -= N_HDRSZ;
	if (length - N_HDRSZ > t_length)
		return NS_ERR;
	return NS_OK;
#else
	ssize_t n;
	uint16_t length, t_length;
	for (length = 0; length < N_HDRSZ;) {
		n = pkgread(fd, pkg, N_HDRSZ - length);
		if (!n) return NS_LEFT;
		if (n < 0 || n > UINT16_MAX)
			return NS_ERR;
		length += n;
	}
	t_length = nt_ltbl[pkg->type];
	printf("in: type=%u, length=%hu\n", pkg->type, t_length + N_HDRSZ);
	for (length = 0; length < t_length;) {
		n = pkgread(fd, &pkg->data, t_length - length);
		if (!n) return NS_LEFT;
		if (n < 0 || n > UINT16_MAX)
			return NS_ERR;
		length += n;
	}
	return NS_OK;
#endif
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

int hack(int fd)
{
	int val = 1;
	return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(int));
}
