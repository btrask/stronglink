#include "SLNFilter.h"

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
		if(!URI) return KVS_ENOMEM;
		return 0;
	}
	return KVS_EINVAL;
}
- (void)printSexp:(FILE *const)file :(size_t const)depth {
	indent(file, depth);
	fprintf(file, "(links-to \"%s\")\n", URI);
}
- (void)printUser:(FILE *const)file :(size_t const)depth {
	fprintf(file, "%s", URI);
}

- (int)prepare:(KVS_txn *const)txn {
	if(!URI) return KVS_EINVAL;
	int rc = [super prepare:txn];
	if(rc < 0) return rc;

	[filter free]; filter = NULL;
	filter = [[SLNUnionFilter alloc] init];
	if(!filter) return KVS_ENOMEM;

	str_t **alts = NULL;
	rc = SLNFilterCopyURISynonyms(txn, URI, &alts);
	if(rc < 0) return rc;

	for(size_t i = 0; alts[i]; i++) {
		SLNFilterRef alt = SLNFilterCreateInternal(SLNMetadataFilterType);
		if(!alt) rc = KVS_ENOMEM;
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

