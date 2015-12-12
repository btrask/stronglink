#include "SLNFilter.h"

// TODO: This should be used in other cases as well.
static int SLNFilterCopyURISynonyms(DB_txn *const txn, strarg_t const URI, str_t ***const out) {
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

	// Even though there can be multiple files with the given URI,
	// we only want the synonyms for ONE of them (the oldest one).
	DB_val key[1];
	DB_range files[1];
	SLNURIAndFileIDRange1(files, txn, URI);
	rc = db_cursor_firstr(cursor, files, key, NULL, +1);
	if(rc >= 0) {
		strarg_t u;
		uint64_t fileID;
		SLNURIAndFileIDKeyUnpack(key, txn, &u, &fileID);
		assert(0 == strcmp(URI, u));

		DB_range URIs[1];
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

@implementation SLNLinksToFilter
- (void)free {
	FREE(&URI);
	[filter free]; filter = NULL;
	[super free];
}

- (SLNFilterType)type {
	return SLNLinksToFilterType;
}
- (strarg_t)stringArg:(size_t const)i {
	if(0 == i) return URI;
	return NULL;
}
- (int)addStringArg:(strarg_t const)str :(size_t const)len {
	if(!URI) {
		URI = strndup(str, len);
		if(!URI) return DB_ENOMEM;
		return 0;
	}
	return DB_EINVAL;
}
- (void)printSexp:(FILE *const)file :(size_t const)depth {
	indent(file, depth);
	fprintf(file, "(links-to \"%s\")\n", URI);
}
- (void)printUser:(FILE *const)file :(size_t const)depth {
	fprintf(file, "%s", URI);
}

- (int)prepare:(DB_txn *const)txn {
	if(!URI) return DB_EINVAL;
	int rc = [super prepare:txn];
	if(rc < 0) return rc;

	[filter free]; filter = NULL;
	filter = [[SLNUnionFilter alloc] init];
	if(!filter) return DB_ENOMEM;

	str_t **alts = NULL;
	rc = SLNFilterCopyURISynonyms(txn, URI, &alts);
	if(rc < 0) return rc;

	for(size_t i = 0; alts[i]; i++) {
		SLNFilterRef alt = SLNFilterCreateInternal(SLNMetadataFilterType);
		if(!alt) rc = DB_ENOMEM;
		if(rc < 0) goto cleanup;
		SLNFilterAddStringArg(alt, STR_LEN("link"));
		SLNFilterAddStringArg(alt, alts[i], -1);
		SLNFilterAddFilterArg((SLNFilterRef)filter, &alt);
		SLNFilterFree(&alt);
	}

cleanup:
	for(size_t i = 0; alts[i]; i++) FREE(&alts[i]);
	FREE(&alts);
	if(rc >= 0) rc = [filter prepare:txn];
	return rc;
}

- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	return [filter seek:dir :sortID :fileID];
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	return [filter current:dir :sortID :fileID];
}
- (void)step:(int const)dir {
	return [filter step:dir];
}
- (SLNAgeRange)fullAge:(uint64_t const)fileID {
	return [filter fullAge:fileID];
}
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID {
	return [filter fastAge:fileID :sortID];
}
@end

