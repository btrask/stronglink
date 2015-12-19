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
	} else if('-' != str[0]) {
		// TODO: Check strdup failures.
		start->URI = '\0' == str[0] ? NULL : strdup(str+0);
		start->dir *= +1;
	} else {
		start->URI = '\0' == str[1] ? NULL : strdup(str+1);
		start->dir *= -1;
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
	assert(0 != dir);
	if(!str) return dir;
	if('a' == str[0]) return +1;
	if('z' == str[0]) return -1;
	return dir;
}
static bool parse_wait(strarg_t const str) {
	if(!str) return true;
	if(0 == strcasecmp(str, "")) return false;
	if(0 == strcasecmp(str, "0")) return false;
	if(0 == strcasecmp(str, "no")) return false;
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
void SLNFilterPositionInit(SLNFilterPosition *const pos, int const dir) {
	assert(0 != dir);
	memset(pos, 0, sizeof(*pos)); // Clear padding for assert_zeroed.
	pos->dir = dir;
	pos->URI = NULL,
	pos->sortID = invalid(-dir);
	pos->fileID = invalid(-dir);
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
		if(valid(pos->fileID)) SLNFilterStep(filter, pos->dir);
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
	// TODO: Given our stance on collisions, is this even needed?
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
	uint64_t sortID, fileID;
	for(;;) {
		SLNFilterCurrent(filter, pos->dir, &sortID, &fileID);
		if(!valid(fileID)) return DB_NOTFOUND;

		uint64_t const age = SLNFilterFastAge(filter, fileID, sortID);
//		fprintf(stderr, "step: {%llu, %llu} -> %llu\n", (unsigned long long)sortID, (unsigned long long)fileID, (unsigned long long)age);
		if(age == sortID) break;
		SLNFilterStep(filter, pos->dir);
	}
	FREE(&pos->URI);
	pos->sortID = sortID;
	pos->fileID = fileID;
	return 0;
}
int SLNFilterCopyURI(SLNFilterRef const filter, uint64_t const fileID, bool const meta, DB_txn *const txn, str_t **const out) {
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
		strarg_t target = NULL;
		SLNMetaFileByIDValUnpack(val, txn, &target);
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

	rc = SLNSessionDBOpen(session, SLN_RDONLY, &db);
	if(rc < 0) goto cleanup;
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
		rc = SLNFilterGetPosition(filter, pos, txn);
		if(DB_NOTFOUND == rc) {
			rc = 0;
			break;
		}
		rc = SLNFilterCopyURI(filter, pos->fileID, meta, txn, &URIs[x]);
		if(rc < 0) goto cleanup;
		assert(URIs[x]);
		SLNFilterStep(filter, pos->dir);
	}

	// The results should always be in the first `i` slots, even when
	// filling them in reverse order.
	if(stepdir < 0) {
		memmove(URIs+0, URIs+(max-i), sizeof(*URIs) * i);
	}

	assert(rc >= 0);
	rc = i;

cleanup:
	db_txn_abort(txn); txn = NULL;
	SLNSessionDBClose(session, &db);

	return rc;
}
ssize_t SLNFilterWriteURIBatch(SLNFilterRef const filter, SLNSessionRef const session, SLNFilterPosition *const pos, bool const meta, uint64_t const max, SLNFilterWriteCB const writecb, void *ctx) {
	str_t *URIs[BATCH_SIZE];
	ssize_t const count = SLNFilterCopyURIs(filter, session, pos, pos->dir, meta, URIs, MIN(max, BATCH_SIZE));
	if(count <= 0) return count;
	uv_buf_t parts[BATCH_SIZE*2];
	for(size_t i = 0; i < count; i++) {
		parts[i*2+0] = uv_buf_init((char *)URIs[i], strlen(URIs[i]));
		parts[i*2+1] = UV_BUF_STATIC("\r\n");
	}
	int rc = writecb(ctx, parts, count*2);
	for(size_t i = 0; i < count; i++) FREE(&URIs[i]);
	assert_zeroed(URIs, count);
	if(rc < 0) return rc;
	return count;
}
int SLNFilterWriteURIs(SLNFilterRef const filter, SLNSessionRef const session, SLNFilterPosition *const pos, bool const meta, uint64_t const max, bool const wait, SLNFilterWriteCB const writecb, SLNFilterFlushCB const flushcb, void *ctx) {
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
		int rc = flushcb ? flushcb(ctx) : 0;
		if(rc < 0) return rc;

		uint64_t latest = pos->sortID;
		uint64_t const timeout = uv_now(async_loop)+(1000 * 30);
		rc = SLNRepoSubmissionWait(repo, &latest, timeout);
		if(UV_ETIMEDOUT == rc) {
			uv_buf_t const parts[] = { UV_BUF_STATIC("\r\n") };
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
			if(count < BATCH_SIZE) break;
		}

		// This is how far we scanned, even if we didn't find anything.
		if(pos->sortID < latest) {
			pos->sortID = latest;
			pos->fileID = 0;
		}
	}

	return 0;
}

int SLNFilterCopyURISynonyms(DB_txn *const txn, strarg_t const URI, str_t ***const out) {
	assert(out);
	if(!URI) return DB_EINVAL;

	DB_cursor *cursor = NULL;
	size_t count = 0;
	size_t size = 0;
	str_t **alts = NULL;
	int rc = 0;

	size = 10;
	alts = reallocarray(NULL, size, sizeof(*alts));
	if(!alts) return DB_ENOMEM;
	alts[count++] = strdup(URI);
	alts[count] = NULL;
	if(!alts[count-1]) rc = DB_ENOMEM;
	if(rc < 0) goto cleanup;

	rc = db_cursor_open(txn, &cursor);
	if(rc < 0) goto cleanup;

	uint64_t fileID = 0;
	rc = SLNURIGetFileID(URI, txn, &fileID);
	if(rc >= 0) {
		DB_range URIs[1];
		DB_val key[1];
		SLNFileIDAndURIRange1(URIs, txn, fileID);
		rc = db_cursor_firstr(cursor, URIs, key, NULL, +1);
		if(rc < 0 && DB_NOTFOUND != rc) goto cleanup;

		for(; DB_NOTFOUND != rc; rc = db_cursor_nextr(cursor, URIs, key, NULL, +1)) {
			uint64_t f;
			strarg_t alt;
			SLNFileIDAndURIKeyUnpack(key, txn, &f, &alt);
			assert(fileID == f);

			// TODO: Check for duplicates.
			if(count+1+1 > size) {
				size *= 2;
				alts = reallocarray(alts, size, sizeof(*alts));
				assert(alts); // TODO
			}
			alts[count++] = strdup(alt);
			alts[count] = NULL;
			if(!alts[count-1]) rc = DB_ENOMEM;
			if(rc < 0) goto cleanup;
		}
	} else if(DB_NOTFOUND != rc) {
		goto cleanup;
	}

	assert(DB_NOTFOUND == rc);
	rc = 0;

	*out = alts; alts = NULL;

cleanup:
	db_cursor_close(cursor); cursor = NULL;
	FREE(&alts);
	return rc;
}

