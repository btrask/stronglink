ROOT_DIR := .

CC := gcc
CFLAGS := -std=gnu99 -g -O0 -Wno-format-extra-args -DSQLITE_DEBUG -DHTTP_PARSER_DEBUG #-DSQLITE_ENABLE_SQLLOG

BUILD_DIR := $(ROOT_DIR)/build
SRC_DIR := $(ROOT_DIR)/src
DEPS_DIR := $(ROOT_DIR)/deps

# TODO: Isn't there a way to have it find everything automatically?

HEADERS := \
	$(SRC_DIR)/common.h \
	$(SRC_DIR)/async.h \
	$(SRC_DIR)/bcrypt.h \
	$(SRC_DIR)/EarthFS.h \
	$(SRC_DIR)/URIList.h \
	$(SRC_DIR)/Template.h \
	$(SRC_DIR)/http/status.h \
	$(SRC_DIR)/http/Headers.h \
	$(SRC_DIR)/http/HTTPMessage.h \
	$(SRC_DIR)/http/HTTPServer.h \
	$(SRC_DIR)/http/MultipartForm.h \
	$(SRC_DIR)/http/QueryString.h \
	$(DEPS_DIR)/crypt_blowfish-1.0.4/ow-crypt.h \
	$(DEPS_DIR)/http_parser/http_parser.h \
	$(DEPS_DIR)/libco/libco.h \
	$(DEPS_DIR)/multipart-parser-c/multipart_parser.h \
	$(DEPS_DIR)/sqlite/sqlite3.h

OBJECTS := \
	$(BUILD_DIR)/main.o \
	$(BUILD_DIR)/EFSRepo.o \
	$(BUILD_DIR)/EFSSession.o \
	$(BUILD_DIR)/EFSSubmission.o \
	$(BUILD_DIR)/EFSHasher.o \
	$(BUILD_DIR)/EFSMetaFile.o \
	$(BUILD_DIR)/EFSFilter.o \
	$(BUILD_DIR)/EFSJSONFilterBuilder.o \
	$(BUILD_DIR)/EFSPull.o \
	$(BUILD_DIR)/EFSServer.o \
	$(BUILD_DIR)/Template.o \
	$(BUILD_DIR)/URIList.o \
	$(BUILD_DIR)/async.o \
	$(BUILD_DIR)/async_fs.o \
	$(BUILD_DIR)/async_mutex.o \
	$(BUILD_DIR)/async_rwlock.o \
	$(BUILD_DIR)/async_sqlite.o \
	$(BUILD_DIR)/bcrypt.o \
	$(BUILD_DIR)/http/Headers.o \
	$(BUILD_DIR)/http/HTTPMessage.o \
	$(BUILD_DIR)/http/HTTPServer.o \
	$(BUILD_DIR)/http/MultipartForm.o \
	$(BUILD_DIR)/http/QueryString.o \
	$(BUILD_DIR)/crypt/crypt_blowfish.o \
	$(BUILD_DIR)/crypt/crypt_gensalt.o \
	$(BUILD_DIR)/crypt/wrapper.o \
	$(BUILD_DIR)/crypt/x86.S.o \
	$(BUILD_DIR)/http_parser.o \
	$(BUILD_DIR)/libco.o \
	$(BUILD_DIR)/multipart_parser.o \
	$(BUILD_DIR)/sqlite/sqlite3.o

# DEBUG
#OBJECTS += \
#	$(BUILD_DIR)/sqlite/test_sqllog.o

OBJECTS += \
	$(BUILD_DIR)/Blog.o

TEST_OBJECTS := \
	$(BUILD_DIR)/async.o \
	$(BUILD_DIR)/sqlite_async.o \
	$(BUILD_DIR)/libco.o \
	$(BUILD_DIR)/sqlite3.o

.DEFAULT_GOAL := all

all: $(BUILD_DIR)/earthfs

$(BUILD_DIR)/earthfs: $(OBJECTS)
	@-mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(CFLAGS) -lcrypto -luv -lyajl

$(BUILD_DIR)/crypt/%.S.o: $(DEPS_DIR)/crypt_blowfish-1.0.4/%.S
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILD_DIR)/crypt/%.o: $(DEPS_DIR)/crypt_blowfish-1.0.4/%.c $(DEPS_DIR)/crypt_blowfish-1.0.4/crypt.h $(DEPS_DIR)/crypt_blowfish-1.0.4/ow-crypt.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILD_DIR)/http_parser.o: $(DEPS_DIR)/http_parser/http_parser.c $(DEPS_DIR)/http_parser/http_parser.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILD_DIR)/libco.o: $(DEPS_DIR)/libco/libco.c $(DEPS_DIR)/libco/libco.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Wno-parentheses
	# x86 version seems incompatible with Clang 3.3.

$(BUILD_DIR)/multipart_parser.o: $(DEPS_DIR)/multipart-parser-c/multipart_parser.c $(DEPS_DIR)/multipart-parser-c/multipart_parser.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -std=c89 -ansi -pedantic -Wall

$(BUILD_DIR)/sqlite/%.o: $(DEPS_DIR)/sqlite/%.c $(DEPS_DIR)/sqlite/sqlite3.h
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -DSQLITE_ENABLE_FTS3 -DSQLITE_ENABLE_FTS3_PARENTHESIS -DSQLITE_MUTEX_APPDEF=1 -Wno-unused-value

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Werror -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter

test: $(BUILD_DIR)/test/sqlite_async
	./$(BUILD_DIR)/test/sqlite_async

$(BUILD_DIR)/test/sqlite_async: $(ROOT_DIR)/test/test_sqlite_async.c $(TEST_OBJECTS) $(HEADERS)
	@-mkdir -p $(dir $@)
	$(CC) -c -o $@.o $< $(CFLAGS) -Werror -Wall -Wno-unused-function
	$(CC) -o $@ $@.o $(TEST_OBJECTS) $(CFLAGS) -luv

clean:
	-rm -rf $(BUILD_DIR)/

.PHONY: all clean test

