

// the idea is to make this easier by starting from scratch

// we can reverse the order of a single batch
// but we can't reverse order across batches
// and the size of a batch is capped at say 50 or 100 items

// rules
// 1. start with the api
// 2. move everything filter/query related out of slnsession

typedef struct {
	str_t *URI;
	uint64_t sortID;
	uint64_t fileID;
	int dir;
} SLNFilterPosition;

typedef int (*SLNFilterWriteCB)(void *ctx, uv_buf_t const parts[], unsigned int const count);

void SLNFilterParseOptions(strarg_t const qs, SLNFilterPosition *const pos, uint64_t *const count, int *const dir);
void SLNFilterPositionCleanup(SLNFilterPosition *const pos);

int SLNFilterSeekToPosition(SLNFilterRef *const filter, SLNFilterPosition *const pos, DB_txn *const txn);
int SLNFilterGetPosition(SLNFilterRef const filter, SLNFilterPosition *const pos, DB_txn *const txn);
int SLNFilterCopyNextURI(SLNFilterRef *const filter, int const dir, bool const meta, DB_txn *const txn, str_t **const out);

int SLNFilterCopyURIs(SLNFilterRef const filter, SLNSessionRef const session, SLNFilterPosition *const pos, int const dir, bool const meta, str_t *uris[], size_t *const count);
int SLNFilterWriteURIBatch(SLNFilterRef const filter, SLNSessionRef const session, SLNFilterPosition *const pos, size_t const count, bool const meta, SLNFilterWriteCB const writecb, void *ctx);
int SLNFilterWriteURIs(SLNFilterRef const filter, SLNSessionRef const session, SLNFilterPosition *const pos, uint64_t const count, bool const meta, bool const wait, SLNFilterWriteCB const writecb, void *ctx);

// something like that...?















