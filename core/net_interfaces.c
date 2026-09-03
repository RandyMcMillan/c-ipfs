#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ipfs/core/net.h"

int ipfs_net_list_interfaces(char addrs[][64], int max_addrs) {
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) != 0) return 0;

    int count = 0;
    for (ifa = ifap; ifa && count < max_addrs; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &(sa->sin_addr), addrs[count], 64);
        count++;
    }

    freeifaddrs(ifap);
    return count;
}

bool ipfs_net_is_public_addr(const char *ip_str) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str, &addr) != 1) return false;

    uint32_t ip = ntohl(addr.s_addr);
    if ((ip >> 24) == 127 || (ip >> 24) == 10 ||
        ((ip >> 24) == 172 && ((ip >> 16) & 0x0F) == 1) ||
        ((ip >> 16) == 0xC0A8)) {
        return false;
    }
    return true;
}
