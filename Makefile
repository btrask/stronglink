CC := clang

all: build/earthfs

build/earthfs: src/*.c
	@-mkdir -p $(dir $@)
	$(CC) -o $@ $^ -l ssl

clean:
	-rm -rf build/

.PHONY: all clean

