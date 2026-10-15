#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipfs/util/time.h"

int ipfs_util_time_parse_RFC3339 (struct timespec *ts, char *s)
{
    char *r;
    struct tm tm;

    if (!ts || !s || strlen(s) != 35) {
        return 1;
    }
    memset(&tm, 0, sizeof(tm));
    r = strptime (s, "%Y-%m-%dT%H:%M:%S", &tm);
    if (!r || *r != '.') {
        return 2;
    }
    tm.tm_isdst = 0;
    ts->tv_sec = timegm(&tm);
    ts->tv_nsec = atoll(++r);
    return 0;
}

char *ipfs_util_time_format_RFC3339 (struct timespec *ts)
{
    char buf[31], *ret;

    ret = malloc(36);
    if (!ret) {
        return NULL;
    }

    if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.%%09dZ00:00", gmtime(&(ts->tv_sec))) != sizeof(buf)-1 ||
        snprintf(ret, 36, buf, ts->tv_nsec) != 35) {
        free (ret);
        return NULL;
    }
    return ret;
}
