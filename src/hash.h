#include <stdint.h>

#define HASH_NOTFOUND (SIZE_MAX-1)

#define HASH_SALT_SIZE 16
extern char hash_salt[HASH_SALT_SIZE]; /* Set this first */

typedef struct {
	size_t count;
	size_t keylen;
	char *keys;
} hash_t;

int hash_init(hash_t *const hash, size_t const count, size_t const keylen);
void hash_destroy(hash_t *const hash);

/* Returns index of key (bring your own payload storage) */
size_t hash_get(hash_t *const hash, char const *const key);
size_t hash_set(hash_t *const hash, char const *const key);

/* Updates external data array for you (elements must be fixed size) */
void hash_del(hash_t *const hash, char const *const key, char *const data, size_t const dlen);

/* Returns number of subsequent elements moved */
size_t hash_del_internal(hash_t *const hash, size_t const x);

