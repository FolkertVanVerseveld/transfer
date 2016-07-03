#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "client.h"
#include "dbg.h"
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
	pkginit(&pkg, NT_ACK);
	pkg.data.ack = NT_STAT;
	/*
	XXX O_RDONLY does not work for PROT_WRITE on x86, see also:
	http://stackoverflow.com/questions/33314745/in-c-mmap-the-file-for-write-permission-denied-linux
	*/
	int mode = O_CREAT | O_RDWR;
	if (!(cfg.mode & MODE_FORCE))
		mode |= O_EXCL;
	file = open(pkg.data.stat.name, mode, 0664);
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
fail:
	ns = pkgout(&pkg, sock);
	nschk(ns);
	if (pkg.type == NT_ERR)
		return 1;
	/* we are ready to receive some data */
	char *data = fmap;
	uint64_t index, max, bcount = (size - 1) / N_CHUNKSZ + 1;
	for (max = bcount; bcount; --bcount) {
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
			index + 1 != max ? N_CHUNKSZ : size % N_CHUNKSZ
		);
	}
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
