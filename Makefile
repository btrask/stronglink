CC := clang
CFLAGS := -g -O0

# TODO: Isn't there a way to have it find everything automatically?

HEADERS := \
	src/common.h \
	src/EarthFS.h \
	src/HTTPServer.h \
	src/QueryString.h \
	deps/crypt_blowfish-1.0.4/ow-crypt.h \
	deps/http_parser/http_parser.h \
	deps/libco/libco.h \
	deps/sqlite/sqlite3.h

OBJECTS := \
	build/main.o \
	build/EFSHasher.o \
	build/EFSRepo.o \
	build/EFSSession.o \
	build/EFSSubmission.o \
	build/EFSFilter.o \
	build/EFSJSONFilterBuilder.o \
	build/EFSServer.o \
	build/HTTPServer.o \
	build/QueryString.o \
	build/crypt/crypt_blowfish.o \
	build/crypt/crypt_gensalt.o \
	build/crypt/wrapper.o \
	build/crypt/x86.S.o \
	build/http_parser.o \
	build/libco.o \
	build/sqlite3.o

all: build/earthfs

build/earthfs: $(OBJECTS)
	@-mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(CFLAGS) -lssl -luv -lyajl

build/crypt/%.S.o: deps/crypt_blowfish-1.0.4/%.S
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

build/crypt/%.o: deps/crypt_blowfish-1.0.4/%.c deps/crypt_blowfish-1.0.4/crypt.h deps/crypt_blowfish-1.0.4/ow-crypt.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

build/http_parser.o: deps/http_parser/http_parser.c deps/http_parser/http_parser.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

build/libco.o: deps/libco/sjlj.c deps/libco/libco.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Wno-parentheses
	# x86 version is crashing a lot for us with Clang 3.3.

build/sqlite3.o: deps/sqlite/sqlite3.c deps/sqlite/sqlite3.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -DSQLITE_ENABLE_FTS3 -DSQLITE_ENABLE_FTS3_PARENTHESIS -Wno-unused-value

build/%.o: src/%.c $(HEADERS)
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Werror

clean:
	-rm -rf build/

.PHONY: all clean

