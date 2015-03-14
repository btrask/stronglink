#include "EFSFilter.h"

@implementation EFSObject
+ (id)alloc {
	return class_createInstance(self, 0);
}
- (id)init {
	return self;
}
- (void)free {
	object_dispose(self);
}
@end

@implementation EFSFilter
- (int)addStringArg:(strarg_t const)str :(size_t const)len {
	return -1;
}
- (int)addFilterArg:(EFSFilter *const)filter {
	return -1;
}
- (int)prepare:(DB_txn *const)txn {
	return 0;
}
@end

EFSFilterRef EFSFilterCreate(EFSFilterType const type) {
	switch(type) {
		case EFSAllFilterType:
			return (EFSFilterRef)[[EFSAllFilter alloc] init];
		case EFSFulltextFilterType:
			return (EFSFilterRef)[[EFSFulltextFilter alloc] init];
		case EFSMetadataFilterType:
			return (EFSFilterRef)[[EFSMetadataFilter alloc] init];
		case EFSIntersectionFilterType:
			return (EFSFilterRef)[[EFSIntersectionFilter alloc] init];
		case EFSUnionFilterType:
			return (EFSFilterRef)[[EFSUnionFilter alloc] init];
		case EFSMetaFileFilterType:
			return (EFSFilterRef)[[EFSMetaFileFilter alloc] init];
		default: assert(0); return NULL;
	}
}
EFSFilterRef EFSPermissionFilterCreate(uint64_t const userID) {
	//return (EFSFilterRef)[[EFSPermissionFilter alloc] initWithUserID:userID];
	return NULL; // TODO
}
void EFSFilterFree(EFSFilterRef *const filterptr) {
	[(EFSFilter *)*filterptr free]; *filterptr = NULL;
}
EFSFilterType EFSFilterGetType(EFSFilterRef const filter) {
	assert(filter);
	return [(EFSFilter *)filter type];
}
EFSFilterRef EFSFilterUnwrap(EFSFilterRef const filter) {
	assert(filter);
	return (EFSFilterRef)[(EFSFilter *)filter unwrap];
}
strarg_t EFSFilterGetStringArg(EFSFilterRef const filter, index_t const i) {
	assert(filter);
	return [(EFSFilter *)filter stringArg:i];
}
int EFSFilterAddStringArg(EFSFilterRef const filter, strarg_t const str, ssize_t const len) {
	assert(filter);
	return [(EFSFilter *)filter addStringArg:str :len];
}
int EFSFilterAddFilterArg(EFSFilterRef const filter, EFSFilterRef const subfilter) {
	assert(filter);
	return [(EFSFilter *)filter addFilterArg:(EFSFilter *)subfilter];
}
void EFSFilterPrint(EFSFilterRef const filter, count_t const depth) {
	assert(filter);
	return [(EFSFilter *)filter print:depth];
}
size_t EFSFilterToUserFilterString(EFSFilterRef const filter, str_t *const data, size_t const size, count_t const depth) {
	assert(filter);
	return [(EFSFilter *)filter getUserFilter:data :size :depth];
}
int EFSFilterPrepare(EFSFilterRef const filter, DB_txn *const txn) {
	assert(filter);
	return [(EFSFilter *)filter prepare:txn];
}
void EFSFilterSeek(EFSFilterRef const filter, int const dir, uint64_t const sortID, uint64_t const fileID) {
	[(EFSFilter *)filter seek:dir :sortID :fileID];
}
void EFSFilterCurrent(EFSFilterRef const filter, int const dir, uint64_t *const sortID, uint64_t *const fileID) {
	assert(filter);
	assert(dir);
	[(EFSFilter *)filter current:dir :sortID :fileID];
}
void EFSFilterStep(EFSFilterRef const filter, int const dir) {
	assert(filter);
	assert(dir);
	[(EFSFilter *)filter step:dir];
}
uint64_t EFSFilterAge(EFSFilterRef const filter, uint64_t const sortID, uint64_t const fileID) {
	assert(filter);
	return [(EFSFilter *)filter age:sortID :fileID];
}
str_t *EFSFilterCopyNextURI(EFSFilterRef const filter, int const dir, DB_txn *const txn) {
	for(;; EFSFilterStep(filter, dir)) {
		uint64_t sortID, fileID;
		EFSFilterCurrent(filter, dir, &sortID, &fileID);
		if(!valid(fileID)) return NULL;

//		fprintf(stderr, "step: %llu, %llu\n", (unsigned long long)sortID, (unsigned long long)fileID);

		uint64_t const age = EFSFilterAge(filter, sortID, fileID);
//		fprintf(stderr, "{%llu, %llu} -> %llu\n", (unsigned long long)sortID, (unsigned long long)fileID, (unsigned long long)age);
		if(age != sortID) continue;

		DB_val fileID_key[1];
		EFSFileByIDKeyPack(fileID_key, txn, fileID);
		DB_val file_val[1];
		int rc = db_get(txn, fileID_key, file_val);
		assertf(DB_SUCCESS == rc, "Database error %s", db_strerror(rc));

		strarg_t const hash = db_read_string(file_val, txn);
		assert(hash);
		str_t *const URI = EFSFormatURI(EFS_INTERNAL_ALGO, hash);
		assert(URI);
		EFSFilterStep(filter, dir);
		return URI;
	}
}


