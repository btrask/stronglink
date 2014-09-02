#include <assert.h>
#include <ctype.h>
#include "fts.h"
#include "strndup.h"
#include "EarthFS.h"

@interface EFSObject
{
	Class isa;
}
+ (id)alloc;
- (id)init;
- (void)free;
@end

@interface EFSFilter : EFSObject
- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn;
@end
@interface EFSFilter (Abstract)
- (EFSFilterType)type;
- (EFSFilter *)unwrap;
- (strarg_t)stringArg:(index_t const)i;
- (err_t)addStringArg:(strarg_t const)str :(size_t const)len;
- (err_t)addFilterArg:(EFSFilter *const)filter;
- (void)print:(count_t const)depth;
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth;

- (uint64_t)step:(int const)dir;
- (uint64_t)age:(uint64_t const)fileID :(uint64_t const)sortID;
@end

@interface EFSIndividualFilter : EFSFilter
{
	MDB_cursor *age_uris;
	MDB_cursor *age_metafiles;
}
- (EFSFilter *)unwrap;
- (uint64_t)age:(uint64_t const)fileID :(uint64_t const)sortID;
@end
@interface EFSIndividualFilter (Abstract)
- (bool_t)match:(uint64_t const)metaFileID;
@end

@interface EFSCollectionFilter : EFSFilter
{
	EFSFilter **filters;
	count_t count;
	count_t asize;
}
- (EFSFilter *)unwrap;
- (err_t)addFilterArg:(EFSFilter *const)filter;
- (uint64_t)step:(int const)dir;

- (void)sort;
@end
@interface EFSCollectionFilter (Abstract)
// TODO
@end

@interface EFSAllFilter : EFSIndividualFilter
{
	MDB_cursor *step_metafiles;
	MDB_cursor *step_files;
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
	MDB_cursor *step_metafiles;
	MDB_cursor *step_phrase; // TODO
	MDB_cursor *step_files;
	MDB_cursor *match;
}
@end

@interface EFSMetadataFilter : EFSIndividualFilter
{
	str_t *field;
	str_t *value;
	uint64_t field_id;
	uint64_t value_id;
	MDB_cursor *match;
}
@end

@interface EFSIntersectionFilter : EFSCollectionFilter
@end
@interface EFSUnionFilter : EFSCollectionFilter
@end


#include <objc/runtime.h>

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
- (err_t)addStringArg:(strarg_t const)str :(size_t const)len {
	return -1;
}
- (err_t)addFilterArg:(EFSFilter *const)filter {
	return -1;
}
- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	return 0;
}
@end

@implementation EFSIndividualFilter
- (void)free {
	mdb_cursor_close(age_uris); age_uris = NULL;
	mdb_cursor_close(age_metafiles); age_metafiles = NULL;
	[super free];
}

- (EFSFilter *)unwrap {
	return self;
}
- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor(txn, conn->URIByFileID, &age_uris);
	db_cursor(txn, conn->metaFileIDByTargetURI, &age_metafiles);
	return 0;
}
- (uint64_t)age:(uint64_t const)fileID :(uint64_t const)sortID {
	uint64_t youngest = UINT64_MAX;
	int rc;

	DB_VAL(fileID_val, 1);
	db_bind(fileID_val, fileID);
	MDB_val URI_val[1];
	rc = mdb_cursor_get(age_uris, fileID_val, URI_val, MDB_SET);
	assert(MDB_SUCCESS == rc || MDB_NOTFOUND == rc);
	for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(age_uris, fileID_val, URI_val, MDB_NEXT_DUP)) {
		uint64_t const targetURI_id = db_column(URI_val, 0);

		DB_VAL(targetURI_val, 1);
		db_bind(targetURI_val, targetURI_id);
		MDB_val metaFileID_val[1];
		rc = mdb_cursor_get(age_metafiles, targetURI_val, metaFileID_val, MDB_SET);
		assert(MDB_SUCCESS == rc || MDB_NOTFOUND == rc);
		for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(age_metafiles, targetURI_val, metaFileID_val, MDB_NEXT_DUP)) {
			uint64_t const metaFileID = db_column(metaFileID_val, 0);
			if(metaFileID > sortID) break;
			if(![self match:metaFileID]) continue;
			if(metaFileID < youngest) youngest = metaFileID;
			break;
		}
	}
	return MAX(youngest, fileID); // No file can be younger than itself. We still have to check younger meta-files, though.
}
@end

static int filtercmp(EFSFilter *const *const _a, EFSFilter *const *const _b) {
	// We use the full range from 0 to UINT64_MAX, so be careful about overflows and underflows. Don't just `return a - b`.
	uint64_t const a = [*_a step:0];
	uint64_t const b = [*_b step:0];
	if(a < b) return -1;
	if(a > b) return 1;
	return 0;
}
@implementation EFSCollectionFilter
- (void)free {
	FREE(&filters);
	[super free];
}

- (EFSFilter *)unwrap {
	if(1 == count) return filters[0];
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

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	for(index_t i = 0; i < count; ++i) [filters[i] prepare:txn :conn];
	[self sort];
	return 0;
}
- (uint64_t)step:(int const)dir {
	assert(count);
	uint64_t const old = [filters[0] step:0];
	if(0 == dir) return old;
	[filters[0] step:dir];
	for(index_t i = 1; i < count; ++i) {
		uint64_t const cur = [filters[i] step:0];
		if(cur != old) break;
		[filters[i] step:dir];
	}
	[self sort];
	return [filters[0] step:0];
}
- (uint64_t)age:(uint64_t const)fileID :(uint64_t const)sortID {
	EFSFilterType const type = [self type]; // TODO: Polymorphism
	bool_t hit = false;
	for(index_t i = 0; i < count; ++i) {
		uint64_t const age = [filters[i] age:fileID :sortID];
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

- (void)sort {
	qsort(filters, count, sizeof(filters[0]), (int (*)())filtercmp);
}
@end

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

@implementation EFSAllFilter
- (void)free {
	mdb_cursor_close(step_metafiles); step_metafiles = NULL;
	mdb_cursor_close(step_files); step_files = NULL;
	[super free];
}

- (EFSFilterType)type {
	return EFSAllFilterType;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(all)\n");
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	if(depth) return wr(data, size, "*");
	return wr(data, size, "");
}

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor(txn, conn->metaFileByID, &step_metafiles);
	db_cursor(txn, conn->fileIDByURI, &step_files);
	return 0;
}
- (uint64_t)step:(int const)dir {
	return 0; // TODO
}

- (bool_t)match:(uint64_t const)metaFileID {
	return true;
}
@end

@implementation EFSFulltextFilter
- (void)free {
	FREE(&term);
	for(index_t i = 0; i < count; ++i) {
		FREE(&tokens[i].str);
	}
	FREE(&tokens);
	mdb_cursor_close(step_metafiles); step_metafiles = NULL;
	mdb_cursor_close(step_files); step_files = NULL;
	[super free];
}

- (EFSFilterType)type {
	return EFSFulltextFilterType;
}
- (strarg_t)stringArg:(index_t const)i {
	if(0 != i) return NULL;
	return term;
}
- (err_t)addStringArg:(strarg_t const)str :(size_t const)len {
	assert(str);
	assert(len);

	if(term) return -1;
	term = strndup(str, len);

	// TODO: libstemmer?
	sqlite3_tokenizer_module const *fts = NULL;
	sqlite3_tokenizer *tokenizer = NULL;
	fts_get(&fts, &tokenizer);

	sqlite3_tokenizer_cursor *tcur = NULL;
	int rc = fts->xOpen(tokenizer, str, len, &tcur);
	assert(SQLITE_OK == rc);

	for(;;) {
		strarg_t token;
		int tlen;
		int ignored1, ignored2, ignored3;
		rc = fts->xNext(tcur, &token, &tlen, &ignored1, &ignored2, &ignored3);
		if(SQLITE_OK != rc) break;
		if(count+1 > asize) {
			asize = MAX(8, asize*2);
			tokens = realloc(tokens, sizeof(tokens[0]) * asize);
			assert(tokens); // TODO
		}
		tokens[count].str = strndup(token, tlen);
		tokens[count].len = tlen;
		count++;
	}

	fts->xClose(tcur);
	return 0;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(fulltext %s)\n", term);
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	return wr_quoted(data, size, term);
}

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor(txn, conn->metaFileIDByFulltext, &step_metafiles);
	db_cursor(txn, conn->fileIDByURI, &step_files);
	db_cursor(txn, conn->metaFileIDByFulltext, &match);

//	MDB_val token_val = { tokens[0].len, tokens[0].str };
//	MDB_val metaFileID_val;
//	mdb_cursor_get(tokens[0].cursor, &token_val, &metaFileID_val, MDB_LAST_DUP);
	return 0;
}
- (uint64_t)step:(int const)dir {
/*
	if(UINT64_MAX != sortID)



	MDB_val token_val = { tokens[0].len, tokens[0].str };
	MDB_val sortID_val;
	int rc = mdb_cursor_get(step_metafiles, &token_val, &sortID_val, MDB_PREV_DUP);
	if(MDB_NOTFOUND == rc) return 0;
	assert(MDB_SUCCESS == rc);
	sortID = db_column(&sortID_val, 0);

	MDB_val metaFile_val;
	mdb_get(txn, conn->metaFileByID, &sortID_val, &metaFile_val);
	targetURI_id = db_column(&metaFile_val, 1);

	DB_VAL(URI_val, 1);
	db_bind(URI_val, targetURI_id);
	MDB_val fileID_val;
	rc = mdb_cursor_get(step_files, URI_val, fileID_val, MDB_PREV_DUP);


*/
	return 0; // TODO
}

- (bool_t)match:(uint64_t const)metaFileID {
	MDB_val token_val = { tokens[0].len, tokens[0].str };
	DB_VAL(metaFileID_val, 1);
	db_bind(metaFileID_val, metaFileID);
	int rc = mdb_cursor_get(match, &token_val, metaFileID_val, MDB_GET_BOTH);
	if(MDB_SUCCESS == rc) return true;
	if(MDB_NOTFOUND == rc) return false;
	assertf(0, "Database error %s", mdb_strerror(rc));
}
@end

@implementation EFSMetadataFilter
- (void)free {
	FREE(&field);
	FREE(&value);
	field_id = 0;
	value_id = 0;
	mdb_cursor_close(match); match = NULL;
	[super free];
}

- (EFSFilterType)type {
	return EFSMetadataFilterType;
}
- (strarg_t)stringArg:(index_t const)i {
	switch(i) {
		case 0: return field;
		case 1: return value;
		default: return NULL;
	}
}
- (err_t)addStringArg:(strarg_t const)str :(size_t const)len {
	if(!field) {
		field = strndup(str, len);
		return 0;
	}
	if(!value) {
		value = strndup(str, len);
		return 0;
	}
	return -1;
}
- (void)print:(count_t const)depth {
	indent(depth);
	fprintf(stderr, "(metadata \"%s\" \"%s\")\n", field, value);
}
- (size_t)getUserFilter:(str_t *const)data :(size_t const)size :(count_t const)depth {
	size_t len = 0;
	len += wr_quoted(data+len, size-len, field);
	len += wr(data+len, size-len, "=");
	len += wr_quoted(data+len, size-len, value);
	return len;
}

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	if(!field || !value) return -1;
	if(!field_id) field_id = db_string_id(txn, conn->schema, field);
	if(!value_id) value_id = db_string_id(txn, conn->schema, value);
	db_cursor(txn, conn->metaFileIDByMetadata, &match);
	return 0;
}
- (uint64_t)step:(int const)dir {
	return 0; // TODO
}

- (bool_t)match:(uint64_t const)metaFileID {
	DB_VAL(metadata_val, 3);
	db_bind(metadata_val, value_id);
	db_bind(metadata_val, field_id);
	DB_VAL(metaFileID_val, 1);
	db_bind(metaFileID_val, metaFileID);
	int rc = mdb_cursor_get(match, metadata_val, metaFileID_val, MDB_GET_BOTH);
	if(MDB_SUCCESS == rc) return true;
	if(MDB_NOTFOUND == rc) return false;
	assertf(0, "Database error %s", mdb_strerror(rc));
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
	return [(EFSFilter *)filter unwrap];
}
strarg_t EFSFilterGetStringArg(EFSFilterRef const filter, index_t const i) {
	assert(filter);
	return [(EFSFilter *)filter stringArg:i];
}
err_t EFSFilterAddStringArg(EFSFilterRef const filter, strarg_t const str, ssize_t const len) {
	assert(filter);
	return [(EFSFilter *)filter addStringArg:str :len];
}
err_t EFSFilterAddFilterArg(EFSFilterRef const filter, EFSFilterRef const subfilter) {
	assert(filter);
	return [(EFSFilter *)filter addFilterArg:subfilter];
}
void EFSFilterPrint(EFSFilterRef const filter, count_t const depth) {
	assert(filter);
	return [(EFSFilter *)filter print:depth];
}
size_t EFSFilterToUserFilterString(EFSFilterRef const filter, str_t *const data, size_t const size, count_t const depth) {
	assert(filter);
	return [(EFSFilter *)filter getUserFilter:data :size :depth];
}
err_t EFSFilterPrepare(EFSFilterRef const filter, MDB_txn *const txn, EFSConnection const *const conn) {
	assert(filter);
	return [(EFSFilter *)filter prepare:txn :conn];
}
uint64_t EFSFilterStep(EFSFilterRef const filter, int const dir) {
	assert(filter);
	return [(EFSFilter *)filter step:dir];
}
uint64_t EFSFilterAge(EFSFilterRef const filter, uint64_t const fileID, uint64_t const sortID) {
	assert(filter);
	return [(EFSFilter *)filter age:fileID :sortID];
}

