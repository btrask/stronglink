CFLAGS += -Wall -Werror -Wno-unused -std=gnu99

.phony: all
all: test_lsmdb test_leveldb test_mdb

test_lsmdb: test_lsmdb.c test.h lsmdb.c lsmdb.h
	cd liblmdb && make
	$(CC) $(CFLAGS) test_lsmdb.c lsmdb.c liblmdb/liblmdb.a -lpthread -lm -o $@

test_leveldb: test_leveldb.c test.h
	cd snappy && make
	cd leveldb && make
	$(CC) $(CFLAGS) -Ileveldb/include test_leveldb.c leveldb/libleveldb.a snappy/.libs/libsnappy.a -lstdc++ -lpthread -o $@

test_mdb: test_mdb.c test.h
	cd liblmdb && make
	$(CC) $(CFLAGS) test_mdb.c liblmdb/liblmdb.a -lpthread -lm -o $@

.phony: clean
clean:
	- cd liblmdb && make clean
	- rm test_lsmdb
	- cd snappy && make clean
	- cd leveldb && make clean
	- rm test_leveldb
	- rm test_mdb
