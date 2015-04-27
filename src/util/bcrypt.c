#include <stdlib.h>
#include <string.h>
#include "../../deps/crypt_blowfish/ow-crypt.h"
#include "../async/async.h"
#include "bcrypt.h"

#define BCRYPT_PREFIX "$2a$" /* TODO: Are we supposed to use "$2y$" now? */
#define BCRYPT_ROUNDS 13 /* TODO: Possibly reduce this to 12. */
#define BCRYPT_SALT 16

bool checkpass(char const *const pass, char const *const hash) {
	int size = 0;
	void *data = NULL;
	char const *attempt = crypt_ra(pass, hash, &data, &size);
	bool const success = (attempt && 0 == strcmp(attempt, hash));
	attempt = NULL;
	free(data); data = NULL;
	return success;
}
char *hashpass(char const *const pass) {
	// TODO: async_random isn't currently parallel or thread-safe
//	async_pool_enter(NULL);
	char input[BCRYPT_SALT];
	if(async_random((unsigned char *)input, BCRYPT_SALT) < 0) {
//		async_pool_leave(NULL);
		return NULL;
	}
	async_pool_enter(NULL); // TODO (above)

	char *salt = crypt_gensalt_ra(BCRYPT_PREFIX, BCRYPT_ROUNDS, input, BCRYPT_SALT);
	if(!salt) {
		async_pool_leave(NULL);
		return NULL;
	}
	int size = 0;
	void *data = NULL;
	char const *orig = crypt_ra(pass, salt, &data, &size);
	char *hash = orig ? strdup(orig) : NULL;
	free(salt); salt = NULL;
	free(data); data = NULL;
	async_pool_leave(NULL);
	return hash;
}

