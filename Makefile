CC := clang

# TODO: Isn't there a way to have it find everything automatically?

HEADERS := \
	src/common.h \
	src/EarthFS.h \
	src/HTTPServer.h \
	deps/http_parser/http_parser.h

SOURCES := \
	src/main.c \
	src/EFSHasher.c \
	src/EFSRepo.c \
	src/EFSSession.c \
	src/EFSSubmission.c \
	src/HTTPServer.c \
	deps/http_parser/http_parser.c

all: build/earthfs

build/earthfs: $(SOURCES) $(HEADERS)
	@-mkdir -p $(dir $@)
	$(CC) -o $@ $(SOURCES) -lssl -lpthread -DSQLITE_ENABLE_FTS3 -DSQLITE_ENABLE_FTS3_PARENTHESIS -Werror

clean:
	-rm -rf build/

.PHONY: all clean

