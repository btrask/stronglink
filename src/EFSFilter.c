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
	EFSFilterType type; \
	EFSSortDirection dir;
#define EFS_FILTER_QUERY \
	EFSMatch state; \
	sqlite3_stmt *advance; \
	sqlite3_stmt *age; \
	int argc;

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
	EFSFilterList *advance;
};

EFSFilterRef EFSFilterCreate(EFSFilterType const type) {
	EFSFilterRef filter = NULL;
	switch(type) {
		case EFSNoFilterType:
			filter = calloc(1, sizeof(struct EFSQueryFilter));
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
		case EFSNoFilterType:
		case EFSPermissionFilterType: {
			EFSQueryFilterRef const f = (EFSQueryFilterRef)filter;
			sqlite3f_finalize(f->advance); f->advance = NULL;
			sqlite3f_finalize(f->age); f->age = NULL;
			break;
		}
		case EFSFileTypeFilterType:
		case EFSFullTextFilterType:
		case EFSLinkedFromFilterType:
		case EFSLinksToFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			sqlite3f_finalize(f->advance); f->advance = NULL;
			sqlite3f_finalize(f->age); f->age = NULL;
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
			FREE(&f->advance);
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
		f->advance = realloc(f->advance, bytes);
		if(!f->filters || !f->advance) {
			FREE(&f->filters);
			FREE(&f->advance);
			return -1;
		}
		f->filters->size = size;
		f->advance->size = size;
		if(!list) {
			list = f->filters;
			list->count = 0;
		}
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


static bool_t EFSMatchInvalid(EFSMatch const *const a) {
	return a->sortID < 0 || a->fileID < 0
		|| INT64_MAX == a->sortID || INT64_MAX == a->fileID;
}
static bool_t EFSMatchEQ(EFSMatch const *const a, EFSMatch const *const b) {
	return a->sortID == b->sortID && a->fileID == b->fileID;
}
static bool_t EFSMatchLT(EFSMatch const *const a, EFSMatch const *const b) {
	return (a->sortID == b->sortID && a->fileID < b->fileID)
		|| a->sortID < b->sortID;
}
static bool_t EFSMatchGT(EFSMatch const *const a, EFSMatch const *const b) {
	return (a->sortID == b->sortID && a->fileID > b->fileID)
		|| a->sortID > b->sortID;
}
static bool_t EFSMatchLTE(EFSMatch const *const a, EFSMatch const *const b) {
	return (a->sortID == b->sortID && a->fileID <= b->fileID)
		|| a->sortID <= b->sortID;
}
static bool_t EFSMatchGTE(EFSMatch const *const a, EFSMatch const *const b) {
	return (a->sortID == b->sortID && a->fileID >= b->fileID)
		|| a->sortID >= b->sortID;
}

#define ADVANCE_ASC(str) \
	str "\n" \
	"AND ((sort_id = ? AND file_id > ?)\n" \
	"	OR sort_id > ?)\n" \
	"ORDER BY file_id ASC LIMIT 1"
#define ADVANCE_DESC(str) \
	str "\n" \
	"AND ((sort_id = ? AND file_id < ?)\n" \
	"	OR sort_id < ?)\n" \
	"ORDER BY file_id DESC LIMIT 1"
#define AGE(str) \
	str "\n" \
	"AND (file_id = ?)\n" \
	"ORDER BY sort_id ASC LIMIT 1"

#define ALL \
	"SELECT file_id AS sort_id,\n" \
	"	file_id AS file_id\n" \
	"FROM files WHERE 1"
#define FILE_TYPE \
	"SELECT file_id AS sort_id,\n" \
	"	file_id AS file_id\n" \
	"FROM files WHERE file_type = ?"
#define LINKED_FROM \
	"SELECT md.meta_file_id AS sort_id,\n" \
	"	f.file_id AS file_id\n" \
	"FROM file_uris AS f\n" \
	"INNER JOIN meta_data AS md\n" \
	"	ON (f.uri = md.value)\n" \
	"WHERE (\n" \
	"	md.field = 'link'\n" \
	"	AND md.value = ?\n" \
	")"
#define LINKS_TO \
	"SELECT md.meta_file_id AS sort_id,\n" \
	"	f.file_id AS file_id\n" \
	"FROM file_uris AS f\n" \
	"INNER JOIN meta_data AS md\n" \
	"	ON (f.uri = md.uri)\n" \
	"WHERE (\n" \
	"	md.field = 'link'\n" \
	"	AND md.value = ?\n" \
	")"

err_t EFSFilterPrepare(EFSFilterRef const filter, EFSMatch const pos, EFSSortDirection const dir, sqlite3f *const db) {
	assert(filter);
	filter->dir = dir;
	switch(filter->type) {
		case EFSNoFilterType: {
			EFSQueryFilterRef const f = (EFSQueryFilterRef)filter;
			f->advance = QUERY(db, ADVANCE_DESC(ALL));
			f->age = QUERY(db, AGE(ALL));
			f->state = pos;
			f->argc = 0;
			EFSFilterAdvance(filter);
			return 0;
		}
		case EFSFileTypeFilterType:
		case EFSLinkedFromFilterType:
		case EFSLinksToFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			switch(filter->type) {
				case EFSFileTypeFilterType:
					f->advance = QUERY(db, ADVANCE_DESC(FILE_TYPE));
					f->age = QUERY(db, AGE(FILE_TYPE));
					break;
				case EFSLinkedFromFilterType:
					f->advance = QUERY(db, ADVANCE_DESC(LINKED_FROM));
					f->age = QUERY(db, AGE(LINKED_FROM));
					break;
				case EFSLinksToFilterType:
					f->advance = QUERY(db, ADVANCE_DESC(LINKS_TO));
					f->age = QUERY(db, AGE(LINKS_TO));
					break;
				default: assert(0);
			}
			f->state = pos;
			sqlite3_bind_text(f->advance, 1, f->string, -1, SQLITE_STATIC);
			sqlite3_bind_text(f->age, 1, f->string, -1, SQLITE_STATIC);
			f->argc = 1;
			EFSFilterAdvance(filter);
			return 0;
		}
		case EFSIntersectionFilterType:
		case EFSUnionFilterType: {
			EFSCollectionFilterRef const f = (EFSCollectionFilterRef)filter;
			EFSFilterList const *const list = f->filters;
			for(index_t i = 0; i < list->count; ++i) {
				EFSFilterPrepare(list->items[i], pos, dir, db);
			}
			f->advance->count = 0;
			return 0;
		}
		default: {
			assertf(0, "Unknown filter type %d\n", filter->type);
			return -1;
		}
	}
}
static EFSMatch EFSCollectionFilterMatch(EFSCollectionFilterRef const filter);
EFSMatch EFSFilterMatch(EFSFilterRef const filter) {
	assert(filter);
	switch(filter->type) {
		case EFSNoFilterType:
		case EFSPermissionFilterType:
		case EFSFileTypeFilterType:
		case EFSFullTextFilterType:
		case EFSLinkedFromFilterType:
		case EFSLinksToFilterType: {
			EFSQueryFilterRef const f = (EFSQueryFilterRef)filter;
			return f->state;
		}
		case EFSIntersectionFilterType:
		case EFSUnionFilterType: {
			return EFSCollectionFilterMatch((EFSCollectionFilterRef)filter);
		}
		default: {
			assertf(0, "Unknown filter type %d\n", filter->type);
			return (EFSMatch){-1, -1};
		}
	}
}
static EFSMatch EFSCollectionFilterMatch(EFSCollectionFilterRef const filter) {
	assert(filter);
	EFSFilterList const *const list = filter->filters;
	EFSFilterList *const advance = filter->advance;
	advance->count = 0;

	for(;;) {
		EFSMatch next = {-1, -1};

		for(index_t i = 0; i < list->count; ++i) {
			EFSMatch const match = EFSFilterMatch(list->items[i]);
			if(EFSMatchInvalid(&match)) continue;
			if(EFSMatchGT(&match, &next)) {
				next = match;
				advance->count = 0;
			}
			if(EFSMatchGTE(&match, &next)) {
				assert(!EFSMatchInvalid(&next));
				advance->items[advance->count++] = list->items[i];
			}
		}
		if(EFSMatchInvalid(&next)) return (EFSMatch){-1, -1};

		bool_t old = false;
		for(index_t i = 0; i < list->count; ++i) {
			int64_t const age = EFSFilterMatchAge(list->items[i], next.fileID);
			if(EFSIntersectionFilterType == filter->type) {
				if(age <= next.sortID) continue;
			} else if(EFSUnionFilterType == filter->type) {
				if(age >= next.sortID) continue;
			}
			old = true;
			break;
		}
		if(!old) return next;
		EFSFilterAdvance((EFSFilterRef)filter);
		assert(0 == advance->count);
	}
}
void EFSFilterAdvance(EFSFilterRef const filter) {
	assert(filter);
	switch(filter->type) {
		case EFSNoFilterType:
		case EFSPermissionFilterType:
		case EFSFileTypeFilterType:
		case EFSFullTextFilterType:
		case EFSLinkedFromFilterType:
		case EFSLinksToFilterType: {
			EFSQueryFilterRef const f = (EFSQueryFilterRef)filter;
			EFSMatch *const state = &f->state;
			sqlite3_bind_int64(f->advance, f->argc+1, state->sortID);
			sqlite3_bind_int64(f->advance, f->argc+2, state->fileID);
			sqlite3_bind_int64(f->advance, f->argc+3, state->sortID);
			if(SQLITE_ROW == STEP(f->advance)) {
				state->sortID = sqlite3_column_int64(f->advance, 0);
				state->fileID = sqlite3_column_int64(f->advance, 1);
			} else {
				state->sortID = -1;
				state->fileID = -1;
			}
			sqlite3_reset(f->advance);
			return;
		}
		case EFSIntersectionFilterType:
		case EFSUnionFilterType: {
			EFSCollectionFilterRef const f = (EFSCollectionFilterRef)filter;
			EFSFilterList *const advance = f->advance;
			for(index_t i = 0; i < advance->count; ++i) {
				EFSFilterAdvance(advance->items[i]);
			}
			advance->count = 0;
			break;
		}
		default: {
			assertf(0, "Unknown filter type %d\n", filter->type);
			break;
		}
	}
}
int64_t EFSFilterMatchAge(EFSFilterRef const filter, int64_t const fileID) {
	switch(filter->type) {
		case EFSNoFilterType:
		case EFSPermissionFilterType:
			return fileID;
		case EFSFileTypeFilterType:
		case EFSFullTextFilterType:
		case EFSLinkedFromFilterType:
		case EFSLinksToFilterType: {
			EFSQueryFilterRef const f = (EFSQueryFilterRef)filter;
			sqlite3_bind_int64(f->age, f->argc+1, fileID);
			int64_t age = INT64_MAX;
			if(SQLITE_ROW == STEP(f->age)) {
				age = sqlite3_column_int64(f->age, 0);
			}
			sqlite3_reset(f->age);
			return age;
		}
		case EFSIntersectionFilterType:
		case EFSUnionFilterType: {
			EFSCollectionFilterRef const f = (EFSCollectionFilterRef)filter;
			EFSFilterList const *const list = f->filters;
			int64_t minAge = INT64_MAX;
			for(index_t i = 0; i < list->count; ++i) {
				int64_t age = EFSFilterMatchAge(list->items[i], fileID);
				if(age < minAge) minAge = age;
			}
			return minAge;
		}
		default: {
			assertf(0, "Unknown filter type %d\n", filter->type);
			return INT64_MAX;
		}
	}
}

