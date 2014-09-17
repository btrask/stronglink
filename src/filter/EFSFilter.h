#include <assert.h>
#include <ctype.h>
#include <objc/runtime.h>
#include "../strndup.h"
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
- (err_t)addStringArg:(strarg_t const)str :(size_t const)len;
- (err_t)addFilterArg:(EFSFilter *const)filter;
- (void)print:(count_t const)depth;
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth;

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn;
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID;
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID;
- (void)step:(int const)dir;
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID;
@end

@interface EFSIndividualFilter : EFSFilter
{
	MDB_cursor *step_target;
	MDB_cursor *step_files;
	MDB_cursor *age_uris;
	MDB_cursor *age_metafiles;
}
- (EFSFilter *)unwrap;

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn;
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID;
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID;
- (void)step:(int const)dir;
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID;
@end
@interface EFSIndividualFilter (Abstract)
- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID;
- (uint64_t)currentMeta:(int const)dir;
- (uint64_t)stepMeta:(int const)dir;
- (bool_t)match:(uint64_t const)metaFileID;
@end

@interface EFSAllFilter : EFSIndividualFilter
{
	MDB_cursor *metafiles;
}
@end

struct token {
	str_t *str;
	size_t len;
};
@interface EFSFulltextFilter : EFSIndividualFilter
{
	str_t *term;
	struct token *tokens;
	count_t count;
	count_t asize;
	MDB_cursor *metafiles;
	MDB_cursor *phrase; // TODO
	MDB_cursor *match;
}
@end

@interface EFSMetadataFilter : EFSIndividualFilter
{
	str_t *field;
	str_t *value;
	uint64_t field_id;
	uint64_t value_id;
	MDB_cursor *metafiles;
	MDB_cursor *match;
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
- (err_t)addFilterArg:(EFSFilter *const)filter;

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

static bool_t valid(uint64_t const x) {
	return 0 != x && UINT64_MAX != x;
}
static uint64_t invalid(int const dir) {
	if(dir < 0) return 0;
	if(dir > 0) return UINT64_MAX;
	assert(9 && "Invalid dir");
	return 0;
}
static MDB_cursor_op op(int const dir, MDB_cursor_op const x) {
	switch(x) {
		case MDB_PREV_DUP: return op(-dir, MDB_NEXT_DUP);
		case MDB_NEXT_DUP:
			if(dir < 0) return MDB_PREV_DUP;
			if(dir > 0) return MDB_NEXT_DUP;
			break;
		case MDB_FIRST_DUP: return op(-dir, MDB_LAST_DUP);
		case MDB_LAST_DUP:
			if(dir < 0) return MDB_FIRST_DUP;
			if(dir > 0) return MDB_LAST_DUP;
			break;
		case MDB_PREV: return op(-dir, MDB_NEXT);
		case MDB_NEXT:
			if(dir < 0) return MDB_PREV;
			if(dir > 0) return MDB_NEXT;
			break;
		case MDB_FIRST: return op(-dir, MDB_LAST);
		case MDB_LAST:
			if(dir < 0) return MDB_FIRST;
			if(dir > 0) return MDB_LAST;
			break;
		default: break;
	}
	assert(0);
	return 0;
}

static void indent(count_t const depth) {
	for(index_t i = 0; i < depth; ++i) fprintf(stderr, "\t");
}
static bool_t needs_quotes(strarg_t const str) {
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
	bool_t const quoted = needs_quotes(str);
	if(quoted) len += wr(data+len, size-len, "\"");
	len += wr(data+len, size-len, str);
	if(quoted) len += wr(data+len, size-len, "\"");
	return len;
}

