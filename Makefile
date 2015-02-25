include Makefile.nall

ROOT_DIR := .
BUILD_DIR := $(ROOT_DIR)/build
SRC_DIR := $(ROOT_DIR)/src
DEPS_DIR := $(ROOT_DIR)/deps
TOOLS_DIR := $(ROOT_DIR)/tools

# TODO: Hardcoded version number...
YAJL_BUILD_DIR := $(DEPS_DIR)/yajl/build/yajl-2.1.1

# TODO: Switch to c99
CFLAGS := -std=gnu99
CFLAGS += -g -fno-omit-frame-pointer
CFLAGS += -DLIBCO_MP

WARNINGS := -Werror -Wall
WARNINGS += -Wno-unused
WARNINGS += -Wno-deprecated
WARNINGS += -Wno-objc-root-class

ifdef RELEASE
CFLAGS += -O2
CFLAGS += -DNDEBUG
else
CFLAGS += -O2
#CFLAGS += -O0
CFLAGS += -DHTTP_PARSER_DEBUG
endif

# TODO: Use compiler -M to track header dependencies automatically
HEADERS := \
	$(SRC_DIR)/common.h \
	$(SRC_DIR)/async/async.h \
	$(SRC_DIR)/db/db_base.h \
	$(SRC_DIR)/db/db_ext.h \
	$(SRC_DIR)/db/db_schema.h \
	$(SRC_DIR)/filter/EFSFilter.h \
	$(SRC_DIR)/bcrypt.h \
	$(SRC_DIR)/EarthFS.h \
	$(SRC_DIR)/EFSRepoPrivate.h \
	$(SRC_DIR)/fts.h \
	$(SRC_DIR)/hash.h \
	$(SRC_DIR)/http/status.h \
	$(SRC_DIR)/http/Headers.h \
	$(SRC_DIR)/http/HTTPMessage.h \
	$(SRC_DIR)/http/HTTPServer.h \
	$(SRC_DIR)/http/MultipartForm.h \
	$(SRC_DIR)/http/QueryString.h \
	$(DEPS_DIR)/libco/libco.h \
	$(DEPS_DIR)/crypt_blowfish/ow-crypt.h \
	$(DEPS_DIR)/http_parser/http_parser.h \
	$(DEPS_DIR)/multipart-parser-c/multipart_parser.h \
	$(DEPS_DIR)/lsmdb/liblmdb/lmdb.h \
	$(DEPS_DIR)/fts3/fts3_tokenizer.h \
	$(DEPS_DIR)/sundown/src/markdown.h \
	$(DEPS_DIR)/sundown/html/html.h \
	$(DEPS_DIR)/sundown/html/houdini.h \
	$(YAJL_BUILD_DIR)/include/yajl/*.h

# Generic library code
OBJECTS := \
	$(BUILD_DIR)/EFSRepo.o \
	$(BUILD_DIR)/EFSSession.o \
	$(BUILD_DIR)/EFSSubmission.o \
	$(BUILD_DIR)/EFSHasher.o \
	$(BUILD_DIR)/EFSMetaFile.o \
	$(BUILD_DIR)/db/db_ext.o \
	$(BUILD_DIR)/db/db_schema.o \
	$(BUILD_DIR)/filter/EFSFilter.o \
	$(BUILD_DIR)/filter/EFSIndividualFilter.o \
	$(BUILD_DIR)/filter/EFSCollectionFilter.o \
	$(BUILD_DIR)/filter/EFSMetaFileFilter.o \
	$(BUILD_DIR)/filter/EFSJSONFilterParser.o \
	$(BUILD_DIR)/filter/EFSUserFilterParser.o \
	$(BUILD_DIR)/EFSPull.o \
	$(BUILD_DIR)/EFSServer.o \
	$(BUILD_DIR)/fts.o \
	$(BUILD_DIR)/hash.o \
	$(BUILD_DIR)/async/async.o \
	$(BUILD_DIR)/async/async_cond.o \
	$(BUILD_DIR)/async/async_fs.o \
	$(BUILD_DIR)/async/async_mutex.o \
	$(BUILD_DIR)/async/async_pool.o \
	$(BUILD_DIR)/async/async_rwlock.o \
	$(BUILD_DIR)/async/async_sem.o \
	$(BUILD_DIR)/async/async_stream.o \
	$(BUILD_DIR)/async/async_worker.o \
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
	$(BUILD_DIR)/lsmdb/liblmdb/mdb.o \
	$(BUILD_DIR)/lsmdb/liblmdb/midl.o \
	$(BUILD_DIR)/fts3/fts3_porter.o \
	$(BUILD_DIR)/sundown/src/markdown.o \
	$(BUILD_DIR)/sundown/src/autolink.o \
	$(BUILD_DIR)/sundown/src/buffer.o \
	$(BUILD_DIR)/sundown/src/stack.o \
	$(BUILD_DIR)/sundown/html/html.o \
	$(BUILD_DIR)/sundown/html/html_smartypants.o \
	$(BUILD_DIR)/sundown/html/houdini_href_e.o \
	$(BUILD_DIR)/sundown/html/houdini_html_e.o

ifdef USE_VALGRIND
HEADERS += $(DEPS_DIR)/libcoro/coro.h
OBJECTS += $(BUILD_DIR)/libcoro/coro.o $(BUILD_DIR)/libco_coro.o
CFLAGS += -DCORO_USE_VALGRIND
else
OBJECTS += $(BUILD_DIR)/libco/libco.o
endif


# Server executable-specific code
HEADERS += \
	$(SRC_DIR)/Template.h
OBJECTS += \
	$(BUILD_DIR)/Blog.o \
	$(BUILD_DIR)/Template.o \
	$(BUILD_DIR)/main.o

MODULES :=

STATIC_LIBS += $(YAJL_BUILD_DIR)/lib/libyajl_s.a
CFLAGS += -I$(YAJL_BUILD_DIR)/include

MODULES += libuv
STATIC_LIBS += $(DEPS_DIR)/uv/.libs/libuv.a

LIBSNAPPY := $(DEPS_DIR)/snappy/.libs/libsnappy.a

LIBS += -lcrypto -lpthread -lobjc -lm
ifeq ($(platform),linux)
LIBS += -lrt
endif

ifeq ($(DB),rocksdb)
  MODULES += snappy
  STATIC_LIBS += $(LIBSNAPPY)
  LIBS += -lrocksdb
  LIBS += -lstdc++
  OBJECTS += $(BUILD_DIR)/db/db_base_rocksdb.o
else ifeq ($(DB),hyper)
  MODULES += snappy
  STATIC_LIBS += $(LIBSNAPPY)
  LIBS += -lhyperleveldb
  LIBS += -lstdc++
  OBJECTS += $(BUILD_DIR)/db/db_base_leveldb.o
else ifeq ($(DB),lsmdb)
  HEADERS += $(DEPS_DIR)/lsmdb/lsmdb.h
  OBJECTS += $(BUILD_DIR)/lsmdb/lsmdb.o
  OBJECTS += $(BUILD_DIR)/db/db_base_lsmdb.o
else ifeq ($(DB),mdb)
  OBJECTS += $(BUILD_DIR)/db/db_base_mdb.o
else
  MODULES += leveldb snappy
  CFLAGS += -I$(DEPS_DIR)/leveldb/include -I$(DEPS_DIR)/snappy/include
  STATIC_LIBS += $(LIBSNAPPY)
  LIBS += $(DEPS_DIR)/leveldb/libleveldb.a
  LIBS += -lstdc++
  OBJECTS += $(BUILD_DIR)/db/db_base_leveldb.o
endif

.DEFAULT_GOAL := all

.PHONY: all
all: $(BUILD_DIR)/earthfs

$(BUILD_DIR)/earthfs: $(OBJECTS) $(MODULES)
	@- mkdir -p $(dir $@)
	$(CC) -o $@ $(OBJECTS) $(CFLAGS) -Werror -Wall $(STATIC_LIBS) $(LIBS)

.PHONY: yajl
yajl:
	cd $(DEPS_DIR)/yajl/build && make yajl_s/fast

$(YAJL_BUILD_DIR)/include/yajl/*.h: yajl

.PHONY: leveldb
leveldb:
	cd $(DEPS_DIR)/leveldb && make

.PHONY: snappy
snappy:
	cd $(DEPS_DIR)/snappy && make

.PHONY: libuv
libuv:
	cd $(DEPS_DIR)/uv && make
#	cd $(DEPS_DIR)/uv && make check

$(BUILD_DIR)/crypt/%.S.o: $(DEPS_DIR)/crypt_blowfish/%.S
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILD_DIR)/crypt/%.o: $(DEPS_DIR)/crypt_blowfish/%.c $(DEPS_DIR)/crypt_blowfish/crypt.h $(DEPS_DIR)/crypt_blowfish/ow-crypt.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILD_DIR)/http_parser.o: $(DEPS_DIR)/http_parser/http_parser.c $(DEPS_DIR)/http_parser/http_parser.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Werror -Wall

$(BUILD_DIR)/libco/%.o: $(DEPS_DIR)/libco/%.c $(DEPS_DIR)/libco/libco.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -Wno-parentheses

$(BUILD_DIR)/libcoro/%.o: $(DEPS_DIR)/libcoro/%.c $(DEPS_DIR)/libcoro/coro.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BUILD_DIR)/multipart_parser.o: $(DEPS_DIR)/multipart-parser-c/multipart_parser.c $(DEPS_DIR)/multipart-parser-c/multipart_parser.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) -std=c89 -ansi -pedantic -Wall

$(BUILD_DIR)/lsmdb/%.o: $(DEPS_DIR)/lsmdb/%.c $(DEPS_DIR)/lsmdb/lsmdb.h $(DEPS_DIR)/lsmdb/liblmdb/lmdb.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) $(WARNINGS) -Wno-format-extra-args

$(BUILD_DIR)/fts3/%.o: $(DEPS_DIR)/fts3/%.c $(DEPS_DIR)/fts3/fts3Int.h $(DEPS_DIR)/fts3/fts3_tokenizer.h $(DEPS_DIR)/fts3/sqlite3.h
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) $(WARNINGS)

$(BUILD_DIR)/sundown/%.o: $(DEPS_DIR)/sundown/%.c
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) $(WARNINGS) #-I$(DEPS_DIR)/sundown/src

$(BUILD_DIR)/filter/%.o: $(SRC_DIR)/filter/%.m $(HEADERS)
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) $(WARNINGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@- mkdir -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) $(WARNINGS)


.PHONY: clean
clean:
	- rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean: clean
	- cd $(DEPS_DIR)/uv && make distclean
	- cd $(DEPS_DIR)/yajl && make distclean
	- cd $(DEPS_DIR)/leveldb && make clean
