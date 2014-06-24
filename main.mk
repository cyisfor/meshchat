BIN = meshchat
SRC = $(wildcard src/*.c)
SRC += $(wildcard deps/*/*.c)
OBJ = $(SRC:.c=.o)
CFLAGS += -Ideps -Wall -pedantic -g
CFLAGS += -std=gnu99

all:: $(BIN)

ifdef INTERNAL_LIBUV
CFLAGS += -Ideps/libuv/include
LDFLAGS += deps/libuv/.libs/libuv.a -lc -lpthread -g
else
LDFLAGS += -luv -g
endif

-include src/peerdb/Makefile

$(BIN):: $(OBJ)
	${CC} -o $@ $^ ${LDFLAGS}

.c.o:
	${CC} -c ${CFLAGS} $< -o $@

install: all
	install -m 0755 ${BIN} ${DESTDIR}${BINDIR}

uninstall:
	rm -f ${DESTDIR}${BINDIR}/${BIN}

clean::
	rm -f $(BIN) $(OBJ)

.PHONY: all install uninstall

