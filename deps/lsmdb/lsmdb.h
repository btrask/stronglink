#include <stdint.h>
#include "liblmdb/lmdb.h"

#define MDB_RDWR 0

typedef struct LSMDB_env LSMDB_env;
typedef struct LSMDB_txn LSMDB_txn;
typedef struct LSMDB_cursor LSMDB_cursor;

typedef uint8_t LSMDB_level;

int lsmdb_env_create(LSMDB_env **const out);
int lsmdb_env_set_mapsize(LSMDB_env *const env, size_t const size);
int lsmdb_env_open(LSMDB_env *const env, char const *const name, unsigned const flags, mdb_mode_t const mode);
void lsmdb_env_close(LSMDB_env *const env);

int lsmdb_txn_begin(LSMDB_env *const env, LSMDB_txn *const parent, unsigned const flags, LSMDB_txn **const out);
int lsmdb_txn_commit(LSMDB_txn *const txn);
void lsmdb_txn_abort(LSMDB_txn *const txn);
void lsmdb_txn_reset(LSMDB_txn *const txn);
int lsmdb_txn_renew(LSMDB_txn *const txn);
int lsmdb_txn_get_flags(LSMDB_txn *const txn, unsigned *const flags);
int lsmdb_txn_cursor(LSMDB_txn *const txn, LSMDB_cursor **const out);

int lsmdb_get(LSMDB_txn *const txn, MDB_val const *const key, MDB_val *const data);
int lsmdb_put(LSMDB_txn *const txn, MDB_val const *const key, MDB_val const *const data, unsigned const flags);
int lsmdb_del(LSMDB_txn *const txn, MDB_val const *const key);
int lsmdb_cmp(LSMDB_txn *const txn, MDB_val const *const a, MDB_val const *const b);

int lsmdb_cursor_open(LSMDB_txn *const txn, LSMDB_cursor **const out);
void lsmdb_cursor_close(LSMDB_cursor *const cursor);
int lsmdb_cursor_renew(LSMDB_txn *const txn, LSMDB_cursor *const cursor);
int lsmdb_cursor_clear(LSMDB_cursor *const cursor);
LSMDB_txn *lsmdb_cursor_txn(LSMDB_cursor *const cursor);

int lsmdb_cursor_get(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, MDB_cursor_op const op);
int lsmdb_cursor_current(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data);
int lsmdb_cursor_seek(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir);
int lsmdb_cursor_first(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir);
int lsmdb_cursor_next(LSMDB_cursor *const cursor, MDB_val *const key, MDB_val *const data, int const dir);

int lsmdb_cursor_put(LSMDB_cursor *const cursor, MDB_val const *const key, MDB_val const *const data, unsigned const flags);
int lsmdb_cursor_del(LSMDB_cursor *const cursor);

int lsmdb_compact(LSMDB_txn *const txn, LSMDB_level const level, size_t const steps);
int lsmdb_autocompact(LSMDB_txn *const txn);

