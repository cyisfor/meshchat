#include "peers.h"

#include <hash/khash.h>

struct peer {
    char ip[40];
    struct sockaddr_in6 addr;
    enum peer_status status;
    struct timespec last_message;    // they sent to us
    struct timespec last_greeted;    // we sent to them
    char *nick;
};

struct peer *
peer_new(const struct sockaddr_in6* addr) {
    struct peer *peer = (struct peer *)malloc(sizeof(struct peer));
    if (!peer) {
        return NULL;
    }
    peer->status = PEER_UNKNOWN;
    ZERO(peer->last_greeted);
    ZERO(peer->last_message);
    peer->nick = NULL;
    strcpy(peer->ip, ip);
    memcpy(peer->addr,addr,sizeof(*addr));
    return peer;
}

static inline khint_t hash_addr(const struct sockaddr_in6* addr) {
    int i;
    struct in6_addr* ip = addr->sin6_addr;
    khint_t* parts = ip->in6_u.u6_addr32;
    khint_t h = parts[0];
    for(i=1;i<sizeof(ip->in6_u.u6_addr32);++i) {
        h = (h <<5) - h + parts[i];
    }
    h = (h << 5) - h + addr->sin6_port;
    return h;
}

static inline equal_addr(const struct sockaddr_in6* a, const struct sockaddr_in6* b) {
    return 0 == memcmp(a,b,sizeof(a));
}

KHASH_INIT(zoop, const struct sockaddr_in6*, struct peer*, 1, hash_addr, equal_addr);

struct peers {
    khash_t(zoop) list;
    uint8_t dirty;
};


struct peers* peers_init(void) {
    struct peers* self = calloc(1,sizeof(struct peers));
    return self;
}

void peers_free(struct peers** self) {
    struct peers* doomed = *self;
    *self = NULL;
    kh_destroy(zoop,doomed);
}

struct peer* peers_lookup(struct peers* peers, const struct sockaddr_in6* addr) {
    khash_t(zoop)* list = (khash_t(zoop)*)peers; 
    // make it ->list if need any other info about all peers

    khiter_t k = kh_get(zoop, list, addr);
    if(k == kh_end(list)) {
        struct peer* peer = peer_new(addr);
        k = kh_put(zoop, list, addr, &ret);
        if(ret) {
            perror("kh_get returns no result, but kh_put indicates there is one???\n");
            exit(3);
        }
        kh_value(list, k) = peer;
        return peer;
    } else {
        return kh_value(list, k);
    }
}

struct peer *
peer_new(const char *ip) {
    struct peer *peer = (struct peer *)malloc(sizeof(struct peer));
    if (!peer) {
        return NULL;
    }
    peer->status = PEER_UNKNOWN;
    ZERO(peer->last_greeted);
    ZERO(peer->last_message);
    peer->nick = NULL;
    strcpy(peer->ip, ip);
    memset(&peer->addr, 0, sizeof(peer->addr));
    peer->addr.sin6_family = AF_INET6;
    inet_pton(AF_INET6, ip, &(peer->addr.sin6_addr));
    peer->addr.sin6_port = ports_lookup(&peer->addr.sin6_addr);
    return peer;
}


