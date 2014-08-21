#include <assert.h>
#include "strndup.h"
#include "EarthFS.h"

typedef struct {
	count_t count;
	count_t size;
	EFSFilterRef items[0];
} EFSFilterList;

typedef struct EFSQueryFilter* EFSQueryFilterRef;
typedef struct EFSPermissionFilter* EFSPermissionFilterRef;
typedef struct EFSStringFilter* EFSStringFilterRef;
typedef struct EFSCollectionFilter* EFSCollectionFilterRef;

#define EFS_FILTER_BASE \
	EFSFilterType type;

struct EFSFilter {
	EFS_FILTER_BASE
};
struct EFSPermissionFilter {
	EFS_FILTER_BASE
	uint64_t userID;
};
struct EFSStringFilter {
	EFS_FILTER_BASE
	str_t *string;
	uint64_t field_id;
	uint64_t value_id;
};
struct EFSCollectionFilter {
	EFS_FILTER_BASE
	EFSFilterList *filters;
};

EFSFilterRef EFSFilterCreate(EFSFilterType const type) {
	EFSFilterRef filter = NULL;
	switch(type) {
		case EFSNoFilterType:
			filter = calloc(1, sizeof(struct EFSFilter));
			break;
		case EFSFileTypeFilterType:
		case EFSFullTextFilterType:
		case EFSLinkedFromFilterType:
		case EFSLinksToFilterType:
			filter = calloc(1, sizeof(struct EFSStringFilter));
			break;
		case EFSIntersectionFilterType:
		case EFSUnionFilterType:
			filter = calloc(1, sizeof(struct EFSCollectionFilter));
			break;
		default:
			assertf(0, "Invalid filter type %d", (int)type);
	}
	if(!filter) return NULL;
	filter->type = type;
	return filter;
}
EFSFilterRef EFSPermissionFilterCreate(uint64_t const userID) {
	EFSPermissionFilterRef filter = calloc(1, sizeof(struct EFSPermissionFilter));
	if(!filter) return NULL;
	filter->type = EFSPermissionFilterType;
	filter->userID = userID;
	return (EFSFilterRef)filter;
}
void EFSFilterFree(EFSFilterRef *const filterptr) {
	EFSFilterRef filter = *filterptr;
	if(!filter) return;
	switch(filter->type) {
		case EFSNoFilterType: {
			break;
		}
		case EFSPermissionFilterType: {
			EFSQueryFilterRef const f = (EFSQueryFilterRef)filter;
//			sqlite3_finalize(f->age); f->age = NULL;
			break;
		}
		case EFSFileTypeFilterType:
		case EFSFullTextFilterType:
		case EFSLinkedFromFilterType:
		case EFSLinksToFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
//			sqlite3_finalize(f->age); f->age = NULL;
			FREE(&f->string);
			break;
		}
		case EFSIntersectionFilterType:
		case EFSUnionFilterType: {
			EFSCollectionFilterRef const f = (EFSCollectionFilterRef)filter;
			EFSFilterList *const list = f->filters;
			if(list) for(index_t i = 0; i < list->count; ++i) {
				EFSFilterFree(&list->items[i]);
			}
			FREE(&f->filters);
			break;
		}
		default: {
			assertf(0, "Invalid filter type %d", (int)filter->type);
		}
	}
	FREE(filterptr); filter = NULL;
}
err_t EFSFilterAddStringArg(EFSFilterRef const filter, strarg_t const str, ssize_t const len) {
	if(!filter) return 0;
	switch(filter->type) {
		case EFSFileTypeFilterType:
		case EFSFullTextFilterType:
		case EFSLinkedFromFilterType:
		case EFSLinksToFilterType:
			break;
		default: return -1;
	}
	EFSStringFilterRef const f = (EFSStringFilterRef)filter;
	if(f->string) return -1;
	f->string = strndup(str, len < 0 ? SIZE_MAX : len);
	return 0;
}
err_t EFSFilterAddFilterArg(EFSFilterRef const filter, EFSFilterRef const subfilter) {
	if(!filter) return 0;
	switch(filter->type) {
		case EFSIntersectionFilterType:
		case EFSUnionFilterType:
			break;
		default: return -1;
	}
	EFSCollectionFilterRef const f = (EFSCollectionFilterRef)filter;
	EFSFilterList *list = f->filters;
	if(!list || list->count+1 > list->size) {
		count_t const size = list ? list->size*2 : 10;
		size_t const bytes = sizeof(EFSFilterList) + (sizeof(EFSFilterRef) * size);
		f->filters = realloc(f->filters, bytes);
		if(!f->filters) {
			FREE(&f->filters);
			return -1;
		}
		if(!list) {
			list = f->filters;
			list->count = 0;
		}
		list->size = size;
	}
	list->items[list->count++] = subfilter;
	return 0;
}
void EFSFilterPrint(EFSFilterRef const filter, count_t const indent) {
	if(!filter) {
		fprintf(stderr, "(null-filter)\n");
		return;
	}
	// TODO: Copy and paste is bad.
	for(index_t i = 0; i < indent; ++i) fprintf(stderr, "\t");
	switch(filter->type) {
		case EFSNoFilterType:
			fprintf(stderr, "(all)\n");
			break;
		case EFSPermissionFilterType:
			fprintf(stderr, "(permission %lld)\n", ((EFSPermissionFilterRef)filter)->userID);
			break;
		case EFSFileTypeFilterType:
			fprintf(stderr, "(file-type %s)\n", ((EFSStringFilterRef)filter)->string);
			break;
		case EFSFullTextFilterType:
			fprintf(stderr, "(full-text %s)\n", ((EFSStringFilterRef)filter)->string);
			break;
		case EFSLinkedFromFilterType:
			fprintf(stderr, "(linked-from %s)\n", ((EFSStringFilterRef)filter)->string);
			break;
		case EFSLinksToFilterType:
			fprintf(stderr, "(links-to %s)\n", ((EFSStringFilterRef)filter)->string);
			break;
		case EFSIntersectionFilterType: {
			fprintf(stderr, "(intersection\n");
			EFSCollectionFilterRef const f = (EFSCollectionFilterRef)filter;
			EFSFilterList const *const list = f->filters;
			for(index_t i = 0; i < list->count; ++i) {
				EFSFilterPrint(list->items[i], indent+1);
			}
			for(index_t i = 0; i < indent; ++i) fprintf(stderr, "\t");
			fprintf(stderr, ")\n");
			break;
		}
		case EFSUnionFilterType: {
			fprintf(stderr, "(union\n");
			EFSCollectionFilterRef const f = (EFSCollectionFilterRef)filter;
			EFSFilterList const *const list = f->filters;
			for(index_t i = 0; i < list->count; ++i) {
				EFSFilterPrint(list->items[i], indent+1);
			}
			for(index_t i = 0; i < indent; ++i) fprintf(stderr, "\t");
			fprintf(stderr, ")\n");
			break;
		}
		default:
			fprintf(stderr, "(unknown-%d)\n", filter->type);
			break;
	}
}

err_t EFSFilterPrepare(EFSFilterRef const filter, EFSConnection const *const conn, MDB_txn *const txn) {
	assert(filter);
	switch(filter->type) {
		case EFSNoFilterType: {
			return 0;
		}
		case EFSFileTypeFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			assert(0);
			return 0;
		}
		case EFSFullTextFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			// TODO: Stemming
			if(strlen(f->string) > 10) f->string[10] = '\0';
			return 0;
		}
		case EFSLinkedFromFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			assert(0);
			return 0;
		}
		case EFSLinksToFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			f->field_id = db_string_id(txn, conn->schema, "link");
			f->value_id = db_string_id(txn, conn->schema, f->string);
			return 0;
		}
		case EFSIntersectionFilterType:
		case EFSUnionFilterType: {
			EFSCollectionFilterRef const f = (EFSCollectionFilterRef)filter;
			EFSFilterList const *const list = f->filters;
			for(index_t i = 0; i < list->count; ++i) {
				EFSFilterPrepare(list->items[i], conn, txn);
			}
			return 0;
		}
		default: {
			assertf(0, "Unknown filter type %d\n", filter->type);
			return -1;
		}
	}
}


static uint64_t EFSCollectionFilterMatchAge(EFSFilterRef const f, uint64_t const fileID, uint64_t const sortID, EFSConnection const *const conn, MDB_txn *const txn);
static uint64_t EFSFilterMetaFileMatchAge(EFSFilterRef const filter, uint64_t const fileID, uint64_t const sortID, EFSConnection const *const conn, MDB_txn *const txn);
static uint64_t EFSFilterMetaFileAge(EFSFilterRef const filter, uint64_t const metaFileID, EFSConnection const *const conn, MDB_txn *const txn);

uint64_t EFSFilterMatchAge(EFSFilterRef const filter, uint64_t const fileID, uint64_t const sortID, EFSConnection const *const conn, MDB_txn *const txn) {
	assert(filter);
	// TODO: The minimum match age for any file should be its own fileID. Might be an opportunity for optimization.
	switch(filter->type) {
		case EFSNoFilterType:
			return fileID;
		case EFSFileTypeFilterType:
			assert(0); // TODO
			return 0;
		case EFSLinkedFromFilterType:
			assert(0); // TODO: This filter isn't based on the file's own meta-data, so we need a separate implementation for it.
			return 0;
		case EFSPermissionFilterType:
		case EFSFullTextFilterType:
		case EFSLinksToFilterType:
			return EFSFilterMetaFileMatchAge
				(filter, fileID, sortID, conn, txn);
		case EFSIntersectionFilterType:
		case EFSUnionFilterType:
			return EFSCollectionFilterMatchAge
				(filter, fileID, sortID, conn, txn);
		default:
			assertf(0, "Unknown filter type %d\n", filter->type);
			return INT64_MAX;
	}
}

static uint64_t EFSCollectionFilterMatchAge(EFSFilterRef const f, uint64_t const fileID, uint64_t const sortID, EFSConnection const *const conn, MDB_txn *const txn) {
	EFSCollectionFilterRef const filter = (EFSCollectionFilterRef)f;
	assert(filter);
	bool_t hit = false;
	EFSFilterList const *const list = filter->filters;
	for(index_t i = 0; i < list->count; ++i) {
		uint64_t const age = EFSFilterMatchAge(list->items[i], fileID, sortID, conn, txn);
		if(age == sortID) {
			hit = true;
		} else if(EFSIntersectionFilterType == filter->type) {
			if(age > sortID) return UINT64_MAX;
		} else if(EFSUnionFilterType == filter->type) {
			if(age < sortID) return 0;
		}
	}
	if(EFSIntersectionFilterType == filter->type) {
		if(!hit) return 0;
	} else if(EFSUnionFilterType == filter->type) {
		if(!hit) return UINT64_MAX;
	}
	return sortID;
}
static uint64_t EFSFilterMetaFileMatchAge(EFSFilterRef const filter, uint64_t const fileID, uint64_t const sortID, EFSConnection const *const conn, MDB_txn *const txn) {
	uint64_t youngest = UINT64_MAX;
	int rc;

	// TODO: Prepare cursors ahead of time
	MDB_cursor *URIs = NULL;
	rc = mdb_cursor_open(txn, conn->URIByFileID, &URIs);
	assert(MDB_SUCCESS == rc);
	MDB_cursor *metaFiles = NULL;
	rc = mdb_cursor_open(txn, conn->metaFileIDByTargetURI, &metaFiles);
	assert(MDB_SUCCESS == rc);

	DB_VAL(fileID_val, 1);
	db_bind(fileID_val, 0, fileID);
	MDB_val URI_val[1];
	rc = mdb_cursor_get(URIs, fileID_val, URI_val, MDB_SET);
	assert(MDB_SUCCESS == rc || MDB_NOTFOUND == rc);
	for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(URIs, fileID_val, URI_val, MDB_NEXT_DUP)) {
		uint64_t const targetURI_id = db_column(URI_val, 0);

		DB_VAL(targetURI_val, 1);
		db_bind(targetURI_val, 0, targetURI_id);
		MDB_val metaFileID_val[1];
		rc = mdb_cursor_get(metaFiles, targetURI_val, metaFileID_val, MDB_SET);
		assert(MDB_SUCCESS == rc || MDB_NOTFOUND == rc);
		for(; MDB_SUCCESS == rc; rc = mdb_cursor_get(metaFiles, targetURI_val, metaFileID_val, MDB_NEXT_DUP)) {
			uint64_t const metaFileID = db_column(metaFileID_val, 0);
			uint64_t const age = EFSFilterMetaFileAge(filter, metaFileID, conn, txn);
			if(UINT64_MAX == age) continue;
			if(age < youngest) youngest = age;
			break;
		}
	}
	mdb_cursor_close(metaFiles); metaFiles = NULL;
	mdb_cursor_close(URIs); URIs = NULL;
	return MAX(youngest, fileID); // No file can be younger than itself. We still have to check younger meta-files, though.
}
static uint64_t EFSFilterMetaFileAge(EFSFilterRef const filter, uint64_t const metaFileID, EFSConnection const *const conn, MDB_txn *const txn) {
	assert(filter);
	int rc;
	switch(filter->type) {
		case EFSPermissionFilterType:
			assert(0);
			return 0;
		case EFSFullTextFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			byte_t buf[20];

			byte_t *const data = buf;
			uint64_t const item = metaFileID;
			data[0] = 0xff & (item >> (8 * 7));
			data[1] = 0xff & (item >> (8 * 6));
			data[2] = 0xff & (item >> (8 * 5));
			data[3] = 0xff & (item >> (8 * 4));
			data[4] = 0xff & (item >> (8 * 3));
			data[5] = 0xff & (item >> (8 * 2));
			data[6] = 0xff & (item >> (8 * 1));
			data[7] = 0xff & (item >> (8 * 0));

			size_t const wlen = strlen(f->string);
			memcpy(buf+8, f->string, wlen);

			MDB_val stem_val[1] = {{ 8+wlen, buf }};
			MDB_val match_val[1];
			rc = mdb_get(txn, conn->fulltext, stem_val, match_val);
			if(MDB_NOTFOUND == rc) return UINT64_MAX;
			assert(MDB_SUCCESS == rc);

			// TODO: No copy and paste.

			// We could've used a wider metadata index to avoid this lookup, but it doesn't seem worth it.
			DB_VAL(metaFileID_val, 1);
			db_bind(metaFileID_val, 0, metaFileID);
			MDB_val metaFile_val[1];
			rc = mdb_get(txn, conn->metaFileByID, metaFileID_val, metaFile_val);
			assert(MDB_SUCCESS == rc);
			uint64_t const fileID = db_column(metaFile_val, 0);
			return fileID;
		}
		case EFSLinksToFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			DB_VAL(metadata_val, 3);
			db_bind(metadata_val, 0, f->value_id);
			db_bind(metadata_val, 1, f->field_id);
			db_bind(metadata_val, 2, metaFileID);
			MDB_val match_val[1];
			rc = mdb_get(txn, conn->metadata, metadata_val, match_val);
			if(MDB_NOTFOUND == rc) return UINT64_MAX;
			assert(MDB_SUCCESS == rc);

			// TODO: No copy and paste.

			// We could've used a wider metadata index to avoid this lookup, but it doesn't seem worth it.
			DB_VAL(metaFileID_val, 1);
			db_bind(metaFileID_val, 0, metaFileID);
			MDB_val metaFile_val[1];
			rc = mdb_get(txn, conn->metaFileByID, metaFileID_val, metaFile_val);
			assert(MDB_SUCCESS == rc);
			uint64_t const fileID = db_column(metaFile_val, 0);
			return fileID;
		}
		default:
			assertf(0, "Unexpected filter type %d\n", filter->type);
			return INT64_MAX;
	}
}

