#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include "dbg.h"
#include "server.h"
#include "string.h"
#include "net.h"
#include "transfer.h"

static int ssock = -1, client = -1, file = -1;
static void *fmap = MAP_FAILED;
static size_t fmapsz;
static struct sockaddr_in sa, ca;
static socklen_t cl;

static int handle(void)
{
	struct npkg pkg;
	struct stat st;
	int abort = 0, ret = 0, ns;
	char **fname = cfg.files;
	while (!abort && *fname) {
		fmap = MAP_FAILED;
		file = open(*fname, O_RDONLY);
		if (file == -1) {
			perror(*fname);
			goto skip;
		}
		if (fstat(file, &st)) {
			perror("fstat");
			goto skip;
		}
		fmap = mmap(NULL, fmapsz = st.st_size, PROT_READ, MAP_PRIVATE, file, 0);
		if (fmap == MAP_FAILED) {
			perror("mmap");
			goto skip;
		}
		printf("preparing to send \"%s\"...\n", *fname);
		memset(&pkg, 0, sizeof pkg);
		pkginit(&pkg, NT_STAT);
		*fname = basename(*fname);
		strncpy(pkg.data.stat.name, *fname, N_NAMESZ - 1);
		pkg.data.stat.name[N_NAMESZ - 1] = '\0';
		/* use tempvars to force correct size
		otherwise it may break when applied
		directly to htobe64 */
		uint64_t f_i, size = st.st_size;
		pkg.data.stat.size = htobe64(size);
		ns = pkgout(&pkg, client);
		nschk(ns);
		ns = pkgin(&pkg, client);
		nschk(ns);
		if (pkg.type != NT_ACK) {
			if (pkg.type == NT_ERR) {
				fputs(
					"transfer failed\n"
					"client dropped\n",
					stderr
				);
				abort = ret = 1;
				goto skip;
			}
			fputs("rejected by client\n", stderr);
			ret = 1;
			goto skip;
		}
		printf("sending \"%s\"...\n", *fname);
		char nm[PSTAT_NAMESZ], *data = fmap;
		char pdone[32], ptot[32], eta[80];
		unsigned st_timer = 0;
		uint64_t index, datasz;
		size_t p_now;
		strencpyz(nm, *fname, sizeof nm, "...");
		strtosi(ptot, sizeof ptot, size, 3);
		printf("\033[s");
		struct timespec start, now;
		clock_gettime(CLOCK_MONOTONIC, &start);
		*eta = '\0';
		for (f_i = 0; f_i < size; f_i += datasz) {
			pkginit(&pkg, NT_FBLK);
			/* just in case packets arrive in wrong order */
			index = f_i / N_CHUNKSZ;
			pkg.data.chunk.index = htobe64(index);
			datasz = size - f_i;
			if (datasz > N_CHUNKSZ)
				datasz = N_CHUNKSZ;
			memcpy(pkg.data.chunk.data, &data[f_i], datasz);
			ns = pkgout(&pkg, client);
			nschk(ns);
			p_now = f_i + datasz;
			if (st_timer) {
				--st_timer;
				goto new_eta;
			}
			st_timer = PSTAT_FREQ;
			strtosi(pdone, sizeof pdone, p_now, 3);
		new_eta:
			clock_gettime(CLOCK_MONOTONIC, &now);
			streta(eta, sizeof eta, start, now, p_now, size);
			printf(
				"\033[u%s, %s/%s (%.2f%%) %s\033[K",
				nm, pdone, ptot,
				p_now * 100.0f / size, eta
			);
		}
		printf(
			"\033[u%s, %s/%s (100%%) %s\033[K\n"
			"transfer completed\n",
			nm, ptot, ptot, eta
		);
	skip:
		++fname;
		if (file != -1) {
			close(file);
			file = -1;
		}
		if (fmap != MAP_FAILED)
			munmap(fmap, fmapsz);
	}
	return ret;
}

int smain(void)
{
	int domain = AF_INET, type = SOCK_STREAM, prot = IPPROTO_TCP;
	int ret = 1;
	ssock = socket(domain, type, prot);
	if (ssock == -1) {
		perror("socket");
		goto fail;
	}
	sa.sin_family = domain;
	sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_port = htobe16(cfg.port);
	if (noclaim(ssock)) {
		perror("noclaim");
		goto fail;
	}
	if (bind(ssock, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
		perror("bind");
		goto fail;
	}
	listen(ssock, BACKLOG);
	puts("Waiting...");
	cl = sizeof(struct sockaddr_in);
	client = accept(ssock, (struct sockaddr*)&ca, (socklen_t*)&cl);
	if (client < 0) {
		perror("accept");
		goto fail;
	}
	puts("Connection accepted");
	ret = handle();
fail:
	if (client != -1)
		close(client);
	if (ssock != -1)
		close(ssock);
	if (fmap != MAP_FAILED)
		munmap(fmap, fmapsz);
	if (file != -1)
		close(file);
	return ret;
}
