#include <netinet/ip6.h>
#include <stdint.h>

void ports_init(uint16_t _defaultPort);
uint16_t ports_lookup(const struct in6_addr* id);
uint8_t ports_changed(const struct sockaddr_in6* peer);
