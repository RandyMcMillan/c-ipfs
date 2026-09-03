#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ipfs/datastore/key.h"

char *datastore_key_clean_input(const char *raw_key) {
    if (!raw_key) return NULL;

    size_t len = strlen(raw_key);
    if (len == 0 || len > 1024) return NULL;

    char *clean = malloc(len + 2);
    if (!clean) return NULL;

    size_t j = 0;
    clean[j++] = '/';

    for (size_t i = 0; i < len; i++) {
        char c = raw_key[i];

        if (c == '\\' || c == '/') {
            if (j > 0 && clean[j - 1] == '/') continue;
            clean[j++] = '/';
        } else if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') {
            clean[j++] = c;
        }
    }

    if (j > 1 && clean[j - 1] == '/') {
        j--;
    }

    clean[j] = '\0';
    return clean;
}
