include Makefile.nall

ROOT_DIR := .
BUILD_DIR := $(ROOT_DIR)/build
SRC_DIR := $(ROOT_DIR)/src
DEPS_DIR := $(ROOT_DIR)/deps
TOOLS_DIR := $(ROOT_DIR)/tools

# TODO: Hardcoded version number...
YAJL_BUILD_DIR := $(DEPS_DIR)/yajl/build/yajl-2.1.1

# TODO: Switch to c99
CFLAGS += -std=gnu99
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
	$(SRC_DIR)/util/aasprintf.h \
	$(SRC_DIR)/util/bcrypt.h \
	$(SRC_DIR)/util/fts.h \
	$(SRC_DIR)/util/hash.h \
	$(SRC_DIR)/util/raiserlimit.h \
	$(SRC_DIR)/common.h \
	$(SRC_DIR)/async/async.h \
	$(SRC_DIR)/db/db_base.h \
	$(SRC_DIR)/db/db_ext.h \
	$(SRC_DIR)/db/db_schema.h \
	$(SRC_DIR)/filter/EFSFilter.h \
	$(SRC_DIR)/EarthFS.h \
	$(SRC_DIR)/EFSDB.h \
	$(SRC_DIR)/EFSRepoPrivate.h \
	$(SRC_DIR)/http/status.h \
	$(SRC_DIR)/http/HTTPConnection.h \
	$(SRC_DIR)/http/HTTPServer.h \
	$(SRC_DIR)/http/HTTPHeaders.h \
	$(SRC_DIR)/http/MultipartForm.h \
	$(SRC_DIR)/http/QueryString.h \
	$(DEPS_DIR)/cmark/src/cmark.h \
	$(DEPS_DIR)/cmark/build/src/*.h \
	$(DEPS_DIR)/crypt_blowfish/ow-crypt.h \
	$(DEPS_DIR)/fts3/fts3_tokenizer.h \
	$(DEPS_DIR)/libco/libco.h \
	$(DEPS_DIR)/http_parser/http_parser.h \
	$(DEPS_DIR)/lsmdb/liblmdb/lmdb.h \
	$(DEPS_DIR)/multipart-parser-c/multipart_parser.h \
	$(DEPS_DIR)/smhasher/MurmurHash3.h \
	$(YAJL_BUILD_DIR)/include/yajl/*.h

# Generic library code
OBJECTS := \
	$(BUILD_DIR)/EFSRepo.o \
	$(BUILD_DIR)/EFSRepoAuth.o \
	$(BUILD_DIR)/EFSSession.o \
	$(BUILD_DIR)/EFSSubmission.o \
	$(BUILD_DIR)/EFSSubmissionMeta.o \
	$(BUILD_DIR)/EFSHasher.o \
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
	$(BUILD_DIR)/util/bcrypt.o \
	$(BUILD_DIR)/util/fts.o \
	$(BUILD_DIR)/util/hash.o \
	$(BUILD_DIR)/async/async.o \
	$(BUILD_DIR)/async/async_cond.o \
	$(BUILD_DIR)/async/async_fs.o \
	$(BUILD_DIR)/async/async_mutex.o \
	$(BUILD_DIR)/async/async_pool.o \
	$(BUILD_DIR)/async/async_rwlock.o \
	$(BUILD_DIR)/async/async_sem.o \
	$(BUILD_DIR)/async/async_stream.o \
	$(BUILD_DIR)/async/async_worker.o \
	$(BUILD_DIR)/http/HTTPConnection.o \
	$(BUILD_DIR)/http/HTTPServer.o \
	$(BUILD_DIR)/http/HTTPHeaders.o \
	$(BUILD_DIR)/http/MultipartForm.o \
	$(BUILD_DIR)/http/QueryString.o \
	$(BUILD_DIR)/deps/crypt/crypt_blowfish.o \
	$(BUILD_DIR)/deps/crypt/crypt_gensalt.o \
	$(BUILD_DIR)/deps/crypt/wrapper.o \
	$(BUILD_DIR)/deps/crypt/x86.S.o \
	$(BUILD_DIR)/deps/fts3/fts3_porter.o \
	$(BUILD_DIR)/deps/http_parser.o \
	$(BUILD_DIR)/deps/multipart_parser.o \
	$(BUILD_DIR)/deps/smhasher/MurmurHash3.o

ifdef USE_VALGRIND
HEADERS += $(DEPS_DIR)/libcoro/coro.h
OBJECTS += $(BUILD_DIR)/deps/libcoro/coro.o $(BUILD_DIR)/util/libco_coro.o
CFLAGS += -DCORO_USE_VALGRIND
else
OBJECTS += $(BUILD_DIR)/deps/libco/libco.o
endif

# Server executable-specific code
HEADERS += \
	$(SRC_DIR)/Template.h
OBJECTS += \
	$(BUILD_DIR)/Blog.o \
	$(BUILD_DIR)/Template.o \
	$(BUILD_DIR)/main.o \
	$(BUILD_DIR)/markdown.o

STATIC_LIBS += $(YAJL_BUILD_DIR)/lib/libyajl_s.a
CFLAGS += -I$(YAJL_BUILD_DIR)/include

MODULES += cmark
STATIC_LIBS += $(DEPS_DIR)/cmark/build/src/libcmark.a
CFLAGS += -I$(DEPS_DIR)/cmark/build/src

MODULES += mdb
STATIC_LIBS += $(DEPS_DIR)/lsmdb/liblmdb/liblmdb.a

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
  OBJECTS += $(BUILD_DIR)/deps/lsmdb/lsmdb.o
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
all: $(BUILD_DIR)/stronglink $(BUILD_DIR)/sln-markdown

$(BUILD_DIR)/stronglink: $(OBJECTS) $(MODULES)
	@- mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARNINGS) $(OBJECTS) $(STATIC_LIBS) $(LIBS) -o $@

$(YAJL_BUILD_DIR)/include/yajl/*.h: yajl
.PHONY: yajl
yajl:
	cd $(DEPS_DIR)/yajl/build && make yajl_s/fast

.PHONY: mdb
mdb:
	cd $(DEPS_DIR)/lsmdb/liblmdb && make

.PHONY: leveldb
leveldb:
	cd $(DEPS_DIR)/leveldb && make

.PHONY: snappy
snappy:
	cd $(DEPS_DIR)/snappy && make

$(DEPS_DIR)/cmark/build/src/*.h: cmark
.PHONY: cmark
cmark:
	cd $(DEPS_DIR)/cmark && make

.PHONY: libuv
libuv:
	cd $(DEPS_DIR)/uv && make
#	cd $(DEPS_DIR)/uv && make check

$(BUILD_DIR)/deps/crypt/%.S.o: $(DEPS_DIR)/crypt_blowfish/%.S
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/deps/crypt/%.o: $(DEPS_DIR)/crypt_blowfish/%.c $(DEPS_DIR)/crypt_blowfish/crypt.h $(DEPS_DIR)/crypt_blowfish/ow-crypt.h
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/deps/http_parser.o: $(DEPS_DIR)/http_parser/http_parser.c $(DEPS_DIR)/http_parser/http_parser.h
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -Werror -Wall $< -o $@

$(BUILD_DIR)/deps/libco/%.o: $(DEPS_DIR)/libco/%.c $(DEPS_DIR)/libco/libco.h
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -Wno-parentheses $< -o $@

$(BUILD_DIR)/deps/libcoro/%.o: $(DEPS_DIR)/libcoro/%.c $(DEPS_DIR)/libcoro/coro.h
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/deps/multipart_parser.o: $(DEPS_DIR)/multipart-parser-c/multipart_parser.c $(DEPS_DIR)/multipart-parser-c/multipart_parser.h
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -std=c89 -ansi -pedantic -Wall $< -o $@

$(BUILD_DIR)/deps/lsmdb/%.o: $(DEPS_DIR)/lsmdb/%.c $(DEPS_DIR)/lsmdb/lsmdb.h $(DEPS_DIR)/lsmdb/liblmdb/lmdb.h
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(WARNINGS) -Wno-format-extra-args $< -o $@

$(BUILD_DIR)/deps/fts3/%.o: $(DEPS_DIR)/fts3/%.c $(DEPS_DIR)/fts3/fts3Int.h $(DEPS_DIR)/fts3/fts3_tokenizer.h $(DEPS_DIR)/fts3/sqlite3.h
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(WARNINGS) $< -o $@

$(BUILD_DIR)/deps/sundown/%.o: $(DEPS_DIR)/sundown/%.c
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(WARNINGS) $< -o $@ #-I$(DEPS_DIR)/sundown/src

$(BUILD_DIR)/deps/smhasher/MurmurHash3.o: $(DEPS_DIR)/smhasher/MurmurHash3.cpp $(DEPS_DIR)/smhasher/MurmurHash3.h
	@- mkdir -p $(dir $@)
	$(CXX) -c -o $@ $< $(CXXFLAGS) $(WARNINGS)

$(BUILD_DIR)/filter/%.o: $(SRC_DIR)/filter/%.m $(HEADERS)
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(WARNINGS) $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(WARNINGS) $< -o $@

.PHONY: sln-markdown
sln-markdown: $(BUILD_DIR)/sln-markdown

$(BUILD_DIR)/sln-markdown: $(BUILD_DIR)/markdown_standalone.o $(BUILD_DIR)/http/QueryString.o $(SRC_DIR)/http/QueryString.h
	@- mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARNINGS) $^ $(DEPS_DIR)/cmark/build/src/libcmark.a -o $@

$(BUILD_DIR)/markdown_standalone.o: $(SRC_DIR)/markdown.c
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(WARNINGS) -DMARKDOWN_STANDALONE $< -o $@

.PHONY: clean
clean:
	- rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean: clean
	- cd $(DEPS_DIR)/cmark && make distclean
	- cd $(DEPS_DIR)/leveldb && make clean
	- cd $(DEPS_DIR)/lsmdb/liblmdb && make clean
	- cd $(DEPS_DIR)/snappy && make distclean
	- cd $(DEPS_DIR)/uv && make distclean
	- cd $(DEPS_DIR)/yajl && make distclean

