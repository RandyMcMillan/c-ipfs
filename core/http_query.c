#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/core/client_api.h"

typedef struct {
    char **args;
    size_t arg_count;
} http_parsed_args_t;

void http_parse_query_arguments(const char *query_string, http_parsed_args_t *parsed) {
    parsed->args = NULL;
    parsed->arg_count = 0;
    if (!query_string) return;

    const char *ptr = query_string;
    while ((ptr = strstr(ptr, "arg=")) != NULL) {
        ptr += 4;
        const char *end = strchr(ptr, '&');
        size_t len = end ? (size_t)(end - ptr) : strlen(ptr);

        parsed->args = realloc(parsed->args, sizeof(char *) * (parsed->arg_count + 1));
        parsed->args[parsed->arg_count] = malloc(len + 1);
        strncpy(parsed->args[parsed->arg_count], ptr, len);
        parsed->args[parsed->arg_count][len] = '\0';
        parsed->arg_count++;

        if (!end) break;
        ptr = end + 1;
    }
}

void http_free_parsed_args(http_parsed_args_t *parsed) {
    if (!parsed) return;
    for (size_t i = 0; i < parsed->arg_count; i++) {
        free(parsed->args[i]);
    }
    free(parsed->args);
    parsed->args = NULL;
    parsed->arg_count = 0;
}
