ROOT_DIR := .

CC := gcc
CFLAGS := -std=gnu99 -g -O0 -Wno-format-extra-args -DSQLITE_DEBUG

# TODO: Isn't there a way to have it find everything automatically?

HEADERS := \
	src/common.h \
	src/async.h \
	src/EarthFS.h \
	src/URIList.h \
	src/Template.h \
	src/http/status.h \
	src/http/Headers.h \
	src/http/HTTPConnection.h \
	src/http/HTTPServer.h \
	src/http/MultipartForm.h \
	src/http/QueryString.h \
	deps/crypt_blowfish-1.0.4/ow-crypt.h \
	deps/http_parser/http_parser.h \
	deps/libco/libco.h \
	deps/multipart-parser-c/multipart_parser.h \
	deps/sqlite/sqlite3.h

OBJECTS := \
	build/main.o \
	build/EFSRepo.o \
	build/EFSSession.o \
	build/EFSSubmission.o \
	build/EFSHasher.o \
	build/EFSMetaFile.o \
	build/EFSFilter.o \
	build/EFSJSONFilterBuilder.o \
	build/EFSServer.o \
	build/Template.o \
	build/URIList.o \
	build/async.o \
	build/async_fs.o \
	build/async_mutex.o \
	build/async_rwlock.o \
	build/async_sqlite.o \
	build/http/Headers.o \
	build/http/HTTPConnection.o \
	build/http/HTTPServer.o \
	build/http/MultipartForm.o \
	build/http/QueryString.o \
	build/crypt/crypt_blowfish.o \
	build/crypt/crypt_gensalt.o \
	build/crypt/wrapper.o \
	build/crypt/x86.S.o \
	build/http_parser.o \
	build/libco.o \
	build/multipart_parser.o \
	build/sqlite3.o

OBJECTS += build/Blog.o

TEST_OBJECTS := \
	build/async.o \
	build/sqlite_async.o \
	build/libco.o \
	build/sqlite3.o

.DEFAULT_GOAL := all

all: build/earthfs

build/earthfs: $(OBJECTS)
	@-mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(CFLAGS) -lcrypto -luv -lyajl

build/crypt/%.S.o: deps/crypt_blowfish-1.0.4/%.S
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

build/crypt/%.o: deps/crypt_blowfish-1.0.4/%.c deps/crypt_blowfish-1.0.4/crypt.h deps/crypt_blowfish-1.0.4/ow-crypt.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

build/http_parser.o: deps/http_parser/http_parser.c deps/http_parser/http_parser.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

build/libco.o: deps/libco/libco.c deps/libco/libco.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Wno-parentheses
	# x86 version seems incompatible with Clang 3.3.

build/multipart_parser.o: deps/multipart-parser-c/multipart_parser.c deps/multipart-parser-c/multipart_parser.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -std=c89 -ansi -pedantic -Wall

build/sqlite3.o: deps/sqlite/sqlite3.c deps/sqlite/sqlite3.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -DSQLITE_ENABLE_FTS3 -DSQLITE_ENABLE_FTS3_PARENTHESIS -DSQLITE_MUTEX_APPDEF=1 -Wno-unused-value

build/%.o: src/%.c $(HEADERS)
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Werror

test: build/test/sqlite_async
	./build/test/sqlite_async

build/test/sqlite_async: test/test_sqlite_async.c $(TEST_OBJECTS) $(HEADERS)
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@.o $< $(CFLAGS) -Werror -Wall -Wno-unused-function
	$(CC) -o $@ $@.o $(TEST_OBJECTS) $(CFLAGS) -luv

clean:
	-rm -rf build/

.PHONY: all clean test

