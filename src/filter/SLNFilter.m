#include "SLNFilter.h"

@implementation SLNObject
+ (id)alloc {
	return class_createInstance(self, 0);
}
- (id)init {
	return self;
}
- (void)free {
	size_t const extra = (char *)&isa + sizeof(isa) - (char *)self;
	size_t const len = class_getInstanceSize(isa);
	assert_zeroed((char *)self+extra, len-extra);
	object_dispose(self);
}
@end

@implementation SLNFilter
- (int)addStringArg:(strarg_t const)str :(size_t const)len {
	return DB_EINVAL;
}
- (int)addFilterArg:(SLNFilter *const)filter {
	return DB_EINVAL;
}
- (int)prepare:(DB_txn *const)txn {
	return DB_SUCCESS;
}
@end

int SLNFilterCreate(SLNSessionRef const session, SLNFilterType const type, SLNFilterRef *const out) {
	// TODO: HACK for certain cases where we're not ready to deal with
	// sessions everywhere (SLNUserFilterParser). Yes this is scary.
	if((SLNSessionRef)-1 != session) {
		if(!SLNSessionHasPermission(session, SLN_RDONLY)) return DB_EACCES;
	}
	switch(type) {
		case SLNAllFilterType:
			*out = (SLNFilterRef)[[SLNAllFilter alloc] init];
			return DB_SUCCESS;
		case SLNVisibleFilterType:
			*out = (SLNFilterRef)[[SLNVisibleFilter alloc] init];
			return DB_SUCCESS;
		case SLNFulltextFilterType:
			*out = (SLNFilterRef)[[SLNFulltextFilter alloc] init];
			return DB_SUCCESS;
		case SLNMetadataFilterType:
			*out = (SLNFilterRef)[[SLNMetadataFilter alloc] init];
			return DB_SUCCESS;
		case SLNIntersectionFilterType:
			*out = (SLNFilterRef)[[SLNIntersectionFilter alloc] init];
			return DB_SUCCESS;
		case SLNUnionFilterType:
			*out = (SLNFilterRef)[[SLNUnionFilter alloc] init];
			return DB_SUCCESS;
		case SLNNegationFilterType:
			*out = (SLNFilterRef)[[SLNNegationFilter alloc] init];
			return DB_SUCCESS;
		case SLNURIFilterType:
			*out = (SLNFilterRef)[[SLNURIFilter alloc] init];
			return DB_SUCCESS;
		case SLNMetaFileFilterType:
			*out = (SLNFilterRef)[[SLNMetaFileFilter alloc] init];
			return DB_SUCCESS;
		case SLNBadMetaFileFilterType:
			*out = (SLNFilterRef)[[SLNBadMetaFileFilter alloc] init];
			return DB_SUCCESS;
		default: assert(0); return DB_EINVAL;
	}
}
void SLNFilterFree(SLNFilterRef *const filterptr) {
	[(SLNFilter *)*filterptr free]; *filterptr = NULL;
}
SLNFilterType SLNFilterGetType(SLNFilterRef const filter) {
	assert(filter);
	return [(SLNFilter *)filter type];
}
SLNFilterRef SLNFilterUnwrap(SLNFilterRef const filter) {
	assert(filter);
	return (SLNFilterRef)[(SLNFilter *)filter unwrap];
}
strarg_t SLNFilterGetStringArg(SLNFilterRef const filter, index_t const i) {
	assert(filter);
	return [(SLNFilter *)filter stringArg:i];
}
int SLNFilterAddStringArg(SLNFilterRef const filter, strarg_t const str, ssize_t const len) {
	assert(filter);
	return [(SLNFilter *)filter addStringArg:str :len];
}
int SLNFilterAddFilterArg(SLNFilterRef const filter, SLNFilterRef const subfilter) {
	assert(filter);
	return [(SLNFilter *)filter addFilterArg:(SLNFilter *)subfilter];
}
void SLNFilterPrint(SLNFilterRef const filter, count_t const depth) {
	assert(filter);
	return [(SLNFilter *)filter print:depth];
}
size_t SLNFilterToUserFilterString(SLNFilterRef const filter, str_t *const data, size_t const size, count_t const depth) {
	assert(filter);
	return [(SLNFilter *)filter getUserFilter:data :size :depth];
}
int SLNFilterPrepare(SLNFilterRef const filter, DB_txn *const txn) {
	assert(filter);
	return [(SLNFilter *)filter prepare:txn];
}
void SLNFilterSeek(SLNFilterRef const filter, int const dir, uint64_t const sortID, uint64_t const fileID) {
	[(SLNFilter *)filter seek:dir :sortID :fileID];
}
void SLNFilterCurrent(SLNFilterRef const filter, int const dir, uint64_t *const sortID, uint64_t *const fileID) {
	assert(filter);
	assert(dir);
	[(SLNFilter *)filter current:dir :sortID :fileID];
}
void SLNFilterStep(SLNFilterRef const filter, int const dir) {
	assert(filter);
	assert(dir);
	[(SLNFilter *)filter step:dir];
}
uint64_t SLNFilterAge(SLNFilterRef const filter, uint64_t const fileID, uint64_t const sortID) {
	assert(filter);
	return [(SLNFilter *)filter fastAge:fileID :sortID];
}

int SLNFilterSeekURI(SLNFilterRef const filter, int const dir, strarg_t const URI, DB_txn *const txn) {
	if(!URI) return DB_NOTFOUND;

	DB_cursor *cursor = NULL;
	int rc = db_txn_cursor(txn, &cursor);
	if(DB_SUCCESS != rc) return rc;

	// URIs passed to this function are assumed to be unique
	// (i.e. using the repo's internal algo), thus we can just
	// use the first file found. If you pass a URI with
	// collisions, we may seek to the wrong place or fail
	// entirely.
	DB_range range[1];
	DB_val key[1];
	SLNURIAndFileIDRange1(range, txn, URI);
	rc = db_cursor_firstr(cursor, range, key, NULL, +1);
	if(DB_SUCCESS != rc) return rc;
	strarg_t u;
	uint64_t fileID;
	SLNURIAndFileIDKeyUnpack(key, txn, &u, &fileID);
	assert(0 == strcmp(URI, u));

	SLNAgeRange const ages = [(SLNFilter *)filter fullAge:fileID];
	if(!valid(ages.min) || ages.min > ages.max) return DB_NOTFOUND;
	uint64_t const sortID = ages.min;

	[(SLNFilter *)filter seek:dir :sortID :fileID];
	[(SLNFilter *)filter step:dir]; // Start just before/after the URI.
	// TODO: Stepping is almost assuredly wrong if the URI doesn't match
	// the filter. We should check if our seek was a direct hit, and
	// only step if it was.
	return DB_SUCCESS;
}
str_t *SLNFilterCopyNextURI(SLNFilterRef const filter, int const dir, DB_txn *const txn) {
	uint64_t sortID, fileID;
	for(;;) {
		SLNFilterCurrent(filter, dir, &sortID, &fileID);
		if(!valid(fileID)) return NULL;

		uint64_t const age = SLNFilterAge(filter, fileID, sortID);
//		fprintf(stderr, "step: {%llu, %llu} -> %llu\n", (unsigned long long)sortID, (unsigned long long)fileID, (unsigned long long)age);
		if(age == sortID) break;
		SLNFilterStep(filter, dir);
	}

	DB_val fileID_key[1], file_val[1];
	SLNFileByIDKeyPack(fileID_key, txn, fileID);
	int rc = db_get(txn, fileID_key, file_val);
	db_assertf(DB_SUCCESS == rc, "Database error %s", db_strerror(rc)); // TODO

	strarg_t const hash = db_read_string(file_val, txn);
	assert(hash); // TODO
	str_t *const URI = SLNFormatURI(SLN_INTERNAL_ALGO, hash);
	assert(URI);
	SLNFilterStep(filter, dir);
	return URI;
}

