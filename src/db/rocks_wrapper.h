#include <rocksdb/c.h>

/* Copyright (c) 2011 The LevelDB Authors. All rights reserved.
  Use of this source code is governed by a BSD-style license that can be
  found in the LICENSE file. See the AUTHORS file for names of contributors.

  Wrapper around RocksDB's special snowflake API.
*/

#ifndef STORAGE_LEVELDB_INCLUDE_C_H_
#define STORAGE_LEVELDB_INCLUDE_C_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* Exported types */

typedef struct rocksdb_t               leveldb_t;
typedef struct rocksdb_cache_t         leveldb_cache_t;
typedef struct rocksdb_comparator_t    leveldb_comparator_t;
typedef struct rocksdb_env_t           leveldb_env_t;
typedef struct rocksdb_filelock_t      leveldb_filelock_t;
typedef struct rocksdb_filterpolicy_t  leveldb_filterpolicy_t;
typedef struct rocksdb_iterator_t      leveldb_iterator_t;
typedef struct rocksdb_logger_t        leveldb_logger_t;
typedef struct rocksdb_options_t       leveldb_options_t;
typedef struct rocksdb_randomfile_t    leveldb_randomfile_t;
typedef struct rocksdb_readoptions_t   leveldb_readoptions_t;
typedef struct rocksdb_seqfile_t       leveldb_seqfile_t;
typedef struct rocksdb_snapshot_t      leveldb_snapshot_t;
typedef struct rocksdb_writablefile_t  leveldb_writablefile_t;
typedef struct rocksdb_writebatch_t    leveldb_writebatch_t;
typedef struct rocksdb_writeoptions_t  leveldb_writeoptions_t;

/* DB operations */

static leveldb_t* leveldb_open(
    const leveldb_options_t* options,
    const char* name,
    char** errptr) {
	return rocksdb_open(options, name, errptr);
}

static void leveldb_close(leveldb_t* db) {
	return rocksdb_close(db);
}

static void leveldb_put(
    leveldb_t* db,
    const leveldb_writeoptions_t* options,
    const char* key, size_t keylen,
    const char* val, size_t vallen,
    char** errptr) {
	return rocksdb_put(db, options, key, keylen, val, vallen, errptr);
}

static void leveldb_delete(
    leveldb_t* db,
    const leveldb_writeoptions_t* options,
    const char* key, size_t keylen,
    char** errptr) {
	return rocksdb_delete(db, options, key, keylen, errptr);
}

static void leveldb_write(
    leveldb_t* db,
    const leveldb_writeoptions_t* options,
    leveldb_writebatch_t* batch,
    char** errptr) {
	return rocksdb_write(db, options, batch, errptr);
}

/* Returns NULL if not found.  A malloc()ed array otherwise.
   Stores the length of the array in *vallen. */
static char* leveldb_get(
    leveldb_t* db,
    const leveldb_readoptions_t* options,
    const char* key, size_t keylen,
    size_t* vallen,
    char** errptr) {
	return rocksdb_get(db, options, key, keylen, vallen, errptr);
}

static leveldb_iterator_t* leveldb_create_iterator(
    leveldb_t* db,
    const leveldb_readoptions_t* options) {
	return rocksdb_create_iterator(db, options);
}

static const leveldb_snapshot_t* leveldb_create_snapshot(
    leveldb_t* db) {
	return rocksdb_create_snapshot(db);
}

static void leveldb_release_snapshot(
    leveldb_t* db,
    const leveldb_snapshot_t* snapshot) {
	return rocksdb_release_snapshot(db, snapshot);
}

/* Returns NULL if property name is unknown.
   Else returns a pointer to a malloc()-ed null-terminated value. */
static char* leveldb_property_value(
    leveldb_t* db,
    const char* propname) {
	return rocksdb_property_value(db, propname);
}

static void leveldb_approximate_sizes(
    leveldb_t* db,
    int num_ranges,
    const char* const* range_start_key, const size_t* range_start_key_len,
    const char* const* range_limit_key, const size_t* range_limit_key_len,
    uint64_t* sizes) {
	return rocksdb_approximate_sizes(db, num_ranges, range_start_key, range_start_key_len, range_limit_key, range_limit_key_len, sizes);
}

static void leveldb_compact_range(
    leveldb_t* db,
    const char* start_key, size_t start_key_len,
    const char* limit_key, size_t limit_key_len) {
	return rocksdb_compact_range(db, start_key, start_key_len, limit_key, limit_key_len);
}

/* Management operations */

static void leveldb_destroy_db(
    const leveldb_options_t* options,
    const char* name,
    char** errptr) {
	return rocksdb_destroy_db(options, name, errptr);
}

static void leveldb_repair_db(
    const leveldb_options_t* options,
    const char* name,
    char** errptr) {
	return rocksdb_repair_db(options, name, errptr);
}

/* Iterator */

static void leveldb_iter_destroy(leveldb_iterator_t*  iter) {
	return rocksdb_iter_destroy(iter);
}
static unsigned char leveldb_iter_valid(const leveldb_iterator_t* iter) {
	return rocksdb_iter_valid(iter);
}
static void leveldb_iter_seek_to_first(leveldb_iterator_t* iter) {
	return rocksdb_iter_seek_to_first(iter);
}
static void leveldb_iter_seek_to_last(leveldb_iterator_t* iter) {
	return rocksdb_iter_seek_to_last(iter);
}
static void leveldb_iter_seek(leveldb_iterator_t* iter, const char* k, size_t klen) {
	return rocksdb_iter_seek(iter, k, klen);
}
static void leveldb_iter_next(leveldb_iterator_t* iter) {
	return rocksdb_iter_next(iter);
}
static void leveldb_iter_prev(leveldb_iterator_t* iter) {
	return rocksdb_iter_prev(iter);
}
static const char* leveldb_iter_key(const leveldb_iterator_t* iter, size_t* klen) {
	return rocksdb_iter_key(iter, klen);
}
static const char* leveldb_iter_value(const leveldb_iterator_t* iter, size_t* vlen) {
	return rocksdb_iter_value(iter, vlen);
}
static void leveldb_iter_get_error(const leveldb_iterator_t* iter, char** errptr) {
	return rocksdb_iter_get_error(iter, errptr);
}

/* Write batch */

static leveldb_writebatch_t* leveldb_writebatch_create() {
	return rocksdb_writebatch_create();
}
static void leveldb_writebatch_destroy(leveldb_writebatch_t* wb) {
	return rocksdb_writebatch_destroy(wb);
}
static void leveldb_writebatch_clear(leveldb_writebatch_t* wb) {
	return rocksdb_writebatch_clear(wb);
}
static void leveldb_writebatch_put(
    leveldb_writebatch_t* wb,
    const char* key, size_t klen,
    const char* val, size_t vlen) {
	return rocksdb_writebatch_put(wb, key, klen, val, vlen);
}
static void leveldb_writebatch_delete(
    leveldb_writebatch_t* wb,
    const char* key, size_t klen) {
	return rocksdb_writebatch_delete(wb, key, klen);
}
static void leveldb_writebatch_iterate(
    leveldb_writebatch_t* wb,
    void* state,
    void (*put)(void*, const char* k, size_t klen, const char* v, size_t vlen),
    void (*deleted)(void*, const char* k, size_t klen)) {
	return rocksdb_writebatch_iterate(wb, state, put, deleted);
}

/* Options */

static leveldb_options_t* leveldb_options_create() {
	return rocksdb_options_create();
}
static void leveldb_options_destroy(leveldb_options_t* opts) {
	return rocksdb_options_destroy(opts);
}
static void leveldb_options_set_comparator(
    leveldb_options_t* opts,
    leveldb_comparator_t* cmp) {
	return rocksdb_options_set_comparator(opts, cmp);
}
static void leveldb_options_set_filter_policy(
    leveldb_options_t* opts,
    leveldb_filterpolicy_t* fp) {
//	return rocksdb_options_set_filter_policy(opts, fp);
}
static void leveldb_options_set_create_if_missing(
    leveldb_options_t* opts, unsigned char flag) {
	return rocksdb_options_set_create_if_missing(opts, flag);
}
static void leveldb_options_set_error_if_exists(
    leveldb_options_t* opts, unsigned char flag) {
	return rocksdb_options_set_error_if_exists(opts, flag);
}
static void leveldb_options_set_paranoid_checks(
    leveldb_options_t* opts, unsigned char flag) {
	return rocksdb_options_set_paranoid_checks(opts, flag);
}
static void leveldb_options_set_env(leveldb_options_t* opts, leveldb_env_t* env) {
	return rocksdb_options_set_env(opts, env);
}
static void leveldb_options_set_info_log(leveldb_options_t* opts, leveldb_logger_t* logger) {
	return rocksdb_options_set_info_log(opts, logger);
}
static void leveldb_options_set_write_buffer_size(leveldb_options_t* opts, size_t size) {
	return rocksdb_options_set_write_buffer_size(opts, size);
}
static void leveldb_options_set_max_open_files(leveldb_options_t* opts, int max) {
	return rocksdb_options_set_max_open_files(opts, max);
}
static void leveldb_options_set_cache(leveldb_options_t* opts, leveldb_cache_t* cache) {
//	return rocksdb_options_set_cache(opts, cache);
}
static void leveldb_options_set_block_size(leveldb_options_t* opts, size_t size) {
//	return rocksdb_options_set_block_size(opts, size);
}
static void leveldb_options_set_block_restart_interval(leveldb_options_t* opts, int interval) {
//	return rocksdb_options_set_block_restart_interval(opts, interval);
}

enum {
  leveldb_no_compression = 0,
  leveldb_snappy_compression = 1
};
static void leveldb_options_set_compression(leveldb_options_t* opts, int val) {
	return rocksdb_options_set_compression(opts, val);
}

/* Comparator */

static leveldb_comparator_t* leveldb_comparator_create(
    void* state,
    void (*destructor)(void*),
    int (*compare)(
        void*,
        const char* a, size_t alen,
        const char* b, size_t blen),
    const char* (*name)(void*)) {
	return rocksdb_comparator_create(state, destructor, compare, name);
}
static void leveldb_comparator_destroy(leveldb_comparator_t* comparator) {
	return rocksdb_comparator_destroy(comparator);
}

/* Filter policy */

static leveldb_filterpolicy_t* leveldb_filterpolicy_create(
    void* state,
    void (*destructor)(void*),
    char* (*create_filter)(
        void*,
        const char* const* key_array, const size_t* key_length_array,
        int num_keys,
        size_t* filter_length),
    unsigned char (*key_may_match)(
        void*,
        const char* key, size_t length,
        const char* filter, size_t filter_length),
    const char* (*name)(void*)) {
//	return rocksdb_filterpolicy_create(state, destructor, create_filter, key_may_match, name);
	return (leveldb_filterpolicy_t *)-1;
}
static void leveldb_filterpolicy_destroy(leveldb_filterpolicy_t* fp) {
//	return rocksdb_filterpolicy_destroy(fp);
}

static leveldb_filterpolicy_t* leveldb_filterpolicy_create_bloom(
    int bits_per_key) {
//	return rocksdb_filterpolicy_create_bloom(bits_per_key);
	return (leveldb_filterpolicy_t *)-1;
}

/* Read options */

static leveldb_readoptions_t* leveldb_readoptions_create() {
	return rocksdb_readoptions_create();
}
static void leveldb_readoptions_destroy(leveldb_readoptions_t* opts) {
	return rocksdb_readoptions_destroy(opts);
}
static void leveldb_readoptions_set_verify_checksums(
    leveldb_readoptions_t* opts,
    unsigned char flag) {
	return rocksdb_readoptions_set_verify_checksums(opts, flag);
}
static void leveldb_readoptions_set_fill_cache(
    leveldb_readoptions_t* opts, unsigned char flag) {
	return rocksdb_readoptions_set_fill_cache(opts, flag);
}
static void leveldb_readoptions_set_snapshot(
    leveldb_readoptions_t* opts,
    const leveldb_snapshot_t* snapshot) {
	return rocksdb_readoptions_set_snapshot(opts, snapshot);
}

/* Write options */

static leveldb_writeoptions_t* leveldb_writeoptions_create() {
	return rocksdb_writeoptions_create();
}
static void leveldb_writeoptions_destroy(leveldb_writeoptions_t* opts) {
	return rocksdb_writeoptions_destroy(opts);
}
static void leveldb_writeoptions_set_sync(
    leveldb_writeoptions_t* opts, unsigned char flag) {
	return rocksdb_writeoptions_set_sync(opts, flag);
}

/* Cache */

static leveldb_cache_t* leveldb_cache_create_lru(size_t capacity) {
//	return rocksdb_create_cache_lru(capacity);
	return (leveldb_cache_t *)-1;
}
static void leveldb_cache_destroy(leveldb_cache_t* cache) {
//	return rocksdb_cache_destroy(cache);
}

/* Env */

static leveldb_env_t* leveldb_create_default_env() {
	return rocksdb_create_default_env();
}
static void leveldb_env_destroy(leveldb_env_t* env) {
	return rocksdb_env_destroy(env);
}

/* Utility */

/* Calls free(ptr).
   REQUIRES: ptr was malloc()-ed and returned by one of the routines
   in this file.  Note that in certain cases (typically on Windows), you
   may need to call this routine instead of free(ptr) to dispose of
   malloc()-ed memory returned by this library. */
static void leveldb_free(void* ptr) {
	return rocksdb_free(ptr);
}

/* Return the major version number for this release. */
static int leveldb_major_version() {
	return 1;
}

/* Return the minor version number for this release. */
static int leveldb_minor_version() {
	return 2;
}

#ifdef __cplusplus
}  /* end extern "C" */
#endif

#endif  /* STORAGE_LEVELDB_INCLUDE_C_H_ */
