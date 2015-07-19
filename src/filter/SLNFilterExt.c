// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "../StrongLink.h"
#include "../SLNDB.h"
#include "../http/QueryString.h"

#define BATCH_SIZE 50

// TODO: Copy and pasted from SLNFilter.h.
static bool valid(uint64_t const x) {
	return 0 != x && UINT64_MAX != x;
}
static uint64_t invalid(int const dir) {
	if(dir < 0) return 0;
	if(dir > 0) return UINT64_MAX;
	assert(0 && "Invalid dir");
	return 0;
}

static void parse_start(strarg_t const str, SLNFilterPosition *const start) {
	assert(!start->URI);
	assert(0 != start->dir);
	if(!str) {
		// Do nothing.
	} else if('-' == str[0]) {
		start->URI = '\0' == str[1] ? NULL : strdup(str+1);
		start->dir = -start->dir;
	} else {
		start->URI = '\0' == str[0] ? NULL : strdup(str+0);
		start->dir = +start->dir;
	}
	start->sortID = invalid(-start->dir);
	start->fileID = invalid(-start->dir);
}
static uint64_t parse_count(strarg_t const str, uint64_t const count) {
	if(!str) return count;
	if('\0' == str[0]) return count;
	unsigned long long x = strtoull(str, NULL, 10);
	if(x > UINT64_MAX) return UINT64_MAX;
	return (uint64_t)x;
}
static int parse_dir(strarg_t const str, int const dir) {
	if(!str) return dir;
	if('a' == str[0]) return +1;
	if('z' == str[0]) return -1;
	return dir;
}
static bool parse_wait(strarg_t const str) {
	if(!str) return true;
	if(0 == strcasecmp(str, "")) return false;
	if(0 == strcasecmp(str, "0")) return false;
	if(0 == strcasecmp(str, "false")) return false;
	return true;
}
void SLNFilterParseOptions(strarg_t const qs, SLNFilterPosition *const start, uint64_t *const count, int *const dir, bool *const wait) {
	static strarg_t const fields[] = {
		"start",
		"count",
		"dir",
		"wait",
	};
	str_t *values[numberof(fields)] = {};
	QSValuesParse(qs, values, fields, numberof(fields));
	if(start) parse_start(values[0], start);
	if(count) *count = parse_count(values[1], *count);
	if(dir) *dir = parse_dir(values[2], *dir);
	if(wait) *wait = parse_wait(values[3]);
	QSValuesCleanup(values, numberof(values));
}
void SLNFilterPositionCleanup(SLNFilterPosition *const pos) {
	assert(pos);
	pos->dir = 0;
	FREE(&pos->URI);
	pos->sortID = 0;
	pos->fileID = 0;
	assert_zeroed(pos, 1);
}

int SLNFilterSeekToPosition(SLNFilterRef const filter, SLNFilterPosition const *const pos, DB_txn *const txn) {
	if(!pos->URI) {
		SLNFilterSeek(filter, pos->dir, pos->sortID, pos->fileID);
		return 0;
	}

	DB_cursor *cursor = NULL;
	int rc = db_txn_cursor(txn, &cursor);
	if(rc < 0) return rc;

	DB_range range[1];
	DB_val key[1];
	SLNURIAndFileIDRange1(range, txn, pos->URI);
	rc = db_cursor_firstr(cursor, range, key, NULL, +1);
	if(rc < 0) return rc;

	// Test that this URI gives us a unique, unambiguous position.
	// This is guaranteed for the internal hash and effectively
	// guaranteed for other cryptographic hashes, but may not be
	// true for other hash algorithms.
	rc = db_cursor_nextr(cursor, range, NULL, NULL, +1);
	if(rc >= 0) return DB_KEYEXIST;
	if(DB_NOTFOUND != rc) return rc;

	strarg_t u;
	uint64_t fileID;
	SLNURIAndFileIDKeyUnpack(key, txn, &u, &fileID);
	assert(0 == strcmp(pos->URI, u));

	SLNAgeRange const ages = SLNFilterFullAge(filter, fileID);
	if(!valid(ages.min) || ages.min > ages.max) return DB_NOTFOUND;
	uint64_t const sortID = ages.min;

	SLNFilterSeek(filter, pos->dir, sortID, fileID);
	SLNFilterStep(filter, pos->dir); // Start just before/after the URI.
	// TODO: Stepping is almost assuredly wrong if the URI doesn't match
	// the filter. We should check if our seek was a direct hit, and
	// only step if it was.
	return 0;
}
int SLNFilterGetPosition(SLNFilterRef const filter, SLNFilterPosition *const pos, DB_txn *const txn) {
	FREE(&pos->URI);
	SLNFilterCurrent(filter, pos->dir, &pos->sortID, &pos->fileID);
	return 0;
}
int SLNFilterCopyURI(SLNFilterRef const filter, int const dir, bool const meta, DB_txn *const txn, str_t **const out) {
	uint64_t sortID, fileID;
	for(;;) {
		SLNFilterCurrent(filter, dir, &sortID, &fileID);
		if(!valid(fileID)) return DB_NOTFOUND;

		uint64_t const age = SLNFilterFastAge(filter, fileID, sortID);
//		fprintf(stderr, "step: {%llu, %llu} -> %llu\n", (unsigned long long)sortID, (unsigned long long)fileID, (unsigned long long)age);
		if(age == sortID) break;
		SLNFilterStep(filter, dir);
	}

	DB_val fileID_key[1], file_val[1];
	SLNFileByIDKeyPack(fileID_key, txn, fileID);
	int rc = db_get(txn, fileID_key, file_val);
	if(rc < 0) return rc;

	strarg_t const hash = db_read_string(file_val, txn);
	db_assert(hash);

	str_t *URI = NULL;
	if(!meta) {
		URI = SLNFormatURI(SLN_INTERNAL_ALGO, hash);
		if(!URI) return DB_ENOMEM;
	} else {
		DB_val key[1], val[1];
		SLNMetaFileByIDKeyPack(key, txn, fileID);
		rc = db_get(txn, key, val);
		if(rc < 0) return rc;
		uint64_t f;
		strarg_t target = NULL;
		SLNMetaFileByIDValUnpack(val, txn, &f, &target);
		db_assert(target);
		URI = aasprintf("hash://%s/%s -> %s", SLN_INTERNAL_ALGO, hash, target);
		if(!URI) return DB_ENOMEM;
	}

	*out = URI; URI = NULL;
	return 0;
}

ssize_t SLNFilterCopyURIs(SLNFilterRef const filter, SLNSessionRef const session, SLNFilterPosition *const pos, int const dir, bool const meta, str_t *URIs[], size_t const max) {
	assert(URIs);
	if(!SLNSessionHasPermission(session, SLN_RDONLY)) return DB_EACCES;
	if(0 == pos->dir) return DB_EINVAL;
	if(0 == dir) return DB_EINVAL;
	if(0 == max) return 0;

	DB_env *db = NULL;
	DB_txn *txn = NULL;
	ssize_t rc = 0;

	SLNRepoRef const repo = SLNSessionGetRepo(session);
	SLNRepoDBOpen(repo, &db);
	rc = db_txn_begin(db, NULL, DB_RDONLY, &txn);
	if(rc < 0) goto cleanup;

	rc = SLNFilterPrepare(filter, txn);
	if(rc < 0) goto cleanup;
	rc = SLNFilterSeekToPosition(filter, pos, txn);
	if(rc < 0) goto cleanup;

	int const stepdir = pos->dir * dir;
	size_t i = 0;
	for(; i < max; i++) {
		size_t const x = stepdir > 0 ? i : max-1-i;
		rc = SLNFilterCopyURI(filter, pos->dir, meta, txn, &URIs[x]);
		if(DB_NOTFOUND == rc) break;
		if(rc < 0) goto cleanup;
		assert(URIs[x]);
		SLNFilterStep(filter, dir);
	}

	rc = SLNFilterGetPosition(filter, pos, txn);
	if(rc < 0) goto cleanup;
	rc = i;

	// The results should always be in the first `i` slots, even when
	// filling them in reverse order.
	if(stepdir < 0) {
		memmove(URIs+0, URIs+(max-i), sizeof(*URIs) * i);
	}

cleanup:
	db_txn_abort(txn); txn = NULL;
	SLNRepoDBClose(repo, &db);

	return rc;
}
ssize_t SLNFilterWriteURIBatch(SLNFilterRef const filter, SLNSessionRef const session, SLNFilterPosition *const pos, bool const meta, uint64_t const max, SLNFilterWriteCB const writecb, void *ctx) {
	str_t *URIs[BATCH_SIZE];
	ssize_t const count = SLNFilterCopyURIs(filter, session, pos, pos->dir, meta, URIs, MIN(max, BATCH_SIZE));
	if(count <= 0) return count;
	uv_buf_t parts[BATCH_SIZE*2];
	for(size_t i = 0; i < count; i++) {
		parts[i*2+0] = uv_buf_init((char *)URIs[i], strlen(URIs[i]));
		parts[i*2+1] = uv_buf_init((char *)STR_LEN("\r\n"));
	}
	int rc = writecb(ctx, parts, count*2);
	for(size_t i = 0; i < count; i++) FREE(&URIs[i]);
	if(rc < 0) return rc;
	return count;
}
int SLNFilterWriteURIs(SLNFilterRef const filter, SLNSessionRef const session, SLNFilterPosition *const pos, bool const meta, uint64_t const max, bool const wait, SLNFilterWriteCB const writecb, void *ctx) {
	uint64_t remaining = max;
	for(;;) {
		ssize_t const count = SLNFilterWriteURIBatch(filter, session, pos, meta, remaining, writecb, ctx);
		if(count < 0) return count;
		remaining -= count;
		if(!remaining) return 0;
		if(!count) break;
	}

	if(!wait || pos->dir < 0) return 0;

	SLNRepoRef const repo = SLNSessionGetRepo(session);
	for(;;) {
		uint64_t const timeout = uv_now(async_loop)+(1000 * 30);
		int rc = SLNRepoSubmissionWait(repo, pos->sortID, timeout);
		if(UV_ETIMEDOUT == rc) {
			uv_buf_t const parts[] = { uv_buf_init((char *)STR_LEN("\r\n")) };
			rc = writecb(ctx, parts, numberof(parts));
			if(rc < 0) break;
			continue;
		}
		assert(rc >= 0); // TODO: Handle cancellation?

		for(;;) {
			ssize_t const count = SLNFilterWriteURIBatch(filter, session, pos, meta, remaining, writecb, ctx);
			if(count < 0) return count;
			remaining -= count;
			if(!remaining) return 0;
		}
	}

	return 0;
}

