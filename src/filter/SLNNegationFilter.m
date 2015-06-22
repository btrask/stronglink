// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include "SLNFilter.h"

@implementation SLNNegationFilter
- (void)free {
	[subfilter free]; subfilter = NULL;
	[super free];
}

- (SLNFilterType)type {
	return SLNNegationFilterType;
}
- (SLNFilter *)unwrap {
	return self;
}
- (int)addFilterArg:(SLNFilter *const)filter {
	if(!filter) return DB_EINVAL;
	if(subfilter) return DB_EINVAL;
	// TODO: Check that filter is "simple"
	// Meaning it returns an age range pinned to either 0 or UINT64_MAX.
	// Basically, it must not be a collection filter.
	// Once we support mutable filters with multiple ranges, this will
	// no longer be a problem.
	subfilter = filter;
	return 0;
}
- (void)print:(size_t const)depth {
	indent(depth);
	fprintf(stderr, "(negation\n");
	[subfilter print:depth+1];
	indent(depth);
	fprintf(stderr, ")\n");
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(size_t const)depth {
	size_t len = 0;
	len += wr(data+len, size-len, "-");
	len += [subfilter getUserFilter:data+len :size-len :depth+1];
	return len;
}

- (int)prepare:(DB_txn *const)txn {
	int rc = [super prepare:txn];
	if(rc < 0) return rc;
	return [subfilter prepare:txn];
}
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	if(sortID) *sortID = invalid(dir);
	if(fileID) *fileID = invalid(dir);
}
- (void)step:(int const)dir {}
- (SLNAgeRange)fullAge:(uint64_t const)fileID {
	SLNAgeRange const ages = [subfilter fullAge:fileID];
	if(!valid(ages.min) && !valid(ages.max)) return (SLNAgeRange){ ages.max, ages.min };
	if( valid(ages.min) && !valid(ages.max)) return (SLNAgeRange){ 0, ages.min-1 };
	if(!valid(ages.min) &&  valid(ages.max)) return (SLNAgeRange){ ages.max+1, UINT64_MAX };
	assert(!"Negation sub-filter");
	return (SLNAgeRange){ 456, 123 }; // Not reached
}
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID {
	uint64_t const age = [subfilter fastAge:fileID :sortID];
	if(sortID == age) return UINT64_MAX;
	return sortID;
}
@end

