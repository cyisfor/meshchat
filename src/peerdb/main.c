#include "logging.h"

#include <tcutil.h>
#include <tcbdb.h>
#include <stdbool.h>

#include <arpa/inet.h>

#include <string.h> // memcmp

#include <unistd.h>
#include <assert.h>

struct peer {
    uint8_t ip[8];
    uint16_t port;
} __attribute__((packed));

#define PEERS_LIMIT 0x100000
#define DEFAULT_LIST 0x100

static int min(int a, int b) {
    if(a < b) return a;
    return b;
}

static int compare_peers(const char* a, int asiz, const char* b, int bsiz, void* data) {
    assert(asiz == 4);
    assert(bsiz == 4);
    // memcmp in network byte order preserves numeric ordering
    return memcmp(a,b,4);
}

static void cleandb(TCBDB* db) {
    uint64_t num = tcbdbrnum(db);
    if ( num < PEERS_LIMIT - 1 ) 
        return;
    BDBCUR* c = tcbdbcurnew(db);
    tcbdbcurlast(c);

    for(;;) {
        // XXX: is this going to blow up?
        tcbdbcurout(c);
        tcbdbcurprev(c); 
        --num;
        if(num < PEERS_LIMIT - 1) 
            break;
    }
    tcbdbcurdel(c);
}

int main(void) {
    TCBDB* db = tcbdbnew();
    // XXX: TODO: absolutize this in ~/.local or something
    assert(true==tcbdbopen(db, "peerdb.tcb", BDBOWRITER|BDBOCREAT|BDBONOLCK));

    tcbdbsetcmpfunc(db, compare_peers, NULL);

    char buf[0x100];

    for(;;) {
        uint8_t type;
        assert(1==read(0,&type,1));
        switch(type) {
            case 0:
                {
                    int space;
                    uint32_t listnum, nderp;
                    int i;
                    BDBCUR* c = NULL;
                    listnum = min(tcbdbrnum(db),DEFAULT_LIST);
                    nderp = htonl(listnum);
                    write(1,&nderp,4);
                    c = tcbdbcurnew(db);
                    tcbdbcurfirst(c);
                    for(i=0;i<listnum;++i) {
                        assert(c);
                        const void* val = tcbdbcurval3(c,&space);
                        assert(space == 10);
                        assert(10==write(1,val,10));
                        tcbdbcurnext(c);
                    }
                    tcbdbcurdel(c);
                }
                break;
            case 1:
                {
                    struct peer guy;
                    assert(4==read(0,&buf,4));
                    read(0,&guy,sizeof(guy));
                    cleandb(db);
                    tcbdbputdup(db, buf, 4, &guy, sizeof(guy));
                }
                break;
            default:
                die("Got a strange request %d",type);
        };
    }

    return 0;
}
