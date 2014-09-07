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
- (bool_t)step:(int const)dir;
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
- (bool_t)step:(int const)dir;
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID;
@end
@interface EFSIndividualFilter (Abstract)
- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID;
- (uint64_t)currentMeta:(int const)dir;
- (uint64_t)stepMeta:(int const)dir;
- (bool_t)match:(uint64_t const)metaFileID;
@end

@interface EFSCollectionFilter : EFSFilter
{
	EFSFilter **filters;
	count_t count;
	count_t asize;
	bool_t sorted;
}
- (EFSFilter *)unwrap;
- (err_t)addFilterArg:(EFSFilter *const)filter;

- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID;
- (bool_t)step:(int const)dir;
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID;

- (void)sort:(int const)dir;
@end
@interface EFSCollectionFilter (Abstract)
// TODO
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

@implementation EFSIndividualFilter
- (void)free {
	mdb_cursor_close(step_target); step_target = NULL;
	mdb_cursor_close(step_files); step_files = NULL;
	mdb_cursor_close(age_uris); age_uris = NULL;
	mdb_cursor_close(age_metafiles); age_metafiles = NULL;
	[super free];
}

- (EFSFilter *)unwrap {
	return self;
}

- (err_t)prepare:(MDB_txn *const)txn :(EFSConnection const *const)conn {
	if([super prepare:txn :conn] < 0) return -1;
	db_cursor(txn, conn->metaFileByID, &step_target);
	db_cursor(txn, conn->fileIDByURI, &step_files);
	db_cursor(txn, conn->URIByFileID, &age_uris);
	db_cursor(txn, conn->metaFileIDByTargetURI, &age_metafiles);
	return 0;
}
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	int rc;
	uint64_t const actualSortID = [self seekMeta:dir :sortID];
	if(!valid(actualSortID)) return;
	DB_VAL(metaFileID_val, 1);
	db_bind(metaFileID_val, actualSortID);
	MDB_val metaFile_val[1];
	rc = mdb_cursor_get(step_target, metaFileID_val, metaFile_val, MDB_SET);
	assertf(MDB_SUCCESS == rc, "Database error %s", mdb_strerror(rc));
	uint64_t const targetURI_id = db_column(metaFile_val, 1);
	DB_VAL(targetURI_val, 1);
	db_bind(targetURI_val, targetURI_id);
	if(sortID == actualSortID) {
		DB_VAL(fileID_val, 1);
		db_bind(fileID_val, fileID);
		rc = mdb_cursor_get(step_files, targetURI_val, fileID_val, MDB_GET_BOTH_RANGE);
		if(MDB_SUCCESS != rc) return;
		uint64_t const actual = db_column(fileID_val, 0);
		if(fileID == actual) return (void)[self step:-dir];
		if(dir > 0) return (void)[self step:-1];
	} else {
		db_cursor_get(step_files, targetURI_val, NULL, MDB_SET);
	}
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	MDB_val fileID_val[1];
	int rc = db_cursor_get(step_files, NULL, fileID_val, MDB_GET_CURRENT);
	if(MDB_SUCCESS == rc) {
		if(sortID) *sortID = [self currentMeta:dir];
		if(fileID) *fileID = db_column(fileID_val, 0);
	} else {
		if(sortID) *sortID = invalid(dir);
		if(fileID) *fileID = invalid(dir);
	}
}
- (bool_t)step:(int const)dir {
	int rc;
	MDB_val fileID_val[1];

	rc = db_cursor_get(step_files, NULL, fileID_val, op(dir, MDB_NEXT_DUP));
	if(MDB_SUCCESS == rc) return true;

	for(uint64_t sortID = [self stepMeta:dir]; valid(sortID); sortID = [self stepMeta:dir]) {
		DB_VAL(metaFileID_val, 1);
		db_bind(metaFileID_val, sortID);
		MDB_val metaFile_val[1];
		rc = mdb_cursor_get(step_target, metaFileID_val, metaFile_val, MDB_SET);
		assertf(MDB_SUCCESS == rc, "Database error %s", mdb_strerror(rc));
		uint64_t const targetURI_id = db_column(metaFile_val, 1);
		DB_VAL(targetURI_val, 1);
		db_bind(targetURI_val, targetURI_id);
		rc = mdb_cursor_get(step_files, targetURI_val, NULL, MDB_SET);
		if(MDB_SUCCESS != rc) continue;
		rc = db_cursor_get(step_files, NULL, fileID_val, op(dir, MDB_FIRST_DUP));
		if(MDB_SUCCESS != rc) continue;
		return true;
	}
	return false;
}
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID {
	uint64_t earliest = UINT64_MAX;
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
			if(metaFileID < earliest) earliest = metaFileID;
			break;
		}
	}
	return earliest;
	// TODO: We should ensure that files aren't given positions
	// earlier than the submission of the files themselves. This
	// can happen if a meta-file is submitted before a file that
	// it targets. The problem is that files can only show up when
	// they are referenced by meta-files, so if we don't put it too
	// early, we have to put it too late (if/when another meta-file
	// is submitted for it later). Even if our ages were changed to
	// be file IDs rather than meta-file IDs (so we could use the
	// file's own ID as its lower-bound age), it wouldn't help,
	// because we wouldn't step() in the right place.
}
@end

static int filtercmp(EFSFilter *const a, EFSFilter *const b, int const dir) {
	uint64_t asort, afile, bsort, bfile;
	[a current:+1*dir :&asort :&afile];
	[b current:+1*dir :&bsort :&bfile];
	if(asort < bsort) return +1*dir;
	if(asort > bsort) return -1*dir;
	if(afile < bfile) return +1*dir;
	if(afile > bfile) return -1*dir;
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
	for(index_t i = 0; i < count; ++i) {
		if([filters[i] prepare:txn :conn] < 0) return -1;
	}
	sorted = false;
	return 0;
}
- (void)seek:(int const)dir :(uint64_t const)sortID :(uint64_t const)fileID {
	for(index_t i = 0; i < count; ++i) {
		[filters[i] seek:dir :sortID :fileID];
	}
}
- (void)current:(int const)dir :(uint64_t *const)sortID :(uint64_t *const)fileID {
	assert(count);
	if(!sorted) [self sort:dir];
	[filters[0] current:dir :sortID :fileID];
}
- (bool_t)step:(int const)dir {
	assert(count);
	if(!sorted) [self sort:dir];
	uint64_t oldSortID, oldFileID;
	[filters[0] current:dir :&oldSortID :&oldFileID];
	bool_t step = false;
	if([filters[0] step:dir]) step = true;
	for(index_t i = 1; i < count; ++i) {
		uint64_t curSortID, curFileID;
		[filters[i] current:dir :&curSortID :&curFileID];
		if(curSortID != oldSortID) break;
		if(curFileID != oldFileID) break;
		if([filters[i] step:dir]) step = true;
	}
	[self sort:dir];
	return step;
}
- (uint64_t)age:(uint64_t const)sortID :(uint64_t const)fileID {
	EFSFilterType const type = [self type]; // TODO: Polymorphism
	bool_t hit = false;
	// TODO: Maybe better to check in reverse order?
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
	sorted = true;
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
	mdb_cursor_close(metafiles); metafiles = NULL;
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
	db_cursor(txn, conn->metaFileByID, &metafiles);
	return 0;
}

- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID {
	DB_VAL(sortID_val, 1);
	db_bind(sortID_val, sortID);
	int rc = db_cursor_get(metafiles, sortID_val, NULL, MDB_SET_RANGE);
	if(MDB_SUCCESS != rc) return invalid(dir);
	uint64_t const actual = db_column(sortID_val, 0);
	if(sortID != actual && dir > 0) return [self stepMeta:-1];
	return actual;
}
- (uint64_t)currentMeta:(int const)dir {
	MDB_val metaFileID_val[1];
	int rc = mdb_cursor_get(metafiles, metaFileID_val, NULL, MDB_GET_CURRENT);
	if(MDB_SUCCESS != rc) return invalid(dir);
	return db_column(metaFileID_val, 0);
}
- (uint64_t)stepMeta:(int const)dir {
	MDB_val metaFileID_val[1];
	int rc = db_cursor_get(metafiles, metaFileID_val, NULL, op(dir, MDB_NEXT));
	if(MDB_SUCCESS == rc) return db_column(metaFileID_val, 0);
	return invalid(dir);
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
	mdb_cursor_close(metafiles); metafiles = NULL;
	mdb_cursor_close(match); match = NULL;
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
	db_cursor(txn, conn->metaFileIDByFulltext, &metafiles);
	db_cursor(txn, conn->metaFileIDByFulltext, &match);
	return 0;
}

- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID {
	MDB_val token_val = { tokens[0].len, tokens[0].str };
	DB_VAL(sortID_val, 1);
	db_bind(sortID_val, sortID);
	int rc = db_cursor_get(metafiles, &token_val, sortID_val, MDB_GET_BOTH_RANGE);
	if(MDB_SUCCESS != rc) return invalid(dir);
	uint64_t const actual = db_column(sortID_val, 0);
	if(sortID != actual && dir > 0) return [self stepMeta:-1];
	return actual;
}
- (uint64_t)currentMeta:(int const)dir {
	MDB_val metaFileID_val[1];
	int rc = db_cursor_get(metafiles, NULL, metaFileID_val, MDB_GET_CURRENT);
	if(MDB_SUCCESS != rc) return invalid(dir);
	return db_column(metaFileID_val, 0);
}
- (uint64_t)stepMeta:(int const)dir {
	int rc;
	MDB_val metaFileID_val[1];
	rc = db_cursor_get(metafiles, NULL, metaFileID_val, op(dir, MDB_NEXT_DUP));
	if(MDB_SUCCESS == rc) return db_column(metaFileID_val, 0);
	// MDB bug workaround: MDB_NEXT/PREV_DUP with key doesn't initialize cursor.
	MDB_val token_val = { tokens[0].len, tokens[0].str };
	rc = db_cursor_get(metafiles, &token_val, metaFileID_val, op(dir, MDB_FIRST_DUP));
	if(MDB_SUCCESS == rc) return db_column(metaFileID_val, 0);
	return invalid(dir);
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
	mdb_cursor_close(metafiles); metafiles = NULL;
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
	db_cursor(txn, conn->metaFileIDByMetadata, &metafiles);
	db_cursor(txn, conn->metaFileIDByMetadata, &match);
	return 0;
}

- (uint64_t)seekMeta:(int const)dir :(uint64_t const)sortID {
	DB_VAL(metadata_val, 2);
	db_bind(metadata_val, value_id);
	db_bind(metadata_val, field_id);
	DB_VAL(sortID_val, 1);
	db_bind(sortID_val, sortID);
	int rc = db_cursor_get(metafiles, metadata_val, sortID_val, MDB_GET_BOTH_RANGE);
	if(MDB_SUCCESS != rc) return invalid(dir);
	uint64_t const actual = db_column(sortID_val, 0);
	if(sortID != actual && dir > 0) return [self stepMeta:-1];
	return actual;
}
- (uint64_t)currentMeta:(int const)dir {
	MDB_val metaFileID_val[1];
	int rc = db_cursor_get(metafiles, NULL, metaFileID_val, MDB_GET_CURRENT);
	if(MDB_SUCCESS != rc) return invalid(dir);
	return db_column(metaFileID_val, 0);
}
- (uint64_t)stepMeta:(int const)dir {
	int rc;
	MDB_val metaFileID_val[1];
	rc = db_cursor_get(metafiles, NULL, metaFileID_val, op(dir, MDB_NEXT_DUP));
	if(MDB_SUCCESS == rc) return db_column(metaFileID_val, 0);
	// MDB bug workaround: MDB_NEXT/PREV_DUP with key doesn't initialize cursor.
	DB_VAL(metadata_val, 2);
	db_bind(metadata_val, value_id);
	db_bind(metadata_val, field_id);
	rc = db_cursor_get(metafiles, metadata_val, metaFileID_val, op(dir, MDB_FIRST_DUP));
	if(MDB_SUCCESS == rc) return db_column(metaFileID_val, 0);
	return invalid(dir);
}
- (bool_t)match:(uint64_t const)metaFileID {
	DB_VAL(metadata_val, 2);
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
void EFSFilterSeek(EFSFilterRef const filter, int const dir, uint64_t const sortID, uint64_t const fileID) {
	[(EFSFilter *)filter seek:dir :sortID :fileID];
}
bool_t EFSFilterStep(EFSFilterRef const filter, int const dir, uint64_t *const sortID, uint64_t *const fileID) {
	assert(filter);
	if(dir && ![(EFSFilter *)filter step:dir]) return false;
	[(EFSFilter *)filter current:dir :sortID :fileID];
	return true;
}
uint64_t EFSFilterAge(EFSFilterRef const filter, uint64_t const sortID, uint64_t const fileID) {
	assert(filter);
	return [(EFSFilter *)filter age:sortID :fileID];
}
str_t *EFSFilterCopyNextURI(EFSFilterRef const filter, int const dir, MDB_txn *const txn, EFSConnection const *const conn) {
	for(;;) {
		uint64_t sortID, fileID;
		if(!EFSFilterStep(filter, dir, &sortID, &fileID)) return NULL;
//		fprintf(stderr, "step: %llu, %llu\n", sortID, fileID);

		uint64_t const age = EFSFilterAge(filter, sortID, fileID);
//		fprintf(stderr, "{%llu, %llu} -> %llu\n", sortID, fileID, age);
		if(age != sortID) continue;

		DB_VAL(fileID_val, 1);
		db_bind(fileID_val, fileID);
		MDB_val file_val[1];
		int rc = mdb_get(txn, conn->fileByID, fileID_val, file_val);
		assertf(MDB_SUCCESS == rc, "Database error %s", mdb_strerror(rc));

		strarg_t const hash = db_column_text(txn, conn->schema, file_val, 0);
		assert(hash);
		return EFSFormatURI(EFS_INTERNAL_ALGO, hash);
	}
}


