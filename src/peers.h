#include "hash/khash.h"
#include <netinet/ip6.h>

static inline khint_t hash_addr(const struct sockaddr_in6* addr) {
    int i;
    const struct in6_addr* ip = &addr->sin6_addr;
    const khint_t* parts = ip->s6_addr32;
    khint_t h = parts[0];
    for(i=1;i<sizeof(ip->s6_addr32);++i) {
        h = (h <<5) - h + parts[i];
    }
    h = (h << 5) - h + addr->sin6_port;
    return h;
}

static inline int equal_addr(const struct sockaddr_in6* a, const struct sockaddr_in6* b) {
    /* memcmp the whole structure means one less pointer reference... should be faster
     * than memcmp the sin6_addr, then compare the sin6_port.
     */
    return 0 == memcmp(a,b,sizeof(*a));
}

KHASH_INIT(peer, const struct sockaddr_in6*, struct peer*, 1, hash_addr, equal_addr)

typedef khash_t(peer) peer_list;


enum peer_status {
    PEER_UNKNOWN,   // we haven't contacted them
    PEER_CONTACTED, // we have sent a greeting
    PEER_ACTIVE,    // they have sent us a greeting recently
    PEER_INACTIVE, // they have not sent us a greeting recently
};

struct peer {
    struct sockaddr_in6 addr;
    enum peer_status status;
    struct timespec last_message;    // they sent to us
    struct timespec last_greeted;    // we sent to them
    char ip[INET6_ADDRSTRLEN];
    char *nick;
};


peer_list* peers_init(void);
void peers_free(peer_list** self);
struct peer* peers_lookup(peer_list* peers, const struct sockaddr_in6* addr);
void peer_ip(struct peer* peer, char ip[]);

#define peers_each(self, block) { \
    struct peer* val; \
    for (khiter_t k = kh_begin(self); k < kh_end(self); ++k) { \
      if (!kh_exist(self, k)) continue; \
      val = kh_value(self, k); \
      block; \
    } \
  }
