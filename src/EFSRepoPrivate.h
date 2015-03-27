#include <assert.h>
#include "StrongLink.h"
#include "EFSDB.h"
#include "util/hash.h"

typedef struct cookie_t cookie_t;

struct EFSRepo {
	str_t *dir;
	str_t *name;

	str_t *dataDir;
	str_t *tempDir;
	str_t *cacheDir;
	str_t *DBPath;

	DB_env *db;

	hash_t cookie_hash[1];
	cookie_t *cookie_data;

	async_mutex_t sub_mutex[1];
	async_cond_t sub_cond[1];
	uint64_t sub_latest;

	EFSPullRef *pulls;
	count_t pull_count;
	count_t pull_size;
};

int EFSRepoAuthInit(EFSRepoRef const repo);
void EFSRepoAuthDestroy(EFSRepoRef const repo);

