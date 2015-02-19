#include "EFSFilter.h"

static int filtercmp(EFSFilter *const a, EFSFilter *const b, int const dir) {
	uint64_t asort, afile, bsort, bfile;
	[a current:dir*+1 :&asort :&afile];
	[b current:dir*+1 :&bsort :&bfile];
	if(asort > bsort) return dir*+1;
	if(asort < bsort) return dir*-1;
	if(afile > bfile) return dir*+1;
	if(afile < bfile) return dir*-1;
	return 0;
}
static int filtercmp_fwd(EFSFilter *const *const a, EFSFilter *const *const b) {
	return filtercmp(*a, *b, +1);
}
static int filtercmp_rev(EFSFilter *const *const a, EFSFilter *const *const b) {
	return filtercmp(*a, *b, -1);
}

@implementation EFSCollectionFilter
- (void)free {
	FREE(&filters);
	[super free];
}

- (EFSFilter *)unwrap {
	if(1 == count) return [filters[0] unwrap];
	return nil;
}
- (err_t)addFilterArg:(EFSFilter *const)filter {
	assert(filter);
	if(count+1 > asize) {
		asize = MAX(8, asize * 2);
		filters = realloc(filters, sizeof(filters[0]) * asize);
		assert(filters); // TODO
	}
	filters[count++] = filter;
	return 0;
}

- (err_t)prepare:(DB_txn *const)txn {
	if([super prepare:txn] < 0) return -1;
	for(index_t i = 0; i < count; ++i) {
		if([filters[i] prepare:txn] < 0) return -1;
	}
	sort = 0;
	return 0;
}
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	assert(count);
	for(index_t i = 0; i < count; ++i) {
		[filters[i] seek:dir :sortID :fileID];
	}
	[self sort:dir ? dir : +1];
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	assert(count);
	// TODO: The current value shouldn't actually depend on which direction the client wants to go. We shouldn't even accept it as an argument.
	if(0 == sort) {
		assert(0); // TODO
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
	[filters[0] current:dir :sortID :fileID];
}
- (void)step:(int const)dir {
	assert(count);
	assert(0 != dir);
	assert(0 != sort); // Means we don't have a valid position.
	if(dir != sort) {
		assert(0 && "Filter direction reversal not currently supported"); // TODO: To reverse directions, we have to "flip" any trailing filters. Cf. LSMDB.
	}
	uint64_t oldSortID, oldFileID;
	[filters[0] current:dir :&oldSortID :&oldFileID];
	[filters[0] step:dir];
	for(index_t i = 1; i < count; ++i) {
		uint64_t curSortID, curFileID;
		[filters[i] current:dir :&curSortID :&curFileID];
		if(curSortID != oldSortID || curFileID != oldFileID) break;
		[filters[i] step:dir];
	}
	[self sort:dir];
}
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID {
	EFSFilterType const type = [self type]; // TODO: Polymorphism
	bool_t hit = false;
	// TODO: Maybe better to check in reverse order?
	// May have to sort first
	for(index_t i = 0; i < count; ++i) {
		uint64_t const age = [filters[i] age:sortID :fileID];
		if(age == sortID) {
			hit = true;
		} else if(EFSIntersectionFilterType == type) {
			if(age > sortID) return UINT64_MAX;
		} else if(EFSUnionFilterType == type) {
			if(age < sortID) return 0;
		}
	}
	if(hit) {
		return sortID;
	} else if(EFSIntersectionFilterType == type) {
		return 0;
	} else if(EFSUnionFilterType == type) {
		return UINT64_MAX;
	}
	assert(0);
	return 0;
}

- (void)sort:(int const)dir {
	assert(0 != dir);
	int (*cmp)();
	if(dir > 0) cmp = filtercmp_fwd;
	if(dir < 0) cmp = filtercmp_rev;
	qsort(filters, count, sizeof(filters[0]), cmp);
	sort = dir;
}
@end

@implementation EFSIntersectionFilter
- (EFSFilterType)type {
	return EFSIntersectionFilterType;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(intersection\n");
	for(index_t i = 0; i < count; ++i) [filters[i] print:depth+1];
	indent(depth);
	fprintf(stderr, ")\n");
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	if(!count) return wr(data, size, "");
	size_t len = 0;
	if(depth) len += wr(data+len, size-len, "(");
	for(index_t i = 0; i < count; ++i) {
		if(i) len += wr(data+len, size-len, " ");
		len += [filters[i] getUserFilter:data+len :size-len :depth+1];
	}
	if(depth) len += wr(data+len, size-len, ")");
	return len;
}
@end

@implementation EFSUnionFilter
- (EFSFilterType)type {
	return EFSUnionFilterType;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(union\n");
	for(index_t i = 0; i < count; ++i) [filters[i] print:depth+1];
	indent(depth);
	fprintf(stderr, ")\n");
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	size_t len = 0;
	for(index_t i = 0; i < count; ++i) {
		if(i) len += wr(data+len, size-len, " or ");
		len += [filters[i] getUserFilter:data+len :size-len :depth+1];
	}
	return len;
}
@end

