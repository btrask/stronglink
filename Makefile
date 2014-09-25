ROOT_DIR := .

CC := gcc
CFLAGS := -std=gnu99
LIBCO_VER := libco
# Use sjlj for clang on x86

#CFLAGS += -arch i386
CFLAGS += -g -O2 -fno-omit-frame-pointer -Wno-unused-result
#CFLAGS += -g -O0
#CFLAGS += -DNDEBUG -Wno-unused-but-set-variable
CFLAGS += -DHTTP_PARSER_DEBUG
CFLAGS += -DLIBCO_MP

BUILD_DIR := $(ROOT_DIR)/build
SRC_DIR := $(ROOT_DIR)/src
DEPS_DIR := $(ROOT_DIR)/deps
TOOLS_DIR := $(ROOT_DIR)/tools

# TODO: Isn't there a way to have it find everything automatically?

HEADERS := \
	$(SRC_DIR)/common.h \
	$(SRC_DIR)/async/async.h \
	$(SRC_DIR)/bcrypt.h \
	$(SRC_DIR)/EarthFS.h \
	$(SRC_DIR)/filter/EFSFilter.h \
	$(SRC_DIR)/Template.h \
	$(SRC_DIR)/lsmdb.h \
	$(SRC_DIR)/db.h \
	$(SRC_DIR)/fts.h \
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
	$(DEPS_DIR)/liblmdb/lmdb.h \
	$(DEPS_DIR)/fts3/fts3_tokenizer.h \
	$(DEPS_DIR)/sundown/src/markdown.h \
	$(DEPS_DIR)/sundown/html/html.h \
	$(DEPS_DIR)/sundown/html/houdini.h

OBJECTS := \
	$(BUILD_DIR)/main.o \
	$(BUILD_DIR)/EFSRepo.o \
	$(BUILD_DIR)/EFSSession.o \
	$(BUILD_DIR)/EFSSubmission.o \
	$(BUILD_DIR)/EFSHasher.o \
	$(BUILD_DIR)/EFSMetaFile.o \
	$(BUILD_DIR)/filter/EFSFilter.o \
	$(BUILD_DIR)/filter/EFSIndividualFilter.o \
	$(BUILD_DIR)/filter/EFSCollectionFilter.o \
	$(BUILD_DIR)/filter/EFSMetaFileFilter.o \
	$(BUILD_DIR)/filter/EFSJSONFilterParser.o \
	$(BUILD_DIR)/filter/EFSUserFilterParser.o \
	$(BUILD_DIR)/EFSPull.o \
	$(BUILD_DIR)/EFSServer.o \
	$(BUILD_DIR)/Template.o \
	$(BUILD_DIR)/lsmdb.o \
	$(BUILD_DIR)/db.o \
	$(BUILD_DIR)/fts.o \
	$(BUILD_DIR)/strndup.o \
	$(BUILD_DIR)/async/async.o \
	$(BUILD_DIR)/async/async_cond.o \
	$(BUILD_DIR)/async/async_fs.o \
	$(BUILD_DIR)/async/async_sem.o \
	$(BUILD_DIR)/async/async_mutex.o \
	$(BUILD_DIR)/async/async_rwlock.o \
	$(BUILD_DIR)/async/async_worker.o \
	$(BUILD_DIR)/async/async_pool.o \
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
	$(BUILD_DIR)/liblmdb/mdb.o \
	$(BUILD_DIR)/liblmdb/midl.o \
	$(BUILD_DIR)/fts3/fts3_porter.o \
	$(BUILD_DIR)/sundown/src/markdown.o \
	$(BUILD_DIR)/sundown/src/autolink.o \
	$(BUILD_DIR)/sundown/src/buffer.o \
	$(BUILD_DIR)/sundown/src/stack.o \
	$(BUILD_DIR)/sundown/html/html.o \
	$(BUILD_DIR)/sundown/html/html_smartypants.o \
	$(BUILD_DIR)/sundown/html/houdini_href_e.o \
	$(BUILD_DIR)/sundown/html/houdini_html_e.o

#OBJECTS += $(BUILD_DIR)/libco/$(LIBCO_VER).o
HEADERS += $(DEPS_DIR)/libcoro/coro.h
OBJECTS += $(BUILD_DIR)/libcoro/coro.o $(BUILD_DIR)/libco_coro.o
CFLAGS += -DCORO_USE_VALGRIND

OBJECTS += \
	$(BUILD_DIR)/Blog.o

LIBUV_DIR := $(DEPS_DIR)/uv/out/Debug/obj.target
#LIBUV_DIR := $(DEPS_DIR)/uv/build/Release

LIBS := -luv -lcrypto -lyajl -lpthread -lobjc -lm
LIBS += -lrt

.DEFAULT_GOAL := all

.PHONY: all
all: $(BUILD_DIR)/earthfs

#.PHONY: libuv
#libuv:
#	@ cd $(DEPS_DIR)/uv && ./gyp_uv.py -f make -Dtarget_arch=i686 > /dev/null
#	@ make -C $(DEPS_DIR)/uv/out -s

$(LIBUV_DIR)/libuv.a: UNUSED := $(shell cd $(DEPS_DIR)/uv; ./gyp_uv.py -f make -Dtarget_arch=i686; make -C out)

$(BUILD_DIR)/earthfs: $(OBJECTS) $(LIBUV_DIR)/libuv.a
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

$(BUILD_DIR)/multipart_parser.o: $(DEPS_DIR)/multipart-parser-c/multipart_parser.c $(DEPS_DIR)/multipart-parser-c/multipart_parser.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -std=c89 -ansi -pedantic -Wall

$(BUILD_DIR)/liblmdb/%.o: $(DEPS_DIR)/liblmdb/%.c $(DEPS_DIR)/liblmdb/lmdb.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILD_DIR)/fts3/%.o: $(DEPS_DIR)/fts3/%.c $(DEPS_DIR)/fts3/fts3Int.h $(DEPS_DIR)/fts3/fts3_tokenizer.h $(DEPS_DIR)/fts3/sqlite3.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILD_DIR)/sundown/%.o: $(DEPS_DIR)/sundown/%.c
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) #-I$(DEPS_DIR)/sundown/src

$(BUILD_DIR)/filter/%.o: $(SRC_DIR)/filter/%.m $(HEADERS)
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Werror -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-unused-value

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Werror -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-unused-value

.PHONY: tools
tools: $(BUILD_DIR)/tools/efs_dump

$(BUILD_DIR)/tools/efs_dump: $(TOOLS_DIR)/efs_dump.c $(SRC_DIR)/db.c $(SRC_DIR)/db.h $(DEPS_DIR)/liblmdb/mdb.c $(DEPS_DIR)/liblmdb/midl.c $(DEPS_DIR)/liblmdb/lmdb.h
	@- mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(CFLAGS) -lpthread -Werror -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-unused-value

.PHONY: clean
clean:
	- rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean: clean
	- rm -rf $(DEPS_DIR)/uv/out

