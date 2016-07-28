# Copyright 2014-2015 Ben Trask
# MIT licensed (see LICENSE for details)

.SUFFIXES:
.SECONDARY:

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

CFLAGS += -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=500
CFLAGS += -g -fno-omit-frame-pointer
CFLAGS += -DINSTALL_PREFIX=\"$(PREFIX)\"
CFLAGS += -fstack-protector
CFLAGS += -DHAVE_TIMEGM -DMAP_ANON -I$(DEPS_DIR)/libasync/deps/libressl-portable/include/compat

WARNINGS := -Werror -Wall -Wextra -Wunused -Wuninitialized -Wvla

# TODO: Unsupported under Clang.
#WARNINGS += -Wlogical-op

# Disabled because it causes a lot of problems on Raspbian (GCC 4.6.3)
# without much perceivable benefit.
#WARNINGS += -Wshadow

# TODO: Useful with GCC but Clang doesn't like it.
#WARNINGS += -Wmaybe-uninitialized

# Causes all string literals to be marked const.
# This would be way too annoying if we don't use const everywhere already.
# The only problem is uv_buf_t, which is const sometimes and not others.
WARNINGS += -Wwrite-strings

# A function's interface is an abstraction and shouldn't strictly reflect
# its implementation. I don't believe in cluttering the code with UNUSED(X).
WARNINGS += -Wno-unused-parameter

# Seems too noisy for static functions in headers.
WARNINGS += -Wno-unused-function

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

# Checks that format strings are literals amongst other things.
WARNINGS += -Wformat=2

ifdef RELEASE
CFLAGS += -O2
#CFLAGS += -DNDEBUG
else
CFLAGS += -O2
CFLAGS += -DHTTP_PARSER_DEBUG
# TODO: We want to enable this but it adds a dependency on libubsan.
#CFLAGS += -fsanitize=undefined
endif

# Generic library code
OBJECTS := \
	$(BUILD_DIR)/src/SLNRepo.o \
	$(BUILD_DIR)/src/SLNSessionCache.o \
	$(BUILD_DIR)/src/SLNSession.o \
	$(BUILD_DIR)/src/SLNSubmission.o \
	$(BUILD_DIR)/src/SLNSubmissionMeta.o \
	$(BUILD_DIR)/src/SLNHasher.o \
	$(BUILD_DIR)/src/SLNSync.o \
	$(BUILD_DIR)/src/SLNPull.o \
	$(BUILD_DIR)/src/SLNServer.o \
	$(BUILD_DIR)/src/filter/SLNFilter.o \
	$(BUILD_DIR)/src/filter/SLNFilterExt.o \
	$(BUILD_DIR)/src/filter/SLNIndirectFilter.o \
	$(BUILD_DIR)/src/filter/SLNDirectFilter.o \
	$(BUILD_DIR)/src/filter/SLNLinksToFilter.o \
	$(BUILD_DIR)/src/filter/SLNCollectionFilter.o \
	$(BUILD_DIR)/src/filter/SLNNegationFilter.o \
	$(BUILD_DIR)/src/filter/SLNMetaFileFilter.o \
	$(BUILD_DIR)/src/filter/SLNJSONFilterParser.o \
	$(BUILD_DIR)/src/filter/SLNUserFilterParser.o \
	$(BUILD_DIR)/src/util/fts.o \
	$(BUILD_DIR)/src/util/pass.o \
	$(BUILD_DIR)/src/util/strext.o \
	$(BUILD_DIR)/deps/crypt_blowfish/crypt_blowfish.o \
	$(BUILD_DIR)/deps/crypt_blowfish/crypt_gensalt.o \
	$(BUILD_DIR)/deps/crypt_blowfish/wrapper.o \
	$(BUILD_DIR)/deps/crypt_blowfish/x86.S.o \
	$(BUILD_DIR)/deps/fts3/fts3_porter.o \
	$(BUILD_DIR)/deps/libasync/deps/libressl-portable/crypto/compat/reallocarray.o \
	$(BUILD_DIR)/deps/libasync/deps/libressl-portable/crypto/compat/strlcat.o \
	$(BUILD_DIR)/deps/libasync/deps/libressl-portable/crypto/compat/strlcpy.o \
	$(BUILD_DIR)/deps/smhasher/MurmurHash3.o

# TODO: Ugly.
ifeq ($(platform),macosx)
OBJECTS += $(BUILD_DIR)/deps/memorymapping/src/fmemopen.o
endif
ifeq ($(platform),bsd)
OBJECTS += $(BUILD_DIR)/deps/memorymapping/src/fmemopen.o
endif

# Blog server
OBJECTS += \
	$(BUILD_DIR)/src/blog/main.o \
	$(BUILD_DIR)/src/blog/Blog.o \
	$(BUILD_DIR)/src/blog/BlogConvert.o \
	$(BUILD_DIR)/src/blog/RSSServer.o \
	$(BUILD_DIR)/src/blog/Template.o \
	$(BUILD_DIR)/src/blog/plaintext.o \
	$(BUILD_DIR)/src/blog/markdown.o \
	$(BUILD_DIR)/deps/content-disposition/content-disposition.o

STATIC_LIBS += $(DEPS_DIR)/cmark/build/src/libcmark.a
CFLAGS += -I$(DEPS_DIR)/cmark/build/src

STATIC_LIBS += $(YAJL_BUILD_DIR)/lib/libyajl_s.a
CFLAGS += -I$(YAJL_BUILD_DIR)/include

STATIC_LIBS += $(DEPS_DIR)/libasync/build/libasync.a
CFLAGS += -I$(DEPS_DIR)/libasync/include
CFLAGS += -iquote $(DEPS_DIR)/libasync/deps

STATIC_LIBS += $(DEPS_DIR)/libasync/deps/libressl-portable/tls/.libs/libtls.a
STATIC_LIBS += $(DEPS_DIR)/libasync/deps/libressl-portable/ssl/.libs/libssl.a
STATIC_LIBS += $(DEPS_DIR)/libasync/deps/libressl-portable/crypto/.libs/libcrypto.a
CFLAGS += -I$(DEPS_DIR)/libasync/deps/libressl-portable/include

STATIC_LIBS += $(DEPS_DIR)/libasync/deps/uv/.libs/libuv.a

STATIC_LIBS += $(DEPS_DIR)/libkvstore/build/libkvstore.a
STATIC_LIBS += $(DEPS_DIR)/libkvstore/deps/liblmdb/liblmdb.a
CFLAGS += -I$(DEPS_DIR)/libkvstore/include
CFLAGS += -iquote $(DEPS_DIR)/libkvstore/deps

LIBS += -lpthread -lobjc -lm
ifeq ($(platform),linux)
LIBS += -lrt
endif

ifdef USE_VALGRIND
CFLAGS += -DSLN_USE_VALGRIND
endif


.DEFAULT_GOAL := all

.PHONY: all
all: $(BUILD_DIR)/stronglink #$(BUILD_DIR)/sln-markdown

$(BUILD_DIR)/stronglink: $(OBJECTS) $(STATIC_LIBS)
	@- mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARNINGS) $(OBJECTS) $(STATIC_LIBS) $(LIBS) -o $@

$(YAJL_BUILD_DIR)/lib/libyajl_s.a: | yajl
.PHONY: yajl
yajl:
	$(MAKE) yajl_s/fast -C $(DEPS_DIR)/yajl/build --no-print-directory

$(DEPS_DIR)/cmark/build/src/*.h: | cmark
$(DEPS_DIR)/cmark/build/src/libcmark.a: | cmark
.PHONY: cmark
cmark:
	$(MAKE) -C $(DEPS_DIR)/cmark --no-print-directory

# TODO: Have libasync bundle these directly.
$(DEPS_DIR)/libasync/build/libasync.a: | libasync
$(DEPS_DIR)/libasync/deps/libressl-portable/tls/.libs/libtls.a: | libasync
$(DEPS_DIR)/libasync/deps/libressl-portable/ssl/.libs/libssl.a: | libasync
$(DEPS_DIR)/libasync/deps/libressl-portable/crypto/.libs/libcrypto.a: | libasync
$(DEPS_DIR)/libasync/deps/uv/.libs/libuv.a: | libasync
.PHONY: libasync
libasync:
	$(MAKE) -C $(DEPS_DIR)/libasync --no-print-directory

$(DEPS_DIR)/libkvstore/build/libkvstore.a: | libkvstore
$(DEPS_DIR)/libkvstore/deps/liblmdb/liblmdb.a: | libkvstore
.PHONY: libkvstore
libkvstore:
	$(MAKE) -C $(DEPS_DIR)/libkvstore --no-print-directory

$(BUILD_DIR)/deps/crypt_blowfish/%.S.o: $(DEPS_DIR)/crypt_blowfish/%.S
	@- mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)/deps/memorymapping/src/fmemopen.o: $(DEPS_DIR)/memorymapping/src/fmemopen.c $(DEPS_DIR)/memorymapping/src/fmemopen.h
	@- mkdir -p $(dir $@)
	$(CC) -c -std=c99 -o $@ $<

$(BUILD_DIR)/deps/%.o: $(DEPS_DIR)/%.c
	@- mkdir -p $(dir $@)
	@- mkdir -p $(dir $(BUILD_DIR)/h/deps/$*.d)
	$(CC) -c $(CFLAGS) $(WARNINGS) -MMD -MP -MF $(BUILD_DIR)/h/deps/$*.d -o $@ $<

$(BUILD_DIR)/deps/%.o: $(DEPS_DIR)/%.cpp
	@- mkdir -p $(dir $@)
	@- mkdir -p $(dir $(BUILD_DIR)/h/deps/$*.d)
	$(CXX) -c $(CXXFLAGS) $(WARNINGS) -MMD -MP -MF $(BUILD_DIR)/h/deps/$*.d -o $@ $<

# We use order-only dependencies to force headers to be built first.
# cc -M* doesn't help during initial build.
$(BUILD_DIR)/src/%.o: $(SRC_DIR)/%.[cm] | libasync libkvstore yajl
	@- mkdir -p $(dir $@)
	@- mkdir -p $(dir $(BUILD_DIR)/h/src/$*.d)
	$(CC) -c $(CFLAGS) -I$(SRC_DIR) $(WARNINGS) -MMD -MP -MF $(BUILD_DIR)/h/src/$*.d -o $@ $<

# TODO: Find files in subdirectories without using shell?
-include $(shell find $(BUILD_DIR)/h -name "*.d")

#.PHONY: sln-markdown
#sln-markdown: $(BUILD_DIR)/sln-markdown

#$(BUILD_DIR)/sln-markdown: $(BUILD_DIR)/markdown_standalone.o $(BUILD_DIR)/http/QueryString.o $(SRC_DIR)/http/QueryString.h
#	@- mkdir -p $(dir $@)
#	$(CC) $(CFLAGS) $(WARNINGS) $^ $(DEPS_DIR)/cmark/build/src/libcmark.a -o $@

#$(BUILD_DIR)/markdown_standalone.o: $(SRC_DIR)/blog/markdown.c cmark
#	@- mkdir -p $(dir $@)
#	$(CC) -c $(CFLAGS) $(WARNINGS) -DMARKDOWN_STANDALONE -o $@ $<

ifeq ($(platform),linux)
SETCAP := setcap "CAP_NET_BIND_SERVICE=+ep" $(DESTDIR)$(PREFIX)/bin/stronglink
endif

.PHONY: install
install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(PREFIX)/share/stronglink
	install $(BUILD_DIR)/stronglink $(DESTDIR)$(PREFIX)/bin
	$(SETCAP)
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
	- $(MAKE) distclean -C $(DEPS_DIR)/libasync
	- $(MAKE) distclean -C $(DEPS_DIR)/libkvstore
	- $(MAKE) distclean -C $(DEPS_DIR)/cmark
	- $(MAKE) distclean -C $(DEPS_DIR)/yajl

