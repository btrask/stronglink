/* LSMDB - LSM-tree wrapper for MDB
 * 
 * Things generally work like they do in MDB. Differences and
 * limitations:
 * 
 * - LSMDB only stores keys without separate payloads. The reason is
 *   that under the hood, LSMDB uses MDB's dupsort to implement
 *   nested b-trees. `Data` parameters are left in the API for future
 *   compatibility. Returned data is always zero length.
 * - For the above reason, lsmdb_get() and the like match the first
 *   key with the given prefix, not necessarily the whole key.
 * - Instead of MDB_cursor_ops, smaller functions are used. Most of
 *   them take a `dir` parameter, where >=1 indicates forward and
 *   <=1 indicates reverse. Some of them also accept 0, meaning exact
 *   match.
 * - As in every write-optimized data structure, lsmdb_put() with
 *   MDB_NOOVERWRITE involves an extra get(). Faster to gracefully
 *   handle replacements/duplicates whenever possible.
 * - You should generally call lsmdb_autocompact() after performing
 *   a batch of inserts (e.g. right before commit). Currently you
 *   have to track which DBIs you touched and explicitly compact all
 *   of them whenever appropriate.
 * - When you do a compaction, you have to renew() any open cursors
 *   in the same transaction, if you intend to keep using them.
 * - Doing large compactions in child transactions might result in
 *   MDB_TXN_FULL.
 * - lsmdb_del() does a full delete, removing every key with the
 *   specified prefix from every level of the LSM-tree. For faster
 *   deletions, put() a sentinel value instead.
 * - Dupsort is used internally, so it isn't available for clients.
 * - Compactions don't currently use MDB_APPEND. It's worth trying
 *   but I'm not sure it would be a big improvement.
 * 
 * 
 * Copyright 2014 Ben Trask
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */

#include <assert.h>
#include <stdint.h>
#include "../deps/liblmdb/lmdb.h"

typedef uint8_t LSMDB_level;
typedef MDB_dbi LSMDB_dbi;
typedef struct LSMDB_cursor LSMDB_cursor;

int lsmdb_dbi_open(MDB_txn *const txn, char const *const name, unsigned const flags, LSMDB_dbi *const dbi);
int lsmdb_set_compare(MDB_txn *const txn, LSMDB_dbi const dbi, MDB_cmp_func *const cmp);

int lsmdb_get(MDB_txn *const txn, LSMDB_dbi const dbi, MDB_val *const key, MDB_val *const data);
int lsmdb_put(MDB_txn *const txn, LSMDB_dbi const dbi, MDB_val *const key, MDB_val *const data, unsigned const flags); // MDB_NOOVERWRITE
int lsmdb_del(MDB_txn *const txn, LSMDB_dbi const dbi, MDB_val *const key);

int lsmdb_cursor_open(MDB_txn *const txn, LSMDB_dbi const dbi, LSMDB_cursor **const cursor);
void lsmdb_cursor_close(LSMDB_cursor *const cursor);
int lsmdb_cursor_renew(MDB_txn *const txn, LSMDB_cursor *const cursor);

int lsmdb_cursor_get(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data); // Equivalent to MDB_GET_CURRENT
int lsmdb_cursor_seek(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir); // Equivalent to MDB_SET or MDB_SET_RANGE
int lsmdb_cursor_step(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir); // Equivalent to MDB_NEXT or MDB_PREV
int lsmdb_cursor_start(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir); // Equivalent to MDB_FIRST or MDB_LAST

// TODO
//int lsmdb_cursor_put(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, unsigned const flags);
//int lsmdb_cursor_del(LSMDB_cursor *const cursor, MDB_val *const key);

int lsmdb_autocompact(MDB_txn *const txn, LSMDB_dbi const dbi, char const *const name);

