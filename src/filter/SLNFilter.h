#include <assert.h>
#include <ctype.h>
#include <objc/runtime.h>
#include "../StrongLink.h"
#include "../SLNDB.h"

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

@interface SLNFilter : SLNObject
@end
@interface SLNFilter (Abstract)
- (SLNFilterType)type;
- (SLNFilter *)unwrap;
- (strarg_t)stringArg:(index_t const)i;
- (int)addStringArg:(strarg_t const)str :(size_t const)len;
- (int)addFilterArg:(SLNFilter *const)filter;
- (void)print:(count_t const)depth;
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth;

- (int)prepare:(DB_txn *const)txn;
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID;
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID;
- (void)step:(int const)dir;
- (SLNAgeRange)fullAge:(uint64_t const)fileID;
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID;
@end

@interface SLNIndividualFilter : SLNFilter
{
	DB_txn *curtxn;
	DB_cursor *step_target;
	DB_cursor *step_files;
	DB_cursor *age_uris;
	DB_cursor *age_metafiles;
}
- (SLNFilter *)unwrap;

- (int)prepare:(DB_txn *const)txn;
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID;
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID;
- (void)step:(int const)dir;
- (SLNAgeRange)fullAge:(uint64_t const)fileID;
- (uint64_t)fastAge:(uint64_t const)fileID :(uint64_t const)sortID;
@end
@interface SLNIndividualFilter (Abstract)
- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID;
- (uint64_t)currentMeta:(int const)dir;
- (uint64_t)stepMeta:(int const)dir;
- (bool)match:(uint64_t const)metaFileID;
@end

@interface SLNAllFilter : SLNIndividualFilter
{
	DB_cursor *metafiles;
}
@end

struct token {
	str_t *str;
};
@interface SLNFulltextFilter : SLNIndividualFilter
{
	str_t *term;
	struct token *tokens;
	count_t count;
	count_t asize;
	DB_cursor *metafiles;
	DB_cursor *phrase; // TODO
	DB_cursor *match;
}
@end

@interface SLNMetadataFilter : SLNIndividualFilter
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
	count_t count;
	count_t asize;
	int sort;
}
- (SLNFilter *)unwrap;
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

static bool valid(uint64_t const x) {
	return 0 != x && UINT64_MAX != x;
}
static uint64_t invalid(int const dir) {
	if(dir < 0) return 0;
	if(dir > 0) return UINT64_MAX;
	assert(0 && "Invalid dir");
	return 0;
}

static void indent(count_t const depth) {
	for(index_t i = 0; i < depth; ++i) fprintf(stderr, "\t");
}
static bool needs_quotes(strarg_t const str) {
	for(index_t i = 0; '\0' != str[i]; ++i) {
		if(isspace(str[i])) return true;
	}
	return false;
}
static size_t wr(str_t *const data, size_t const size, strarg_t const str) {
	size_t const len = MIN(size, strlen(str));
	memcpy(data, str, len);
	if(len < size) data[len] = '\0';
	return len;
}
static size_t wr_quoted(str_t *const data, size_t const size, strarg_t const str) {
	size_t len = 0;
	bool const quoted = needs_quotes(str);
	if(quoted) len += wr(data+len, size-len, "\"");
	len += wr(data+len, size-len, str);
	if(quoted) len += wr(data+len, size-len, "\"");
	return len;
}

