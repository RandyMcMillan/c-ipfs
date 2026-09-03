#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libp2p/utils/logger.h"
#include "ipfs/repo/init.h"
#include "ipfs/importer/importer.h"
#include "ipfs/importer/exporter.h"
#include "ipfs/dnslink/dnslink.h"
#include "ipfs/core/daemon.h"
#include "ipfs/core/swarm.h"
#include "ipfs/cmd/cli.h"
#include "ipfs/cmd/ipfs/id.h"
#include "ipfs/cmd/ipfs/get.h"
#include "ipfs/cmd/ipfs/nostr.h"
#include "ipfs/namesys/name.h"

static void print_help(FILE* out) {
	fprintf(out, "USAGE:\n");
	fprintf(out, "  ipfs [--config=<path>] <command> [arguments]\n\n");
	fprintf(out, "COMMANDS:\n");
	fprintf(out, "  init          Initialize IPFS repo and generate a new keypair\n");
	fprintf(out, "  add <path>    Add a file to IPFS\n");
	fprintf(out, "  cat <hash>    Display the contents of an IPFS object\n");
	fprintf(out, "  get <hash>    Download an IPFS object\n");
	fprintf(out, "  id            Show IPFS node identity\n");
	fprintf(out, "  object get    Get a dag object\n");
	fprintf(out, "  daemon        Run the IPFS daemon\n");
	fprintf(out, "  ping <peer>   Send echo requests to a peer\n");
	fprintf(out, "  swarm         Swarm management options\n");
	fprintf(out, "  name          Publish and resolve IPNS names\n");
	fprintf(out, "  dns <domain>  DNS link resolution\n");
	fprintf(out, "  nostr         Nostr hybrid protocol (publish, repo, patch, issue)\n");
	fprintf(out, "  help          Show this help text\n\n");
	fprintf(out, "OPTIONS:\n");
	fprintf(out, "  -c, --config  Path to the configuration directory\n");
	fprintf(out, "  -h, --help    Show this help text\n");
}

#ifdef __MINGW32__
    void bzero(void *s, size_t n)
    {
        memset (s, '\0', n);
    }

    char *strtok_r(char *str, const char *delim, char **save)
    {
        char *res, *last;

        if( !save )
            return strtok(str, delim);
        if( !str && !(str = *save) )
            return NULL;
        last = str + strlen(str);
        if( (*save = res = strtok(str, delim)) )
        {
            *save += strlen(res);
            if( *save < last )
                (*save)++;
            else
                *save = NULL;
        }
        return res;
    }
#endif // MINGW

void stripit(int argc, char** argv) {
	char* old_arg = argv[argc];
	int full_length = strlen(old_arg);
	char *tmp = (char*) malloc(full_length + 1);
	if (tmp == NULL) {
		return;
	}
	char* ptr1 = &old_arg[1];
	strcpy(tmp, ptr1);
	tmp[strlen(tmp)-1] = 0;
	strcpy(old_arg, tmp);
	free(tmp);
	return;
}

void strip_quotes(int argc, char** argv) {
	for(int i = 0; i < argc; i++) {
		if (argv[i][0] == '\'' && argv[i][strlen(argv[i])-1] == '\'') {
			stripit(i, argv);
		}
	}
}

#define INIT 1
#define ADD 2
#define OBJECT_GET 3
#define DNS 4
#define CAT 5
#define DAEMON 6
#define PING 7
#define GET 8
#define NAME 9
#define SWARM 10
#define ID 11
#define HELP 12
#define NOSTR 13

/**
 * Find out if this command line argument is part of a switch
 * @param argc the number of arguments
 * @param argv the arguments
 * @param index the argument to look at
 * @returns 0 if not a switch, 1 if it is a regular switch, 2 if the next parameter is also part of the switch
 */
int is_switch(int argc, char** argv, int index) {
	char* to_test = argv[index];
	if (to_test[0] == '-') {
		if (strcmp(to_test, "-c") == 0 || strcmp(to_test, "--config") == 0) {
			return 2;
		}
		return 1;
	}
	return 0;
}

/**
 * Find the command line piece that will actually do something
 * @param argc the number of command line arguments
 * @param argv the actual command line arguments
 * @returns the index of the item that does something, or false(0)
 */
int get_cli_verb(int argc, char** argv) {
	for(int i = 1; i < argc; i++) {
		int advance_by_more_than_one = is_switch(argc, argv, i);
		if (advance_by_more_than_one == 0) {
			// this is the verb
			return i;
		} else {
			if (advance_by_more_than_one == 2) {
				// skip the next one
				i++;
			}
		}
	}
	return 0;
}

/***
 * Basic parsing of command line arguments to figure out where the user wants to go
 */
int parse_arguments(int argc, char** argv) {
	int saw_help = 0;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			saw_help = 1;
		}
	}
	int index = get_cli_verb(argc, argv);
	if (argc == 1 || index == 0) {
		return HELP;
	}
	if (strcmp("help", argv[index]) == 0) {
		return HELP;
	}
	if (saw_help && strcmp("swarm", argv[index]) == 0) {
		return SWARM;
	}
	if (strcmp("init", argv[index]) == 0) {
		return INIT;
	}
	if (strcmp("add", argv[index]) == 0) {
		return ADD;
	}
	if (strcmp("object", argv[index]) == 0 && argc > 2 && strcmp("get", argv[index+1]) == 0) {
		return OBJECT_GET;
	}
	if (strcmp("cat", argv[index]) == 0) {
		return CAT;
	}
	if (strcmp("dns", argv[index]) == 0) {
		return DNS;
	}
	if (strcmp("daemon", argv[index]) == 0) {
		return DAEMON;
	}
	if (strcmp("ping", argv[index]) == 0) {
		return PING;
	}
	if (strcmp("get", argv[index]) == 0) {
		return GET;
	}
	if (strcmp("name", argv[index]) == 0) {
		return NAME;
	}
	if (strcmp("swarm", argv[index]) == 0) {
		return SWARM;
	}
	if (strcmp("id", argv[index]) == 0) {
		return ID;
	}
	if (strcmp("nostr", argv[index]) == 0) {
		return NOSTR;
	}
	return -1;
}

/***
 * The beginning
 */
int main(int argc, char** argv) {
	int retVal = 0;
	strip_quotes(argc, argv);
	// CliArguments is the new way to do it. Eventually, all will use this structure
	struct CliArguments* args = cli_arguments_new(argc, argv);
	if (args != NULL) {
		// until then, use the old way
		int cmd = parse_arguments(argc, argv);
		switch (cmd) {
			case (HELP):
				print_help(stdout);
				retVal = 1;
				break;
			case (INIT):
				retVal = ipfs_repo_init(argc, argv);
				break;
			case (ADD):
				retVal = ipfs_import_files(args);
				break;
			case (OBJECT_GET):
				retVal = ipfs_exporter_object_get(argc, argv);
				break;
			case(GET):
				retVal = ipfs_get(argc, argv);
				break;
			case (CAT):
				retVal = ipfs_exporter_object_cat(args, stdout);
				break;
			case (DNS):
				retVal = ipfs_dns(argc, argv);
				break;
			case (DAEMON):
				retVal = ipfs_daemon(argc, argv);
				break;
			case (PING):
				retVal = ipfs_ping(argc, argv);
				break;
			case (NAME):
				retVal = ipfs_name(args);
				break;
			case (SWARM):
				retVal = ipfs_swarm(args);
				break;
			case (ID):
				retVal = ipfs_id(argc, argv);
				break;
			case (NOSTR):
				retVal = ipfs_nostr(argc, argv);
				break;
			default:
				fprintf(stderr, "Error: unknown command.\n\n");
				print_help(stderr);
				retVal = 0;
				break;
		}
		cli_arguments_free(args);
	}
	libp2p_logger_free();
	exit(retVal == 1 ? EXIT_SUCCESS : EXIT_FAILURE);
}
