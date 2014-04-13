/* vim: set expandtab ts=4 sw=4: */
/*
 * meshchat.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "ircd.h"
#include "hash/hash.h"
#include "meshchat.h"
#include "cjdnsadmin.h"
#include "util.h"

#define MESHCHAT_PORT 14627
#define MESHCHAT_PACKETLEN 1400
#define MESHCHAT_PEERFETCH_INTERVAL 60
#define MESHCHAT_PEERSERVICE_INTERVAL 5
#define MESHCHAT_TIMEOUT 60
#define MESHCHAT_PING_INTERVAL 20
#define MESHCHAT_RETRY_INTERVAL 600

struct meshchat {
    ircd_t *ircd;
    cjdnsadmin_t *cjdnsadmin;
    const char *host;
    int port;
    int listener;
    time_t last_peerfetch;
    time_t last_peerservice;
    hash_t *peers;
};

struct peer {
    char ip[40];
    struct sockaddr_in6 addr;
    enum peer_status status;
    time_t last_greeting;   // they sent to us
    time_t last_greeted;    // we sent to them
};

enum event_type {
    EVENT_MSG = 1,
    EVENT_PRIVMSG,
    EVENT_ACTION,
    EVENT_NOTICE,
    EVENT_JOIN,
    EVENT_PART,
    EVENT_NICK,
};

void handle_datagram(struct sockaddr *addr, char *buffer, ssize_t len);
void found_ip(void *obj, const char *ip);
void service_peers(meshchat_t *mc);
peer_t *peer_new(const char *ip);
void peer_send(meshchat_t *mc, peer_t *peer, char *msg, size_t len);
void greet_peer(meshchat_t *mc, peer_t *peer);

void on_irc_msg(void *obj, char *channel, char *data);
void on_irc_privmsg(void *obj, char *channel, char *data);
void on_irc_action(void *obj, char *channel, char *data);
void on_irc_notice(void *obj, char *channel, char *data);
void on_irc_nick(void *obj, char *channel, char *data);
void on_irc_join(void *obj, char *channel, char *data);
void on_irc_part(void *obj, char *channel, char *data);

meshchat_t *meshchat_new() {
    meshchat_t *mc = calloc(1, sizeof(meshchat_t));
    if (!mc) {
        perror("calloc");
        return NULL;
    }

    mc->peers = hash_new();
    if (!mc->peers) {
        free(mc);
        return NULL;
    }

    mc->cjdnsadmin = cjdnsadmin_new();
    if (!mc->cjdnsadmin) {
        free(mc->peers);
        free(mc);
        fprintf(stderr, "fail\n");
        return NULL;
    }

    // add callback for peer discovery through cjdns
    cjdnsadmin_on_found_ip(mc->cjdnsadmin, found_ip, (void *)mc);

    ircd_callbacks_t callbacks = {
        .on_msg = {(void *)mc, on_irc_msg},
        .on_privmsg = {(void *)mc, on_irc_privmsg},
        .on_part = {(void *)mc, on_irc_part},
        .on_join = {(void *)mc, on_irc_join},
        .on_nick = {(void *)mc, on_irc_nick},
        .on_action = {(void *)mc, on_irc_action},
        .on_notice = {(void *)mc, on_irc_notice},
    };

    mc->ircd = ircd_new(&callbacks);
    if (!mc->ircd) {
        fprintf(stderr, "fail\n");
        exit(1);
    }

    // todo: allow custom port/hostname
    mc->port = MESHCHAT_PORT;
    mc->host = "::"; // wildcard

    return mc;
}

void
meshchat_free(meshchat_t *mc) {
    cjdnsadmin_free(mc->cjdnsadmin);
    ircd_free(mc->ircd);
    hash_free(mc->peers);
    free(mc);
}

void
meshchat_start(meshchat_t *mc) {
    // start the local IRC server
    ircd_start(mc->ircd);

    // connect to cjdns admin
    cjdnsadmin_start(mc->cjdnsadmin);

    struct sockaddr_in6 addr = {0};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(mc->port);
    inet_pton(AF_INET6, mc->host, &(addr.sin6_addr));

    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd==-1) {
        perror("socket");
    }
    mc->listener = fd;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
    }

    printf("meshchat bound to %s\n", sprint_addrport((struct sockaddr *)&addr));
}

void
meshchat_add_select_descriptors(meshchat_t *mc, fd_set *in_set,
        fd_set *out_set, int *maxfd) {

    ircd_add_select_descriptors(mc->ircd, in_set, out_set, maxfd);
    cjdnsadmin_add_select_descriptors(mc->cjdnsadmin, in_set, out_set, maxfd);

    int fd = mc->listener;

    FD_SET(fd, in_set);
    if (fd > *maxfd) {
        *maxfd = fd;
    }
}

void
meshchat_process_select_descriptors(meshchat_t *mc, fd_set *in_set,
        fd_set *out_set) {
    static char buffer[MESHCHAT_PACKETLEN];

    ircd_process_select_descriptors(mc->ircd, in_set, out_set);
    cjdnsadmin_process_select_descriptors(mc->cjdnsadmin, in_set, out_set);

    // periodically fetch list of peers to ping
    time_t now;
    time(&now);
    if (difftime(now, mc->last_peerfetch) > MESHCHAT_PEERFETCH_INTERVAL) {
        mc->last_peerfetch = now;
        cjdnsadmin_fetch_peers(mc->cjdnsadmin);
    }

    // periodically ping peers
    if (difftime(now, mc->last_peerservice) > MESHCHAT_PEERSERVICE_INTERVAL) {
        mc->last_peerservice = now;
        service_peers(mc);
    }

    // check if our listener has something
    if (FD_ISSET(mc->listener, in_set)) {
        struct sockaddr_storage src_addr;
        socklen_t src_addr_len = sizeof(src_addr);
        ssize_t count = recvfrom(mc->listener, buffer, sizeof(buffer), 0,
                (struct sockaddr *)&src_addr, &src_addr_len);
        if (count == -1) {
            perror("recvfrom");
        } else if (count == sizeof(buffer)) {
            fprintf(stderr, "datagram too large for buffer: truncated");
        } else {
            handle_datagram((struct sockaddr *)&src_addr, buffer, count);
        }
    }
}

void
handle_datagram(struct sockaddr *addr, char *buffer, ssize_t len) {
    printf("%s sent: \"%*s\"\n", sprint_addrport(addr),
            (int)len-1, buffer+1);
}

void
found_ip(void *obj, const char *ip) {
    meshchat_t *mc = (meshchat_t *)obj;
    //printf("ip: %s, peers: %u\n", ip, hash_size(mc->peers));
    if (hash_size(mc->peers) && hash_has(mc->peers, (char *)ip)) {
        // we have already seen this ip
        return;
    }

    // restrict the network to a small collection of peers, for testing
    if (1 &&
#define IP(addr) strcmp(ip, addr) &&
#include "mynetwork.def"
    1) {
        return;
    }

    // new peer. add to the list
    peer_t *peer = peer_new(ip);
    if (!peer) {
        fprintf(stderr, "Unable to create peer\n");
        return;
    }
    hash_set(mc->peers, (char *)ip, (void *)peer);
    //printf("ip: %s\n", ip);
}

peer_t *
peer_new(const char *ip) {
    peer_t *peer = (peer_t *)malloc(sizeof(peer_t));
    if (!peer) {
        return NULL;
    }
    peer->status = PEER_UNKNOWN;
    peer->last_greeted = -1;
    peer->last_greeting = -1;
    strcpy(peer->ip, ip);
    memset(&peer->addr, 0, sizeof(peer->addr));
    peer->addr.sin6_family = AF_INET6;
    peer->addr.sin6_port = htons(MESHCHAT_PORT);
    inet_pton(AF_INET6, ip, &(peer->addr.sin6_addr));
    return peer;
}

void
peer_send(meshchat_t *mc, peer_t *peer, char *msg, size_t len) {
    if (sendto(mc->listener, msg, len, 0, (struct sockaddr *)&peer->addr,
                sizeof(peer->addr)) < 0) {
        perror("sendto");
    }
    //printf("sent \"%*s\" to %s\n", (int)len-1, msg,
        //sprint_addrport((struct sockaddr *)&peer->addr));
}

static inline void
service_peer(meshchat_t *mc, time_t now, const char *key, peer_t *peer) {
    //peer_print(peer);
    switch (peer->status) {
        // greet new unknown peer
        case PEER_UNKNOWN:
            greet_peer(mc, peer);
            peer->status = PEER_CONTACTED;
            break;
        case PEER_CONTACTED:
        case PEER_ACTIVE:
            if (difftime(now, peer->last_greeted) > MESHCHAT_PING_INTERVAL) {
                // ping active peer
                greet_peer(mc, peer);
            }
            if (difftime(now, peer->last_greeting) > MESHCHAT_TIMEOUT) {
                // mark unreponsive peer as timed out
                peer->status = PEER_INACTIVE;
            }
            break;
        case PEER_INACTIVE:
            // greet inactive peer after a while
            if (difftime(now, peer->last_greeted) > MESHCHAT_RETRY_INTERVAL) {
                greet_peer(mc, peer);
                peer->status = PEER_CONTACTED;
            }
            break;
    }
}

static inline void
broadcast_all_peer(meshchat_t *mc, peer_t *peer, void *msg, size_t len) {
    // send only to active peer
    if (peer->status == PEER_ACTIVE) {
        peer_send(mc, peer, msg, len);
    }
}

// send a message to all active peers
void
broadcast_all(meshchat_t *mc, void *msg) {
    size_t len = strlen(msg);
    if (len > MESHCHAT_PACKETLEN) len = MESHCHAT_PACKETLEN;
    hash_each_val(mc->peers, broadcast_all_peer(mc, val, msg, len));
}

// send a message to all active peers in a channel
void
broadcast_channel(meshchat_t *mc, char *channel, void *msg) {
    size_t len = strlen(msg);
    if (len > MESHCHAT_PACKETLEN) len = MESHCHAT_PACKETLEN;
    //hash_each_val(mc->peers, broadcast_active_peer(mc, val, msg, len));
}

void
service_peers(meshchat_t *mc) {
    //printf("servicing peers (%u)\n", hash_size(mc->peers));
    time_t now;
    time(&now);
    hash_each(mc->peers, service_peer(mc, now, key, val));
}

void
greet_peer(meshchat_t *mc, peer_t *peer) {
    static char *msg = "hello";
    //printf("greeting peer %s\n", peer->ip);
    peer_send(mc, peer, msg, strlen(msg)+1);
    time(&peer->last_greeted);
}

void
on_irc_msg(void *obj, char *channel, char *data) {
    meshchat_t *mc = (meshchat_t *)obj;
    static char msg[MESHCHAT_PACKETLEN];
    size_t channel_len = strlen(channel)+1;
    size_t data_len = strlen(data);
    // todo: check for buffer overrun
    msg[0] = EVENT_MSG;
    // include null byte as seperator
    strncpy(msg + 1, channel, channel_len);
    strncpy(msg + 1 + channel_len, data, data_len);
    //snprintf(msg+1, sizeof(msg)-1, "%s\0%s", channel, data);
    broadcast_channel(mc, channel, msg);
}

void
on_irc_action(void *obj, char *channel, char *data) {
    // todo
}

void
on_irc_notice(void *obj, char *channel, char *data) {
    // todo
}

void
on_irc_privmsg(void *obj, char *recipient, char *data) {
    /*
    meshchat_t *mc = (meshchat_t *)obj;
    static char msg[MESHCHAT_PACKETLEN];
    msg[0] = EVENT_PRIVMSG;
    strncpy(msg+1, data, sizeof(msg)-1);
    // todo: look up peer
    //peer = 
    //peer_send(mc, peer, msg);
    */
}

void
on_irc_join(void *obj, char *channel, char *data) {
    meshchat_t *mc = (meshchat_t *)obj;
    static char msg[MESHCHAT_PACKETLEN];
    msg[0] = EVENT_JOIN;
    strncpy(msg+1, channel, sizeof(msg)-1);
    broadcast_all(mc, msg);
}

void
on_irc_part(void *obj, char *channel, char *data) {
    meshchat_t *mc = (meshchat_t *)obj;
    static char msg[MESHCHAT_PACKETLEN];
    msg[0] = EVENT_PART;
    strncpy(msg+1, channel, sizeof(msg)-1);
    broadcast_all(mc, msg);
}

void
on_irc_nick(void *obj, char *channel, char *data) {
    (void)channel;
    meshchat_t *mc = (meshchat_t *)obj;
    static char msg[MESHCHAT_PACKETLEN];
    msg[0] = EVENT_NICK;
    // todo: don't bother writing null bytes up to the end of the buffer
    strncpy(msg+1, data, sizeof(msg)-1);
    broadcast_all(mc, msg);
}
