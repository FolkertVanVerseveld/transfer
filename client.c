#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include "client.h"
#include "dbg.h"
#include "string.h"
#include "net.h"
#include "transfer.h"

static int sock = -1, file = -1;
static void *fmap = MAP_FAILED;
static size_t fmapsz;
static struct sockaddr_in server;

static int handle(void)
{
	struct npkg pkg;
	int dirty = 0;
loop:
	memset(&pkg, 0, sizeof pkg);
	int ns = pkgin(&pkg, sock);
	if (ns != NS_OK) {
		switch (ns) {
		case NS_LEFT:
			if (dirty)
				puts("transfers completed");
			else
				fputs("other left\n", stderr);
			return 0;
		default:
			fprintf(stderr,"network error: code %u\n", ns);
			return ns;
		}
	}
	nschk(ns);
	if (pkg.type != NT_STAT) {
		fputs("communication error: NT_STAT expected\n", stderr);
		return 1;
	}
	uint64_t size = be64toh(pkg.data.stat.size);
	pkg.data.stat.name[N_NAMESZ - 1] = '\0';
	printf(
		"server wants to send \"%s\"\n"
		"filesize: %lu bytes\n",
		pkg.data.stat.name, size
	);
	// TODO ask user confirmation
	/*
	XXX O_RDONLY does not work for PROT_WRITE on x86, see also:
	http://stackoverflow.com/questions/33314745/in-c-mmap-the-file-for-write-permission-denied-linux
	*/
	int mode = O_CREAT | O_RDWR;
	if (!(cfg.mode & MODE_FORCE))
		mode |= O_EXCL;
	char *name = pkg.data.stat.name;
	file = open(name, mode, 0664);
	if (file == -1) {
		perror(pkg.data.stat.name);
		pkginit(&pkg, NT_ERR);
		goto fail;
	}
	if (posix_fallocate(file, 0, size)) {
		perror("preallocate failed");
		fputs(
			"This filesystem does not have enough disk space\n"
			"or it does not support preallocation.\n"
			"E.g. ext4 supports preallocation, ext3 and FAT do not.\n",
			stderr
		);
		pkginit(&pkg, NT_ERR);
		goto fail;
	}
	fmap = mmap(NULL, fmapsz = size, PROT_WRITE, MAP_SHARED, file, 0);
	if (fmap == MAP_FAILED) {
		perror("mmap");
		pkginit(&pkg, NT_ERR);
		goto fail;
	}
	pkginit(&pkg, NT_ACK);
	pkg.data.ack = NT_STAT;
fail:
	ns = pkgout(&pkg, sock);
	nschk(ns);
	if (pkg.type == NT_ERR)
		return 1;
	/* we are ready to receive some data */
	char nm[PSTAT_NAMESZ], *data = fmap;
	char pdone[32], ptot[32], eta[80];
	unsigned st_timer = 0;
	uint64_t index, f_p, max, datasz, bcount = (size - 1) / N_CHUNKSZ + 1;
	size_t p_now;
	strencpyz(nm, name, sizeof nm, "...");
	strtosi(ptot, sizeof ptot, size, 3);
	printf("\033[s");
	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	*eta = '\0';
	for (f_p = 0, max = bcount; bcount; --bcount) {
		ns = pkgin(&pkg, sock);
		nschk(ns);
		if (pkg.type != NT_FBLK) {
			fputs("communication error: NT_FBLK expected\n", stderr);
			return 1;
		}
		index = be64toh(pkg.data.chunk.index);
		if (index > max) {
			fprintf(stderr, "bad index: %lu\n", index);
			return 1;
		}
		memcpy(
			&data[index * N_CHUNKSZ], pkg.data.chunk.data,
			datasz = index + 1 != max ? N_CHUNKSZ : size % N_CHUNKSZ
		);
		if (st_timer) {
			--st_timer;
			++f_p;
			goto new_eta;
		}
		st_timer = PSTAT_FREQ;
		strtosi(pdone, sizeof pdone, f_p * N_CHUNKSZ + datasz, 3);
		++f_p;
	new_eta:
		clock_gettime(CLOCK_MONOTONIC, &now);
		p_now = f_p * N_CHUNKSZ + datasz;
		streta(eta, sizeof eta, start, now, p_now, size);
		printf(
			"\033[u%s, %s/%s (%.2f%%) %s\033[K",
			nm, pdone, ptot,
			p_now * 100.0f / size, eta
		);
	}
	printf("\033[u%s, %s/%s (100%%) %s\033[K\n", nm, ptot, ptot, eta);
	dirty = 1;
	goto loop;
}

int cmain(void)
{
	int domain = AF_INET, type = SOCK_STREAM, prot = IPPROTO_TCP;
	int ret = 1;
	sock = socket(domain, type, prot);
	if (sock == -1) {
		perror("socket");
		goto fail;
	}
	server.sin_addr.s_addr = inet_addr(cfg.address);
	server.sin_family = domain;
	server.sin_port = htobe16(cfg.port);
	if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
		perror("connect");
		goto fail;
	}
	ret = handle();
fail:
	if (sock != -1)
		close(sock);
	if (fmap != MAP_FAILED)
		munmap(fmap, fmapsz);
	if (file != -1)
		close(file);
	return ret;
}
