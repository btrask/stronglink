
ROOT_DIR := .

CC := gcc
CFLAGS := -std=gnu99
LIBCO_VER := libco
# Use sjlj for clang on x86

#CFLAGS += -arch i386
CFLAGS += -g -O2 -Wno-unused-result
#CFLAGS += -g -O0
#CFLAGS += -DNDEBUG -Wno-unused-but-set-variable
CFLAGS += -DSQLITE_DEBUG -DHTTP_PARSER_DEBUG
#CFLAGS += -DSQLITE_ENABLE_SQLLOG

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
	$(SRC_DIR)/sqlite3f.h \
	$(SRC_DIR)/strndup.h \
	$(SRC_DIR)/http/status.h \
	$(SRC_DIR)/http/Headers.h \
	$(SRC_DIR)/http/HTTPMessage.h \
	$(SRC_DIR)/http/HTTPServer.h \
	$(SRC_DIR)/http/MultipartForm.h \
	$(SRC_DIR)/http/QueryString.h \
	$(DEPS_DIR)/libco/libco.h \
	$(DEPS_DIR)/crypt_blowfish-1.0.4/ow-crypt.h \
	$(DEPS_DIR)/http_parser/http_parser.h \
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
	$(BUILD_DIR)/EFSJSONFilterParser.o \
	$(BUILD_DIR)/EFSUserFilterParser.o \
	$(BUILD_DIR)/EFSPull.o \
	$(BUILD_DIR)/EFSServer.o \
	$(BUILD_DIR)/Template.o \
	$(BUILD_DIR)/URIList.o \
	$(BUILD_DIR)/sqlite3f.o \
	$(BUILD_DIR)/strndup.o \
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
	$(BUILD_DIR)/multipart_parser.o \
	$(BUILD_DIR)/sqlite/sqlite3.o

OBJECTS += $(BUILD_DIR)/libco/$(LIBCO_VER).o
#HEADERS += $(DEPS_DIR)/libcoro/coro.h
#OBJECTS += $(BUILD_DIR)/libcoro/coro.o $(BUILD_DIR)/libco_coro.o
#CFLAGS += -DCORO_USE_VALGRIND

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

LIBUV_DIR := $(DEPS_DIR)/uv/out/Debug/obj.target
#LIBUV_DIR := $(DEPS_DIR)/uv/build/Release

LIBS := -luv -lcrypto -lyajl -lpthread
LIBS += -lrt

.DEFAULT_GOAL := all

.PHONY: all
all: $(BUILD_DIR)/earthfs

.PHONY: libuv
libuv:
	@ cd $(DEPS_DIR)/uv && ./gyp_uv.py -f make -Dtarget_arch=i686 > /dev/null
	@ make -C $(DEPS_DIR)/uv/out -s

$(BUILD_DIR)/earthfs: $(OBJECTS) | libuv
	@- mkdir -p $(dir $@)
	$(CC) -o $@ $(OBJECTS) $(CFLAGS) -L$(LIBUV_DIR) $(LIBS)

$(BUILD_DIR)/crypt/%.S.o: $(DEPS_DIR)/crypt_blowfish-1.0.4/%.S
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILD_DIR)/crypt/%.o: $(DEPS_DIR)/crypt_blowfish-1.0.4/%.c $(DEPS_DIR)/crypt_blowfish-1.0.4/crypt.h $(DEPS_DIR)/crypt_blowfish-1.0.4/ow-crypt.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILD_DIR)/http_parser.o: $(DEPS_DIR)/http_parser/http_parser.c $(DEPS_DIR)/http_parser/http_parser.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILD_DIR)/libco/%.o: $(DEPS_DIR)/libco/%.c $(DEPS_DIR)/libco/libco.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Wno-parentheses

$(BUILD_DIR)/libcoro/%.o: $(DEPS_DIR)/libcoro/%.c $(DEPS_DIR)/libcoro/coro.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)
	# GPL build

$(BUILD_DIR)/multipart_parser.o: $(DEPS_DIR)/multipart-parser-c/multipart_parser.c $(DEPS_DIR)/multipart-parser-c/multipart_parser.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -std=c89 -ansi -pedantic -Wall

$(BUILD_DIR)/sqlite/%.o: $(DEPS_DIR)/sqlite/%.c $(DEPS_DIR)/sqlite/sqlite3.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -DSQLITE_ENABLE_FTS3 -DSQLITE_ENABLE_FTS3_PARENTHESIS -Wno-unused-value -Wno-constant-conversion

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Werror -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-unused-value

.PHONY: test
test: $(BUILD_DIR)/test/sqlite_async
	./$(BUILD_DIR)/test/sqlite_async

$(BUILD_DIR)/test/sqlite_async: $(ROOT_DIR)/test/test_sqlite_async.c $(TEST_OBJECTS) $(HEADERS)
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@.o $< $(CFLAGS) -Werror -Wall -Wno-unused-function
	$(CC) -o $@ $@.o $(TEST_OBJECTS) $(CFLAGS) -luv

.PHONY: clean
clean:
	- rm -rf $(BUILD_DIR)
	- rm -rf $(DEPS_DIR)/uv/out

