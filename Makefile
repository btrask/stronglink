include Makefile.nall

ROOT_DIR := .
BUILD_DIR := $(ROOT_DIR)/build
SRC_DIR := $(ROOT_DIR)/src
DEPS_DIR := $(ROOT_DIR)/deps
TOOLS_DIR := $(ROOT_DIR)/tools

# TODO: Hardcoded version number...
YAJL_BUILD_DIR := $(DEPS_DIR)/yajl/build/yajl-2.1.1

DESTDIR ?=
PREFIX ?= /usr/local

# TODO: Switch to c99
CFLAGS += -std=gnu99
CFLAGS += -g -fno-omit-frame-pointer
CFLAGS += -DLIBCO_MP
CFLAGS += -DINSTALL_PREFIX=\"$(PREFIX)\"

# Also consider -Weverything...
WARNINGS := -Werror -Wall -Wextra -Wshadow

# Dead code can sometimes indicate bugs, but these are just too noisy and
# putting an UNUSED() macro everywhere would probably mask any problems
# we might find.
WARNINGS += -Wno-unused -Wno-unused-parameter

# For OS X.
WARNINGS += -Wno-deprecated

# We define our own Objective-C root class (SLNObject) because we don't use
# Apple's frameworks. Warning only used by Clang. GCC complains about it when
# it stops on an unrelated error, but otherwise it doesn't cause any problems.
WARNINGS += -Wno-objc-root-class

# We use use the isa instance variable when checking that all of the other
# instance variables are zeroed.
WARNINGS += -Wno-deprecated-objc-isa-usage

# Checking that an unsigned variable is less than a constant which happens
# to be zero should be okay.
WARNINGS += -Wno-type-limits

# Usually happens for a ssize_t after already being checked for non-negative,
# or a constant that I don't want to stick a "u" on.
WARNINGS += -Wno-sign-compare

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
	$(SRC_DIR)/async/async.h \
	$(SRC_DIR)/db/db_base.h \
	$(SRC_DIR)/db/db_ext.h \
	$(SRC_DIR)/db/db_schema.h \
	$(SRC_DIR)/http/status.h \
	$(SRC_DIR)/http/HTTPConnection.h \
	$(SRC_DIR)/http/HTTPServer.h \
	$(SRC_DIR)/http/HTTPHeaders.h \
	$(SRC_DIR)/http/MultipartForm.h \
	$(SRC_DIR)/http/QueryString.h \
	$(SRC_DIR)/util/aasprintf.h \
	$(SRC_DIR)/util/bcrypt.h \
	$(SRC_DIR)/util/fts.h \
	$(SRC_DIR)/util/raiserlimit.h \
	$(SRC_DIR)/common.h \
	$(SRC_DIR)/StrongLink.h \
	$(SRC_DIR)/SLNDB.h \
	$(SRC_DIR)/filter/SLNFilter.h \
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
	$(BUILD_DIR)/SLNRepo.o \
	$(BUILD_DIR)/SLNSessionCache.o \
	$(BUILD_DIR)/SLNSession.o \
	$(BUILD_DIR)/SLNSubmission.o \
	$(BUILD_DIR)/SLNSubmissionMeta.o \
	$(BUILD_DIR)/SLNHasher.o \
	$(BUILD_DIR)/SLNPull.o \
	$(BUILD_DIR)/SLNServer.o \
	$(BUILD_DIR)/filter/SLNFilter.o \
	$(BUILD_DIR)/filter/SLNIndividualFilter.o \
	$(BUILD_DIR)/filter/SLNCollectionFilter.o \
	$(BUILD_DIR)/filter/SLNNegationFilter.o \
	$(BUILD_DIR)/filter/SLNURIFilter.o \
	$(BUILD_DIR)/filter/SLNMetaFileFilter.o \
	$(BUILD_DIR)/filter/SLNBadMetaFileFilter.o \
	$(BUILD_DIR)/filter/SLNFilterOpts.o \
	$(BUILD_DIR)/filter/SLNJSONFilterParser.o \
	$(BUILD_DIR)/filter/SLNUserFilterParser.o \
	$(BUILD_DIR)/async/async.o \
	$(BUILD_DIR)/async/async_cond.o \
	$(BUILD_DIR)/async/async_fs.o \
	$(BUILD_DIR)/async/async_mutex.o \
	$(BUILD_DIR)/async/async_pool.o \
	$(BUILD_DIR)/async/async_rwlock.o \
	$(BUILD_DIR)/async/async_sem.o \
	$(BUILD_DIR)/async/async_stream.o \
	$(BUILD_DIR)/async/async_worker.o \
	$(BUILD_DIR)/db/db_ext.o \
	$(BUILD_DIR)/db/db_schema.o \
	$(BUILD_DIR)/http/HTTPConnection.o \
	$(BUILD_DIR)/http/HTTPServer.o \
	$(BUILD_DIR)/http/HTTPHeaders.o \
	$(BUILD_DIR)/http/MultipartForm.o \
	$(BUILD_DIR)/http/QueryString.o \
	$(BUILD_DIR)/util/bcrypt.o \
	$(BUILD_DIR)/util/fts.o \
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

# Blog server
HEADERS += \
	$(DEPS_DIR)/cmark/src/cmark.h \
	$(DEPS_DIR)/cmark/src/buffer.h \
	$(DEPS_DIR)/cmark/src/houdini.h \
	$(DEPS_DIR)/cmark/build/src/*.h \
	$(SRC_DIR)/blog/Blog.h \
	$(SRC_DIR)/blog/converter.h \
	$(SRC_DIR)/blog/Template.h
OBJECTS += \
	$(BUILD_DIR)/blog/main.o \
	$(BUILD_DIR)/blog/Blog.o \
	$(BUILD_DIR)/blog/BlogConvert.o \
	$(BUILD_DIR)/blog/Template.o \
	$(BUILD_DIR)/blog/plaintext.o \
	$(BUILD_DIR)/blog/markdown.o

STATIC_LIBS += $(DEPS_DIR)/cmark/build/src/libcmark.a
CFLAGS += -I$(DEPS_DIR)/cmark/build/src

STATIC_LIBS += $(YAJL_BUILD_DIR)/lib/libyajl_s.a
CFLAGS += -I$(YAJL_BUILD_DIR)/include

STATIC_LIBS += $(DEPS_DIR)/lsmdb/liblmdb/liblmdb.a

STATIC_LIBS += $(DEPS_DIR)/uv/.libs/libuv.a

LIBS += -lcrypto -lpthread -lobjc -lm
ifeq ($(platform),linux)
LIBS += -lrt
endif

ifeq ($(DB),rocksdb)
  STATIC_LIBS += $(DEPS_DIR)/snappy/.libs/libsnappy.a
  LIBS += -lrocksdb
  LIBS += -lstdc++
  OBJECTS += $(BUILD_DIR)/db/db_base_rocksdb.o
else ifeq ($(DB),hyper)
  STATIC_LIBS += $(DEPS_DIR)/snappy/.libs/libsnappy.a
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
  CFLAGS += -I$(DEPS_DIR)/leveldb/include -I$(DEPS_DIR)/snappy/include
  STATIC_LIBS += $(DEPS_DIR)/leveldb/libleveldb.a $(DEPS_DIR)/snappy/.libs/libsnappy.a
  LIBS += -lstdc++
  OBJECTS += $(BUILD_DIR)/db/db_base_leveldb.o
endif

.DEFAULT_GOAL := all

.PHONY: all
all: $(BUILD_DIR)/stronglink #$(BUILD_DIR)/sln-markdown

$(BUILD_DIR)/stronglink: $(OBJECTS) $(STATIC_LIBS)
	@- mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARNINGS) $(OBJECTS) $(STATIC_LIBS) $(LIBS) -o $@

$(YAJL_BUILD_DIR)/include/yajl/*.h: | yajl
$(YAJL_BUILD_DIR)/lib/libyajl_s.a: | yajl
.PHONY: yajl
yajl:
	make yajl_s/fast -C $(DEPS_DIR)/yajl/build --no-print-directory

$(DEPS_DIR)/lsmdb/liblmdb/liblmdb.a: | mdb
.PHONY: mdb
mdb:
	make -C $(DEPS_DIR)/lsmdb/liblmdb --no-print-directory

$(DEPS_DIR)/leveldb/libleveldb.a: | leveldb
.PHONY: leveldb
leveldb:
	make -C $(DEPS_DIR)/leveldb --no-print-directory

$(DEPS_DIR)/snappy/.libs/libsnappy.a: | snappy
.PHONY: snappy
snappy:
	make -C $(DEPS_DIR)/snappy --no-print-directory

$(DEPS_DIR)/cmark/build/src/*.h: | cmark
$(DEPS_DIR)/cmark/build/src/libcmark.a: | cmark
.PHONY: cmark
cmark:
	make -C $(DEPS_DIR)/cmark --no-print-directory

$(DEPS_DIR)/uv/.libs/libuv.a: | libuv
.PHONY: libuv
libuv:
	make -C $(DEPS_DIR)/uv --no-print-directory
#	make -C $(DEPS_DIR)/uv check --no-print-directory

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

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.[cm] $(HEADERS)
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(WARNINGS) $< -o $@

#.PHONY: sln-markdown
#sln-markdown: $(BUILD_DIR)/sln-markdown

#$(BUILD_DIR)/sln-markdown: $(BUILD_DIR)/markdown_standalone.o $(BUILD_DIR)/http/QueryString.o $(SRC_DIR)/http/QueryString.h
#	@- mkdir -p $(dir $@)
#	$(CC) $(CFLAGS) $(WARNINGS) $^ $(DEPS_DIR)/cmark/build/src/libcmark.a -o $@

#$(BUILD_DIR)/markdown_standalone.o: $(SRC_DIR)/blog/markdown.c cmark
#	@- mkdir -p $(dir $@)
#	$(CC) -c $(CFLAGS) $(WARNINGS) -DMARKDOWN_STANDALONE $< -o $@

.PHONY: install
install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(PREFIX)/share/stronglink
	install $(BUILD_DIR)/stronglink $(DESTDIR)$(PREFIX)/bin
	#install $(BUILD_DIR)/sln-markdown $(DESTDIR)$(PREFIX)/bin
	cp -r $(ROOT_DIR)/res/blog $(DESTDIR)$(PREFIX)/share/stronglink
	chmod -R u=rwX,go=rX $(DESTDIR)$(PREFIX)/share/stronglink
# chmod -R u=rwX,go=rX
# user (root): read, write, execute if executable
# group, other: read, execute if executable

.PHONY: uninstall
uninstall:
	- rm $(DESTDIR)$(PREFIX)/bin/stronglink
	- rm $(DESTDIR)$(PREFIX)/bin/sln-markdown
	- rm -r $(DESTDIR)$(PREFIX)/share/stronglink

.PHONY: test
test: #$(BUILD_DIR)/tests/util/hash.test.run

.PHONY: $(BUILD_DIR)/tests/*.test.run
$(BUILD_DIR)/tests/%.test.run: $(BUILD_DIR)/tests/%.test
	$<

#$(BUILD_DIR)/tests/util/hash.test: $(BUILD_DIR)/util/hash.test.o $(BUILD_DIR)/util/hash.o $(BUILD_DIR)/deps/smhasher/MurmurHash3.o
#	@- mkdir -p $(dir $@)
#	$(CC) $(CFLAGS) $(WARNINGS) $^ -o $@

.PHONY: clean
clean:
	- rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean: clean
	- make distclean -C $(DEPS_DIR)/cmark
	- make clean -C $(DEPS_DIR)/leveldb
	- make clean -C $(DEPS_DIR)/lsmdb/liblmdb
	- make distclean -C $(DEPS_DIR)/snappy
	- make distclean -C $(DEPS_DIR)/uv
	- make distclean -C $(DEPS_DIR)/yajl

