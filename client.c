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
static uint64_t bcount;
static uint64_t skip = 0;

static int recover(void)
{
	struct npkg pkg;
	int ns;
	memset(&pkg, 0, sizeof pkg);
	pkginit(&pkg, NT_CHK);
	ns = pkgout(&pkg, sock);
	nschk(ns);
	puts("receiving checksums...");
	uint64_t i, j, pos;
	bcount = (fmapsz - 1) / N_CHUNKSZ + 1;
	uint32_t sum, sump;
	size_t n;
	const uint8_t *map = fmap;
	for (i = 0; i < bcount; ++i) {
		ns = pkgin(&pkg, sock);
		nschk(ns);
		if (pkg.type != NT_CHK) {
			fputs("communication error: NT_CHK expected\n", stderr);
			return 1;
		}
		j = be64toh(pkg.data.chk.index);
		if ((pos = j * N_CHUNKSZ) <= fmapsz)
			n = N_CHUNKSZ;
		else
			n = fmapsz - pos;
		sum = crc32(0, map + pos, n);
		pkginit(&pkg, NT_ACK);
		sump = be32toh(pkg.data.chk.sum);
		if (sum != sump) {
			char poff[32];
			strtosi(poff, sizeof poff, j * N_CHUNKSZ, 3);
			printf("found diff at %s in block %lu\n", poff, j);
			skip = j;
			pkginit(&pkg, NT_CHK);
		}
		ns = pkgout(&pkg, sock);
		nschk(ns);
		if (pkg.type == NT_CHK)
			break;
	}
	return 0;
}

static int handle(void)
{
	struct npkg pkg;
	int dirty = 0;
	char nm[PSTAT_NAMESZ], *data;
	char pdone[32], ptot[32], eta[80];
	unsigned st_timer;
	uint64_t index, f_p, max, datasz;
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
			fprintf(stderr, "network error: code %u\n", ns);
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
	char *name = pkg.data.stat.name;
	int exist = 0;
	struct stat st;
	if (!stat(name, &st))
		exist = 1;
	if (!(cfg.mode & (MODE_FORCE | MODE_RECOVER)))
		mode |= O_EXCL;
	file = open(name, mode, 0664);
	if (file == -1) {
		perror(name);
		pkginit(&pkg, NT_ERR);
		goto fail;
	}
	int resize = 1, chk = 0;
	if (exist && (cfg.mode & MODE_RECOVER)) {
		if ((resize = st.st_size != size) && !(cfg.mode & MODE_FORCE)) {
			fprintf(
				stderr,
				"Can't recover transfer,"
				"file size mismatch: have %zu, but got %zu\n",
				st.st_size, size
			);
			pkginit(&pkg, NT_ERR);
			goto fail;
		}
		chk = 1;
	}
	if (resize && posix_fallocate(file, 0, size)) {
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
	int prot = PROT_WRITE;
	if (exist && (cfg.mode & MODE_RECOVER))
		prot |= PROT_READ;
	fmap = mmap(NULL, fmapsz = size, prot, MAP_SHARED, file, 0);
	if (fmap == MAP_FAILED) {
		perror("mmap");
		pkginit(&pkg, NT_ERR);
		goto fail;
	}
	if (chk) {
		if (!recover())
			goto restore;
		fputs("Can't recover transfer\n", stderr);
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
	skip = 0;
	/* we are ready to receive some data */
restore:
	data = fmap;
	st_timer = 0;
	bcount = (size - 1) / N_CHUNKSZ + 1;
	size_t p_now;
	strencpyz(nm, name, sizeof nm, "...");
	strtosi(ptot, sizeof ptot, size, 3);
	printf("\033[s");
	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	*eta = '\0';
	for (f_p = 0, max = bcount, bcount -= skip; bcount; --bcount) {
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
