// All we need for the FTS Porter stemmer.

#define sqlite3_malloc malloc
#define sqlite3_free free
#define sqlite3_realloc realloc

#define SQLITE_OK 0
#define SQLITE_NOMEM 7
#define SQLITE_DONE 101

