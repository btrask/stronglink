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
#define EFS_FILTER_QUERY \
	sqlite3_stmt *age; \
	int arg;

struct EFSFilter {
	EFS_FILTER_BASE
};
struct EFSQueryFilter {
	EFS_FILTER_BASE
	EFS_FILTER_QUERY
};
struct EFSPermissionFilter {
	EFS_FILTER_BASE
	EFS_FILTER_QUERY
	int64_t userID;
};
struct EFSStringFilter {
	EFS_FILTER_BASE
	EFS_FILTER_QUERY
	str_t *string;
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
EFSFilterRef EFSPermissionFilterCreate(int64_t const userID) {
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
			sqlite3_finalize(f->age); f->age = NULL;
			break;
		}
		case EFSFileTypeFilterType:
		case EFSFullTextFilterType:
		case EFSLinkedFromFilterType:
		case EFSLinksToFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			sqlite3_finalize(f->age); f->age = NULL;
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

err_t EFSFilterPrepare(EFSFilterRef const filter, sqlite3f *const db) {
	assert(filter);
	switch(filter->type) {
		case EFSNoFilterType: {
			return 0;
		}
		case EFSFileTypeFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			f->age = QUERY_UNCACHED(db,
				"SELECT MIN(file_id) AS age\n"
				"FROM files\n"
				"WHERE file_type = ? AND file_id = ?");
			sqlite3_bind_text(f->age, 1, f->string, -1, SQLITE_STATIC);
			f->arg = 2;
			return 0;
		}
		case EFSFullTextFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			f->age = QUERY_UNCACHED(db,
				"SELECT MIN(mf.file_id) AS age\n"
				"FROM fulltext AS ft\n"
				"INNER JOIN meta_fulltext AS mft\n"
				"	ON (mft.docid = ft.docid)\n"
				"INNER JOIN meta_files AS mf\n"
				"	ON (mf.meta_file_id = mft.meta_file_id)\n"
				"INNER JOIN file_uris AS f\n"
				"	ON (f.uri = mf.target_uri)\n"
				"WHERE ft.value MATCH ? AND f.file_id = ?");
			sqlite3_bind_text(f->age, 1, f->string, -1, SQLITE_STATIC);
			f->arg = 2;
			return 0;
		}
		case EFSLinkedFromFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			f->age = QUERY_UNCACHED(db,
				"SELECT MIN(mf.file_id) AS age\n"
				"FROM file_uris AS f\n"
				"INNER JOIN meta_data AS md\n"
				"	ON (md.value = f.uri)\n"
				"INNER JOIN meta_files AS mf\n"
				"	ON (mf.meta_file_id = md.meta_file_id)\n"
				"WHERE md.field = 'link'\n"
				"AND f.uri = ?\n"
				"AND f.file_id = ?");
			sqlite3_bind_text(f->age, 1, f->string, -1, SQLITE_STATIC);
			f->arg = 2;
			return 0;
		}
		case EFSLinksToFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			f->age = QUERY_UNCACHED(db,
				"SELECT MIN(mf.file_id) AS age\n"
				"FROM file_uris AS f\n"
				"INNER JOIN meta_files AS mf\n"
				"	ON (mf.target_uri = f.uri)\n"
				"INNER JOIN meta_data AS md\n"
				"	ON (md.meta_file_id = mf.meta_file_id)\n"
				"WHERE md.field = 'link'\n"
				"AND md.value = ?\n"
				"AND f.file_id = ?");
			sqlite3_bind_text(f->age, 1, f->string, -1, SQLITE_STATIC);
			f->arg = 2;
			return 0;
		}
		case EFSIntersectionFilterType:
		case EFSUnionFilterType: {
			EFSCollectionFilterRef const f = (EFSCollectionFilterRef)filter;
			EFSFilterList const *const list = f->filters;
			for(index_t i = 0; i < list->count; ++i) {
				EFSFilterPrepare(list->items[i], db);
			}
			return 0;
		}
		default: {
			assertf(0, "Unknown filter type %d\n", filter->type);
			return -1;
		}
	}
}
static int64_t EFSCollectionFilterMatchAge(EFSCollectionFilterRef const filter, int64_t const sortID, int64_t const fileID) {
	assert(filter);
	bool_t hit = false;
	EFSFilterList const *const list = filter->filters;
	for(index_t i = 0; i < list->count; ++i) {
		int64_t const age = EFSFilterMatchAge(list->items[i], sortID, fileID);
		if(age == sortID) {
			hit = true;
		} else if(EFSIntersectionFilterType == filter->type) {
			if(age > sortID) return INT64_MAX;
		} else if(EFSUnionFilterType == filter->type) {
			if(age < sortID) return -1;
		}
	}
	if(EFSIntersectionFilterType == filter->type) {
		if(!hit) return -1;
	} else if(EFSUnionFilterType == filter->type) {
		if(!hit) return INT64_MAX;
	}
	return sortID;
}
int64_t EFSFilterMatchAge(EFSFilterRef const filter, int64_t const sortID, int64_t const fileID) {
	assert(filter);
	switch(filter->type) {
		case EFSNoFilterType:
			return fileID;
		case EFSPermissionFilterType:
		case EFSFileTypeFilterType:
		case EFSFullTextFilterType:
		case EFSLinkedFromFilterType:
		case EFSLinksToFilterType: {
			EFSQueryFilterRef const f = (EFSQueryFilterRef)filter;
			sqlite3_bind_int64(f->age, f->arg, fileID);
			int64_t age = INT64_MAX;
			if(SQLITE_ROW == STEP(f->age) && SQLITE_NULL != sqlite3_column_type(f->age, 0)) {
				age = sqlite3_column_int64(f->age, 0);
			}
			sqlite3_reset(f->age);
			return age;
		}
		case EFSIntersectionFilterType:
		case EFSUnionFilterType: {
			EFSCollectionFilterRef const f = (EFSCollectionFilterRef)filter;
			return EFSCollectionFilterMatchAge(f, sortID, fileID);
		}
		default: {
			assertf(0, "Unknown filter type %d\n", filter->type);
			return INT64_MAX;
		}
	}
}

