#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include "transfer.h"
#include "net.h"
#include "server.h"
#include "client.h"

#define VERSION "0"

static int help = 0;

struct cfg cfg = {
	.port = PORT,
	.address = "127.0.0.1",
	.files = NULL
};

static struct option long_opt[] = {
	{"help", no_argument, 0, 0},
	{"server", no_argument, 0, 0},
	{"client", no_argument, 0, 0},
	{"port", required_argument, 0, 0},
	{"address", required_argument, 0, 0},
	{"force", no_argument, 0, 0},
	{0, 0, 0, 0}
};

static int parse_opt(int argc, char **argv)
{
	int c, o_i;
	while (1) {
		c = getopt_long(argc, argv, "hscp:a:fr", long_opt, &o_i);
		if (c == -1) break;
		switch (c) {
		case 'h':
			help = 1;
			puts(
				"Quick and Dirty file transfer\n"
				"usage: transfer OPTIONS FILE...\n"
				"version " VERSION "\n"
				"available options:\n"
				"ch long    description\n"
				" h help    this help\n"
				" s server  master mode\n"
				" c client  slave mode\n"
				" p port    transfer endpoint number\n"
				" a address transfer endpoint IP\n"
				" f force   overwrite existing files\n"
				" r recover restart interrupted transfer"
			);
			break;
		case 's':
			if (cfg.mode & MODE_CLIENT) {
		err_mode:
				fputs(
					"too many modes: master and slave\n"
					"remove master or slave option\n",
					stderr
				);
				return -1;
			}
			cfg.mode |= MODE_SERVER;
			break;
		case 'c':
			if (cfg.mode & MODE_SERVER)
				goto err_mode;
			cfg.mode |= MODE_CLIENT;
			break;
		case 'p': {
			int port;
			port = atoi(optarg);
			if (port < 1 || port > 65535) {
				fprintf(stderr, "%s: bad port, use 1-65535\n", optarg);
				return -1;
			}
			cfg.port = port;
			break;
		};
		case 'a':
			cfg.address = optarg;
			break;
		case 'f':
			cfg.mode |= MODE_FORCE;
			break;
		case 'r':
			cfg.mode |= MODE_RECOVER;
			break;
		}
	}
	return o_i;
}

int main(int argc, char **argv)
{
	int argp, ret = parse_opt(argc, argv);
	if (ret < 0) return -ret;
	if (cfg.mode & MODE_SERVER) {
		struct stat st;
		argp = optind;
		if (argp == argc) {
			fputs(
				"no files specified\n"
				"usage: transfer OPTIONS files...\n",
				stderr
			);
			return 1;
		}
		cfg.files = &argv[argp];
		for (char **f = cfg.files; *f; ++f) {
			if (stat(*f, &st))
				perror(*f);
		}
	}
	if (!(cfg.mode & (MODE_SERVER | MODE_CLIENT))) {
		if (help) return 0;
		fputs(
			"bad mode: not master or slave\n"
			"specify -s or -c to start master or slave\n",
			stderr
		);
		return 1;
	}
	netinit();
	return cfg.mode & MODE_SERVER ? smain() : cmain();
}
