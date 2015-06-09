// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <ctype.h>
#include <objc/runtime.h>
#include "../StrongLink.h"
#include "../SLNDB.h"
#include "../../deps/openbsd-compat/includes.h"

typedef struct {
	uint64_t min;
	uint64_t max;
} SLNAgeRange;

@interface SLNObject
{
	Class isa;
}
+ (id)alloc;
- (id)init;
- (void)free;
@end

// TODO: At one point I had a System(tm) for declaring abstract and concrete
// methods, but over time it has falling into disrepair. We should reevaluate
// using @protocols with optional methods to see if we can get better
// compile-time checking.

@interface SLNFilter : SLNObject
@end
@interface SLNFilter (Abstract)
- (SLNFilterType)type;
- (SLNFilter *)unwrap;
- (strarg_t)stringArg:(size_t const)i;
- (int)addStringArg:(strarg_t const)str :(size_t const)len;
- (int)addFilterArg:(SLNFilter *const)filter;
- (void)print:(size_t const)depth;
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(size_t const)depth;

- (int)prepare:(DB_txn *const)txn;
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID;
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID;
- (void)step:(int const)dir;
- (SLNAgeRange)fullAge:(uint64_t const)fileID;
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID;
@end

@interface SLNIndirectFilter : SLNFilter
{
	DB_txn *curtxn;
	DB_cursor *step_target;
	DB_cursor *step_files;
	DB_cursor *age_uris;
	DB_cursor *age_metafiles;
}
- (int)prepare:(DB_txn *const)txn;
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID;
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID;
- (void)step:(int const)dir;
- (SLNAgeRange)fullAge:(uint64_t const)fileID;
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID;
@end
@interface SLNIndirectFilter (Abstract)
- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID;
- (uint64_t)currentMeta:(int const)dir;
- (uint64_t)stepMeta:(int const)dir;
- (bool)match:(uint64_t const)metaFileID;
@end

@interface SLNVisibleFilter : SLNIndirectFilter
{
	DB_cursor *metafiles;
}
@end

struct token {
	str_t *str;
};
@interface SLNFulltextFilter : SLNIndirectFilter
{
	str_t *term;
	struct token *tokens;
	size_t count;
	size_t asize;
	DB_cursor *metafiles;
	DB_cursor *phrase; // TODO
	DB_cursor *match;
}
@end

@interface SLNMetadataFilter : SLNIndirectFilter
{
	str_t *field;
	str_t *value;
	DB_cursor *metafiles;
	DB_cursor *match;
}
@end

// SLNCollectionFilter.m
@interface SLNCollectionFilter : SLNFilter
{
	SLNFilter **filters;
	size_t count;
	size_t asize;
	int sort;
}
- (int)addFilterArg:(SLNFilter *const)filter;

- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID;
- (void)step:(int const)dir;

- (void)sort:(int const)dir;
@end
@interface SLNIntersectionFilter : SLNCollectionFilter
- (SLNAgeRange)fullAge:(uint64_t const)fileID;
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID;
@end
@interface SLNUnionFilter : SLNCollectionFilter
- (SLNAgeRange)fullAge:(uint64_t const)fileID;
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID;
@end

@interface SLNMetaFileFilter : SLNFilter
{
	DB_cursor *metafiles;
}
@end

// SLNBadMetaFileFilter.m
@interface SLNBadMetaFileFilter : SLNFilter
{
	SLNUnionFilter *main;
	SLNFilter *internal; // weak ref
	SLNFilter *subfilter; // weak ref
}
@end

// SLNNegationFilter.m
@interface SLNNegationFilter : SLNFilter
{
	SLNFilter *subfilter;
}
@end

// SLNDirectFilter.m
@interface SLNURIFilter : SLNFilter
{
	DB_txn *curtxn;
	str_t *URI;
	DB_cursor *files;
	DB_cursor *age;
}
@end
@interface SLNTargetURIFilter : SLNFilter
{
	DB_txn *curtxn;
	str_t *targetURI;
	DB_cursor *metafiles;
	DB_cursor *age;
}
@end
@interface SLNAllFilter : SLNFilter
{
	DB_cursor *files;
}
@end

static bool valid(uint64_t const x) {
	return 0 != x && UINT64_MAX != x;
}
static uint64_t invalid(int const dir) {
	if(dir < 0) return 0;
	if(dir > 0) return UINT64_MAX;
	assert(0 && "Invalid dir");
	return 0;
}

static void indent(size_t const depth) {
	for(size_t i = 0; i < depth; i++) fputc('\t', stderr);
}
static bool needs_quotes(strarg_t const str) {
	// TODO: Kind of a hack.
	for(size_t i = 0; '\0' != str[i]; i++) {
		if(isspace(str[i]) || '=' == str[i]) return true;
	}
	return false;
}
static size_t wr(str_t *const data, size_t const size, strarg_t const str) {
	return strlcpy(data, str, size);
}
static size_t wr_quoted(str_t *const data, size_t const size, strarg_t const str) {
	size_t len = 0;
	bool const quoted = needs_quotes(str);
	if(quoted) len += wr(data+len, size-len, "\"");
	len += wr(data+len, size-len, str);
	if(quoted) len += wr(data+len, size-len, "\"");
	return len;
}

