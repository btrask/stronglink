// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

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
- (SLNFilter *)unwrap {
	return self;
}

- (int)addStringArg:(strarg_t const)str :(size_t const)len {
	return DB_EINVAL;
}
- (int)addFilterArg:(SLNFilter *const)filter {
	return DB_EINVAL;
}
- (int)prepare:(DB_txn *const)txn {
	return 0;
}
@end

int SLNFilterCreate(SLNSessionRef const session, SLNFilterType const type, SLNFilterRef *const out) {
	if(!SLNSessionHasPermission(session, SLN_RDONLY)) return DB_EACCES;
	SLNFilterRef const filter = SLNFilterCreateInternal(type);
	if(!filter) return DB_ENOMEM;
	*out = filter;
	return 0;
}
SLNFilterRef SLNFilterCreateInternal(SLNFilterType const type) {
	switch(type) {
		case SLNAllFilterType:
			return (SLNFilterRef)[[SLNAllFilter alloc] init];
		case SLNVisibleFilterType:
			return (SLNFilterRef)[[SLNVisibleFilter alloc] init];
		case SLNFulltextFilterType:
			return (SLNFilterRef)[[SLNFulltextFilter alloc] init];
		case SLNMetadataFilterType:
			return (SLNFilterRef)[[SLNMetadataFilter alloc] init];
		case SLNIntersectionFilterType:
			return (SLNFilterRef)[[SLNIntersectionFilter alloc] init];
		case SLNUnionFilterType:
			return (SLNFilterRef)[[SLNUnionFilter alloc] init];
		case SLNNegationFilterType:
			return (SLNFilterRef)[[SLNNegationFilter alloc] init];
		case SLNURIFilterType:
			return (SLNFilterRef)[[SLNURIFilter alloc] init];
		case SLNTargetURIFilterType:
			return (SLNFilterRef)[[SLNTargetURIFilter alloc] init];
		case SLNMetaFileFilterType:
			return (SLNFilterRef)[[SLNMetaFileFilter alloc] init];
		case SLNLinksToFilterType:
			return (SLNFilterRef)[[SLNLinksToFilter alloc] init];
		default:
			assert(!"Filter type"); return NULL;
	}
}
void SLNFilterFree(SLNFilterRef *const filterptr) {
	[(SLNFilter *)*filterptr free]; *filterptr = NULL;
}
SLNFilterType SLNFilterGetType(SLNFilterRef const filter) {
	if(!filter) return SLNFilterTypeInvalid;
	return [(SLNFilter *)filter type];
}
SLNFilterRef SLNFilterUnwrap(SLNFilterRef const filter) {
	assert(filter);
	return (SLNFilterRef)[(SLNFilter *)filter unwrap];
}
strarg_t SLNFilterGetStringArg(SLNFilterRef const filter, size_t const i) {
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
void SLNFilterPrint(SLNFilterRef const filter, size_t const depth) {
	assert(filter);
	return [(SLNFilter *)filter print:depth];
}
size_t SLNFilterToUserFilterString(SLNFilterRef const filter, str_t *const data, size_t const size, size_t const depth) {
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
SLNAgeRange SLNFilterFullAge(SLNFilterRef const filter, uint64_t const fileID) {
	assert(filter);
	return [(SLNFilter *)filter fullAge:fileID];
}
uint64_t SLNFilterFastAge(SLNFilterRef const filter, uint64_t const fileID, uint64_t const sortID) {
	assert(filter);
	return [(SLNFilter *)filter fastAge:fileID :sortID];
}

