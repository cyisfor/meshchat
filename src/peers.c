#include "peers.h"
#include "logging.h"

#include <arpa/inet.h>


struct private_peer {
    struct peer public;
    /* meh */
};

struct peer *
peer_new(const struct sockaddr_in6* addr) {
    struct private_peer *ppeer = (struct private_peer *)calloc(1,sizeof(struct private_peer));
    if (!ppeer) {
        return NULL;
    }
    struct peer* peer = &ppeer->public;
    peer->status = PEER_UNKNOWN;
    inet_ntop(AF_INET6, &(addr->sin6_addr),peer->ip,INET6_ADDRSTRLEN);
    memcpy(&peer->addr,addr,sizeof(*addr));
    return peer;
}

struct private_peer_list {
    peer_list list;
    uint8_t dirty;
};


peer_list* peers_init(void) {
    peer_list* self = calloc(1,sizeof(struct private_peer_list));
    return self;
}

void peers_free(peer_list** self) {
    peer_list* doomed = *self;
    *self = NULL;
    kh_destroy(peer,doomed);
}

struct peer* peers_lookup(peer_list* peers, const struct sockaddr_in6* addr) {
    khiter_t k = kh_get(peer, peers, addr);
    if(k == kh_end(peers)) {
        int ret = 0;
        struct peer* peer = peer_new(addr);
        k = kh_put(peer, peers, addr, &ret);
        if(ret) {
            die("kh_get returns no result, but kh_put indicates there is one???\n");
        }
        kh_value(peers, k) = peer;
        return peer;
    } else {
        return kh_value(peers, k);
    }
}
