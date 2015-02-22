#include <assert.h>
#include <ctype.h>
#include <objc/runtime.h>
#include "../EarthFS.h"

@interface EFSObject
{
	Class isa;
}
+ (id)alloc;
- (id)init;
- (void)free;
@end

@interface EFSFilter : EFSObject
@end
@interface EFSFilter (Abstract)
- (EFSFilterType)type;
- (EFSFilter *)unwrap;
- (strarg_t)stringArg:(index_t const)i;
- (int)addStringArg:(strarg_t const)str :(size_t const)len;
- (int)addFilterArg:(EFSFilter *const)filter;
- (void)print:(count_t const)depth;
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth;

- (int)prepare:(DB_txn *const)txn;
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID;
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID;
- (void)step:(int const)dir;
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID;
@end

@interface EFSIndividualFilter : EFSFilter
{
	DB_txn *curtxn;
	DB_cursor *step_target;
	DB_cursor *step_files;
	DB_cursor *age_uris;
	DB_cursor *age_metafiles;
}
- (EFSFilter *)unwrap;

- (int)prepare:(DB_txn *const)txn;
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID;
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID;
- (void)step:(int const)dir;
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID;
@end
@interface EFSIndividualFilter (Abstract)
- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID;
- (uint64_t)currentMeta:(int const)dir;
- (uint64_t)stepMeta:(int const)dir;
- (bool)match:(uint64_t const)metaFileID;
@end

@interface EFSAllFilter : EFSIndividualFilter
{
	DB_cursor *metafiles;
}
@end

struct token {
	str_t *str;
};
@interface EFSFulltextFilter : EFSIndividualFilter
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

@interface EFSMetadataFilter : EFSIndividualFilter
{
	str_t *field;
	str_t *value;
	DB_cursor *metafiles;
	DB_cursor *match;
}
@end

// EFSCollectionFilter.m
@interface EFSCollectionFilter : EFSFilter
{
	EFSFilter **filters;
	count_t count;
	count_t asize;
	int sort;
}
- (EFSFilter *)unwrap;
- (int)addFilterArg:(EFSFilter *const)filter;

- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID;
- (void)step:(int const)dir;
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID;

- (void)sort:(int const)dir;
@end
@interface EFSIntersectionFilter : EFSCollectionFilter
@end
@interface EFSUnionFilter : EFSCollectionFilter
@end

// EFSMetaFileFilter.m
@interface EFSMetaFileFilter : EFSFilter
{
	EFSUnionFilter *main;
	EFSFilter *internal; // weak ref
	EFSFilter *subfilter; // weak ref
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

