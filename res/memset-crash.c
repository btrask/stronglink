/*
Build with:

clang -O0 -g memset-crash.c ../deps/sqlite/sqlite3.c ../deps/libco/x86.c -Wno-parentheses -DSQLITE_OMIT_LOAD_EXTENSION -lpthread

Result:
	Program received signal SIGSEGV, Segmentation fault.
	findInodeInfo (pFile=0x8105990, ppInode=0x8105998)
		at ../deps/sqlite/sqlite3.c:25181
	25181	  memset(&fileId, 0, sizeof(fileId));

Workarounds:
- GCC
- Clang with -fno-builtin
- libco/sjlj or libco/ucontext

Makes no difference:
- Stack size
- -DSQLITE_THREADSAFE=0
- SQLITE_OPEN_NOMUTEX, SQLITE_OPEN_FULLMUTEX

I didn't manage to reproduce the crash by calling memset without using SQLite. Sorry.

Tested under Linux with Clang 3.3.
*/
#include <stdlib.h>
#include "../deps/libco/libco.h"
#include "../deps/sqlite/sqlite3.h"

void test(void) {
	sqlite3 *db;
	sqlite3_open_v2(
		"/tmp/memset-crash.db",
		&db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
		NULL
	);
}

int main(int const argc, char const *const *const argv) {
	co_switch(co_create(1024 * 1000 * sizeof(void *) / 4, test));
	return EXIT_SUCCESS;
}

