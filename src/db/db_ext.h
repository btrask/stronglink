// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "db_base.h"

int db_get(DB_txn *const txn, DB_val *const key, DB_val *const data);
int db_put(DB_txn *const txn, DB_val *const key, DB_val *const data, unsigned const flags);

typedef struct {
	DB_val min[1];
	DB_val max[1];
} DB_range;

int db_cursor_seekr(DB_cursor *const cursor, DB_range const *const range, DB_val *const key, DB_val *const data, int const dir);
int db_cursor_firstr(DB_cursor *const cursor, DB_range const *const range, DB_val *const key, DB_val *const data, int const dir);
int db_cursor_nextr(DB_cursor *const cursor, DB_range const *const range, DB_val *const key, DB_val *const data, int const dir);

