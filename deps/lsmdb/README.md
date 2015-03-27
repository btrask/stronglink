# LSMDB

LSMDB is a proof-of-concept LSM-tree built on top of MDB (AKA LMDB). It only interacts with MDB using the standard MDB API, and doesn't require any changes to the MDB code or file format.

LSMDB provides its own interface to the application, which is similar to MDB except for the following differences:
- It doesn't support MDB's DBIs, which are used internally as levels of the LSM-tree. Additionally, using separate DBIs would actually hurt the performance of an LSM-tree, because the goal is to combine as many writes as possible.
- It doesn't support MDB's dup-sort.
- Some minor APIs simply aren't implemented/exposed yet.
- It uses scalars instead of enums to specify scan directions. For example, `lsmdb_cursor_next()` accepts `+1` (next) or `-1` (previous). A wrapper supporting existing `MDB_cursor_op`s is included.
- Each transaction keeps a cursor for cases where it needs to use one internally, to avoid frequently allocating and freeing memory. The cursor is also available to clients in case they want to use it briefly.

LSMDB has many shortcomings and isn't intended to be production-ready. However, I think it demonstrates a viable approach for a simple, reliable and high-performance LSM-tree in around 800 lines of code (not counting the code for MDB, which itself is quite small).

Improvements I would desire in a production version:
- Use two instances of MDB internally, one for level 0 and one for levels 1+, in order to have fully concurrent compaction.
	- Reads read-lock both `MDB_env`s;
	- Writes write-lock the level 0 env and read-lock level 1+;
	- Compactions write-lock 1+ and read-lock 0.
- If MDB provided efficient ranged deletion, incremental compaction would be extremely fast and easy (not relying on 2MB chunks like LevelDB or requiring 2X space overhead like LSMDB currently).
- If MDB provided an efficient way to rename DBIs, LSMDB would need to do less work to track level meta-data.
- During periods of no writes, LSMDB should continue compacting into a single level in order to reach full MDB read performance.
- Support `MDB_APPEND` by writing directly into the highest level.
- Optionally support bloom filters to minimize disk seeks.
- Provide equivalents to `mdb_dump`, `mdb_load`, `mdb_stat`, etc.
- Obviously use more error checking and general testing. If you get an internal assertion failure while writing data, you may need to increase the map size.

Notes regarding the benchmarks:
- This is a write-only benchmark with a single writer.
- Each database is configured to use synchronous writes (full durability).
- Snappy compression is disabled in LevelDB to be fair to the other databases.
- By default, 1000 values are written per transaction. This is realistic for applications and prevents micro-benchmarking transaction speed (which should be the same for every competent database).
- Keys are written in pseudo-random order (sequential integers in reversed byte order).
- Key size is 8 bytes and value size is 16 bytes, which is small but not unreasonable. Large values should never be stored in any database due to locking, but LSM-trees take it even worse due to compaction.
- Testing was done on a laptop with Samsung 840 SSD under Xen with full disk encryption. Similar results were obtained on a laptop with an older Kingspec SSD without Xen or encryption.

2,000,000 values, random order:
```
$ time ./test_leveldb
test_leveldb.c

real	0m48.039s
user	0m13.577s
sys	0m3.937s
$ time ./test_lsmdb
test_lsmdb.c

real	1m12.958s
user	0m14.183s
sys	0m4.691s
$ time ./test_mdb
test_mdb.c

real	3m2.929s
user	0m10.282s
sys	0m21.105s
```

As you can see, LSMDB is a big improvement over plain MDB for this workload, although it still can't catch up to LevelDB (even without compression).

Bonus test, MDB with _sequential_ writes:
```
$ time ./test_mdb
test_mdb.c

real	0m47.488s
user	0m1.847s
sys	0m1.520s
```

As you can see, in this test MDB's sequential write performance (using `MDB_APPEND`) is merely on par with LevelDB's random write performance. Given that LSMDB heavily relies on `MDB_APPEND` for fast compaction, it's no surprise that it can't beat LevelDB.

I'm not sure why sequential writes with `MDB_APPEND` are as slow as they are, but I'd speculate it's due to MDB's higher transaction overhead. After each transaction MDB has to update the header to mark the active root, and the update can never be combined with other writes. This seems like the biggest downside of MDB's otherwise brilliant design.

The other big potential improvement for LSMDB is if it supported concurrent compaction, then perhaps it could make better use of disk bandwidth.

At any rate, LSMDB's ability to speed up MDB while using MDB as-is may point the way for future write-optimized database engines.

LSMDB is provided under the OpenLDAP Public License, just like MDB.

