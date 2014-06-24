#ifndef PEERDB_MAIN
#error please -DPEERDB_MAIN=/some/where
#endif

#include "peers.h"
#include "logging.h"

#include <uv.h>

#include <unistd.h>

uv_process_t handle;
uv_stdio_container_t stdio[2];
uv_pipe_t reader;
uv_pipe_t writer;

peer_list* peers = NULL;

struct context {
    uint8_t abuf[8+2];
    struct sockaddr_in6 addr;    
    uv_buf_t buf;
    uint8_t offset;
};

static size_t max(size_t a, size_t b) {
    if(a<b) return a;
    return b;
}

static void alloc_buffer(uv_handle_t* handle, size_t suggestion, uv_buf_t* buf) {
    uv_buf_t* sessionbuf = &((struct context*)handle->data)->buf;
    sessionbuf->base = realloc(sessionbuf->base, max(suggestion,sessionbuf->len));
    sessionbuf->len = suggestion;
    buf->base = sessionbuf->base;
    buf->len = suggestion;
}

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    if(nread < 0) {
        if(nread == UV_EOF) return;
        die("Reading failed %s\n",uv_strerror(nread));
    }
    struct context* ctx = (struct context*) stream->data;

    ssize_t noff = 0;
    ssize_t nleft = nread;

    uint8_t off = ctx->offset;
    uint8_t left = sizeof(ctx->abuf) - off;

    for(;;) {
        if(left <= nleft) {
            memcpy(ctx->abuf+off,buf->base+noff,left);
            ctx->offset = off = 0;
            left = sizeof(ctx->abuf);
            nleft -= left;
            noff += left;

            /* we have a complete peer! */
            struct sockaddr_in6 addr;
            /* XXX: probably could just directly copy to &addr
             * instead of a buffer, then assigning to addr
             */

            memcpy(addr.sin6_addr.s6_addr,ctx->abuf,8);
            addr.sin6_port = *((uint16_t*) ctx->abuf + 8);
            peers_lookup(peers, &addr);
        } else if(nleft == 0) {
            // could be zero in previous condition too, but in that case we also
            // have a complete peer to lookup.
            return;
        } else {
            /* ended with a partial. */
            memcpy(ctx->abuf+off,buf->base+noff,nleft);
            ctx->offset += nleft;
            return;
        }
    }
}

static void watch_reader(void) {
    if(reader.data == NULL) {
        struct context* ctx = malloc(sizeof(struct context));
        ctx->buf.base = NULL;
        ctx->offset = 0;
        reader.data = ctx;
    }
    uv_read_start((uv_stream_t*)&reader, alloc_buffer, on_read);
}

static void restart(void);

static void take_down_and_restart(uv_process_t* process, int64_t exit_status, int term_signal) {
    uv_read_stop((uv_stream_t*)&reader);
    // don't free reader.data here yet, because we're just restarting so we can
    // reuse it.
    // we can also reuse ctx->buf.base since it always realloc's anyway.
    restart();
}
static void restart(void) {
    uv_process_options_t options = {
        .file = PEERDB_MAIN,
        .exit_cb = take_down_and_restart,
        .stdio_count = 2,
        .stdio = stdio,
        .flags = 0
    };

    uv_spawn(uv_default_loop(),&handle, &options);

    watch_reader();
}

uint32_t writers = 0;

static void freereq(uv_write_t* req, int status) {
    --writers;
    free(req);
}

static void getLatest(void) {
    char type = 0;
    uv_buf_t buf;
    buf.base = &type;
    buf.len = 1;
    ++writers;
    uv_write(malloc(sizeof(uv_write_t)), (uv_stream_t*)&writer, &buf, 1, freereq);
}

void peerdb_startup(peer_list* p) {
    peers = p;

    uv_pipe_init(uv_default_loop(),&reader,1);
    uv_pipe_init(uv_default_loop(),&writer,1);

    reader.data = NULL; // THIS IS SORTA IMPORTANT
    
    stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
    stdio[0].data.stream = (uv_stream_t*)&reader;
    stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[1].data.stream = (uv_stream_t*)&writer;

    restart();
    getLatest();
}

void peerdb_observe(struct peer* peer) {
    if(writers > 0x100) {
        // save some cycles not overloading the db
        return;
    }
    // type (1) key (4), ip (8), port (2)
    uint8_t base[1+4+8+2];
    base[0] = 1;
    // we don't need persistent nanosecond timing
    // (tv_nsec just prevents an active DoS)
    uint32_t last_message = htonl(peer->last_message.tv_sec);
    memcpy(base+1,&last_message,4);

    memcpy(base+1+4,peer->addr.sin6_addr.s6_addr,8);
    // this one is always network byte order already:
    memcpy(base+1+4+8,&peer->addr.sin6_port,2);

    uv_buf_t message;
    message.base = (char*)base;
    message.len = 1+4+8+2;
    ++writers;
    uv_write(malloc(sizeof(uv_write_t)), (uv_stream_t*)&writer, &message, 1, freereq);
}
