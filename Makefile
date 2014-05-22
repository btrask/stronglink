CC := clang

all: build/earthfs

build/earthfs: src/*.c
	@-mkdir -p $(dir $@)
	$(CC) -o $@ $^ -lssl -DSQLITE_ENABLE_FTS3 -DSQLITE_ENABLE_FTS3_PARENTHESIS

clean:
	-rm -rf build/

.PHONY: all clean

