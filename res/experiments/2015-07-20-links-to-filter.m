@interface SLNLinksToFilter
{
	str_t *URI;
	SLNUnionFilter *filter;
}
@end

// TODO: This should be used in other cases as well.
static int SLNFilterCopyURISynonyms(DB_txn *const txn, strarg_t const URI, str_t **out) {
	assert(out);
	if(!URI) return DB_EINVAL;

	DB_cursor *c1 = NULL;
	DB_cursor *c2 = NULL;
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

	rc = db_cursor_open(txn, &c1);
	if(rc < 0) goto cleanup;
	rc = db_cursor_open(txn, &c2);
	if(rc < 0) goto cleanup;

	DB_val key[1];
	DB_range files[1];
	SLNURIAndFileIDRange1(files, txn, URI);
	rc = db_cursor_firstr(c1, files, key, NULL, +1);
	if(rc < 0 && DB_NOTFOUND != rc) goto cleanup;

	for(; DB_NOTFOUND != rc; rc = db_cursor_nextr(c1, files, key, NULL, +1)) {
		strarg_t u;
		uint64_t fileID;
		SLNURIAndFileIDKeyUnpack(key, txn, &u, &fileID);
		assert(0 == strcmp(URI, u));

		DB_range URIs[1];
		SLIFileIDAndURIRange1(URIs, txn, fileID);
		rc = db_cursor_firstr(c2, URIs, key, NULL, +1);
		if(rc < 0 && DB_NOTFOUND != rc) goto cleanup;
		for(; DB_NOTFOUND != rc; rc = db_cursor_next(c2, URIs, key, NULL, +1)) {
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
	}

	*out = alts; alts = NULL;

cleanup:
	db_cursor_close(c1); c1 = NULL;
	db_cursor_close(c2); c2 = NULL;
	FREE(&alts);
	return rc;
}

@implementation SLNLinksToFilter
- (void)free {
	FREE(&URI);
	SLNFilterFree(&filter);
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
- (void)print:(size_t const)depth {
	indent(depth);
	fprintf(stderr, "(links-to \"%s\")\n", URI);
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(size_t const)depth {
	return wr(data, size, URI);
}

- (int)prepare:(DB_txn *const)txn {
	if(!URI) return DB_EINVAL;
	int rc = [super prepare:txn];
	if(rc < 0) return rc;

	SLNFilterFree(&filter);
	filter = SLNFilterCreateInternal(SLNUnionFilterType);
	if(!filter) return DB_ENOMEM;

	str_t *alts = NULL;
	rc = SLNFilterCopyURISynonyms(txn, URI, &alts);
	if(rc < 0) return rc;

	for(size_t i = 0; alts[i]; i++) {
		SLNFilterRef alt = SLNFilterCreateInternal(SLNMetadataFilterType);
		if(!alt) rc = DB_ENOMEM;
		if(rc < 0) goto cleanup;
		SLNFilterAddStringArg(alt, STR_LEN("link"));
		SLNFilterAddStringArg(alt, alts[i], -1);
		SLNFilterAddFilterArg(filter, alt);
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

