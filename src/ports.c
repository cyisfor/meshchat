#include "ports.h"
#include <gdbm.h>

GDBM_FILE db = NULL;
uint16_t defaultPort = 0;

void ports_init(uint16_t _defaultPort) {
    defaultPort = _defaultPort;
    db = gdbm_open("ports.db", 0, GDBM_WRCREAT | GDBM_NOLOCK, 0600, NULL);
}

uint16_t ports_lookup(const struct in6_addr* id) {
    datum key = {(char*)id, sizeof(struct in6_addr)};
    datum value = gdbm_fetch(db, key);
    if(value.dptr == NULL)
        return defaultPort;
    return *((uint16_t*)value.dptr);
}

uint8_t ports_changed(const struct sockaddr_in6* peer) {
    datum key = {(char*)&peer->sin6_addr, sizeof(struct in6_addr)};
    datum value = gdbm_fetch(db, key);
    if(value.dptr != NULL) {
        uint16_t port = *((uint16_t*)value.dptr);
        if(port == peer->sin6_port)
            return 0;
    }
    value.dptr = (char*) &peer->sin6_port;
    gdbm_store(db, key, value, GDBM_REPLACE);
    return 1;
}
