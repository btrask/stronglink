#include <stdio.h>
#include <stdlib.h>
#include "hash.h"

#define test_assert(x, fmt, ...) ({ \
	__typeof__(x) const __x = (x); \
	if(!__x) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); \
		fprintf(stderr, (fmt), ##__VA_ARGS__); \
		fprintf(stderr, "\n"); \
		exit(1); \
	} \
})

#define TEST_SIZE 100

int main(void) {
	hash_salt = 0;

	int rc, i, j;
	size_t x;
	hash_t hash[1];
	rc = hash_init(hash, TEST_SIZE, sizeof(int));
	test_assert(rc >= 0, "hash_init failed");
	for(i = 1; i <= TEST_SIZE; i++) {
		x = hash_set(hash, (char const *)&i);
		test_assert(HASH_NOTFOUND != x, "hash_set failed (%d)", i);
	}
	for(i = 1; i <= TEST_SIZE; i++) {
		x = hash_get(hash, (char const *)&i);
		test_assert(HASH_NOTFOUND != x, "hash_get failed (%d)", i);
	}
	for(i = 1; i <= TEST_SIZE; i++) {
		x = hash_get(hash, (char const *)&i);
		test_assert(HASH_NOTFOUND != x, "Couldn't find value to remove (%d, expected at %zu)", i, hash_func(hash, (char const *)&i));
//		fprintf(stderr, "deleting %d at %zu\n", i, x);
		(void)hash_del_keyonly(hash, x);
	}
	for(i = 1; i <= TEST_SIZE; i++) {
		x = hash_get(hash, (char const *)&i);
		test_assert(HASH_NOTFOUND == x, "Deleted value still present (%d)", i);
	}
	hash_destroy(hash);
	return 0;
}


