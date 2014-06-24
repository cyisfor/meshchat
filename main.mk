BIN = meshchat
SRC = $(wildcard src/*.c)
SRC += $(wildcard deps/*/*.c)
OBJ = $(SRC:.c=.o)
CFLAGS += -Ideps -Wall -pedantic
CFLAGS += -std=gnu99

all: $(BIN)

ifdef INTERNAL_LIBUV
CFLAGS += -Ideps/libuv/include
LDFLAGS += deps/libuv/.libs/libuv.a -lc -lpthread
else
LDFLAGS += -luv
endif

OBJ += deps/tokyocabinet/git/libtokyocabinet.so

deps/tokyocabinet/git/Makefile: deps/tokyocabinet/git/configure
	cd deps/tokyocabinet/git && ./configure --enable-fastest

deps/tokyocabinet/git/libtokyocabinet.so: deps/tokyocabinet/git/Makefile
	$(MAKE) -C deps/tokyocabinet/git libtokyocabinet.so

$(BIN):: $(OBJ)
	${CC} -o $@ $^ ${LDFLAGS}

.c.o:
	${CC} -c ${CFLAGS} $< -o $@

install: all
	install -m 0755 ${BIN} ${DESTDIR}${BINDIR}

uninstall:
	rm -f ${DESTDIR}${BINDIR}/${BIN}

clean:
	rm -f $(BIN) $(OBJ)
	[ -d deps/tokyocabinet/git ] && make -C deps/tokyocabinet/git clean

.PHONY: all install uninstall

