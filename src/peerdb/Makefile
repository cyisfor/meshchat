TOKYOCABINET=deps/tokyocabinet/git
ME=src/peerdb

CFLAGS += -I$(TOKYOCABINET) -Isrc/
# need to specify -lpthread and -lc because gcc is dumb about static linking
LDFLAGS += -lpthread -lc -lbz2 -lz -lm

all:: $(ME)/main 

OBJ += $(ME)/interface.o

$(ME)/interface.o: $(ME)/interface.c
	$(CC) $(CFLAGS) -DPEERDB_MAIN=\"./$(ME)/main\" -c -o $@ $^

$(ME)/main: $(ME)/main.o src/logging.o $(TOKYOCABINET)/tokyocabinet_all.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(TOKYOCABINET)/configure:
	mkdir -p deps/tokyocabinet/
	cd deps/tokyocabinet/; \
		[ -d git ] || git clone git://github.com/Incubaid/tokyocabinet git; \

$(TOKYOCABINET)/Makefile: $(TOKYOCABINET)/configure
	cd $(TOKYOCABINET) && ./configure --disable-shared --enable-static --enable-fastest

$(TOKYOCABINET)/tokyocabinet_all.o: $(TOKYOCABINET)/Makefile
	$(MAKE) -C $(TOKYOCABINET) tokyocabinet_all.o

clean::
	rm -f $(ME)/main.o $(ME)/main
	[ -d $(TOKYOCABINET) ] && $(MAKE) -C $(TOKYOCABINET) clean

.PHONY: clean all
