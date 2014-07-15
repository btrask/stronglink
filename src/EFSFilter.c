#include "EarthFS.h"

typedef struct {
	count_t count;
	count_t size;
	EFSFilterRef items[0];
} EFSFilterList;

struct EFSFilter {
	EFSFilterType type;
	sqlite3_stmt *matchFile;
	sqlite3_stmt *matchAge;
	int argc;
	union {
		str_t *string;
		EFSFilterList *filters;
		int64_t userID;
	} data;
};

EFSFilterRef EFSFilterCreate(EFSFilterType const type) {
	switch(type) {
		case EFSNoFilter:
		case EFSFileTypeFilter:
		case EFSIntersectionFilter:
		case EFSUnionFilter:
		case EFSFullTextFilter:
		case EFSLinkedFromFilter:
		case EFSLinksToFilter:
			break;
		default:
			return NULL;
	}
	EFSFilterRef filter = calloc(1, sizeof(struct EFSFilter));
	if(!filter) return NULL;
	filter->type = type;
	return filter;
}
EFSFilterRef EFSPermissionFilterCreate(int64_t const userID) {
	EFSFilterRef filter = calloc(1, sizeof(struct EFSFilter));
	if(!filter) return NULL;
	filter->type = EFSPermissionFilter;
	filter->data.userID = userID;
	return filter;
}
void EFSFilterFree(EFSFilterRef *const filterptr) {
	EFSFilterRef filter = *filterptr;
	if(!filter) return;
	switch(filter->type) {
		case EFSNoFilter:
		case EFSPermissionFilter: {
			break;
		}
		case EFSFileTypeFilter:
		case EFSFullTextFilter:
		case EFSLinkedFromFilter:
		case EFSLinksToFilter: {
			sqlite3_finalize(filter->matchFile); filter->matchFile = NULL;
			sqlite3_finalize(filter->matchAge); filter->matchAge = NULL;
			FREE(&filter->data.string);
			break;
		}
		case EFSIntersectionFilter:
		case EFSUnionFilter: {
			EFSFilterList *const list = filter->data.filters;
			if(list) for(index_t i = 0; i < list->count; ++i) {
				EFSFilterFree(&list->items[i]);
			}
			FREE(&filter->data.filters);
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
		case EFSFileTypeFilter:
		case EFSFullTextFilter:
		case EFSLinkedFromFilter:
		case EFSLinksToFilter:
			break;
		default: return -1;
	}
	if(filter->data.string) return -1;
	filter->data.string = strndup(str, len < 0 ? SIZE_MAX : len);
	return 0;
}
err_t EFSFilterAddFilterArg(EFSFilterRef const filter, EFSFilterRef const subfilter) {
	if(!filter) return 0;
	switch(filter->type) {
		case EFSIntersectionFilter:
		case EFSUnionFilter:
			break;
		default: return -1;
	}
	EFSFilterList *filters = filter->data.filters;
	count_t size = filters ? filters->size : 0;
	count_t count = filters ? filters->count : 0;
	if(++count > size) {
		size = MAX(10, size * 2);
		filters = realloc(filters,  sizeof(EFSFilterList) + (sizeof(EFSFilterRef) * size));
		filter->data.filters = filters;
		if(!filters) return -1;
		filters->size = size;
	}
	filters->count = count;
	filters->items[count-1] = subfilter;
	return 0;
}


#define MATCH_FILE(str) \
	"SELECT MIN(f.file_id)\n" \
	str "\n" \
	"AND (md.meta_file_id = ? AND f.file_id > ?)"

#define MATCH_AGE(str) \
	"SELECT MIN(md.meta_file_id)\n" \
	str "\n" \
	"AND (f.file_id = ?)"


#define LINKED_FROM \
	"FROM file_uris AS f\n" \
	"INNER JOIN meta_data AS md\n" \
	"	ON (f.uri = md.value)\n" \
	"WHERE (\n" \
	"	md.field = 'link'\n" \
	"	AND md.value = ?\n" \
	")"

#define LINKS_TO \
	"FROM file_uris AS f\n" \
	"INNER JOIN meta_data AS md\n" \
	"	ON (f.uri = md.uri)\n" \
	"WHERE (\n" \
	"	md.field = 'link'\n" \
	"	AND md.value = ?\n" \
	")"

err_t EFSFilterPrepare(EFSFilterRef const filter, sqlite3 *const db) {
	assertf(!filter->matchFile, "Filter already prepared");
	assertf(!filter->matchAge, "Filter already prepared");
	assertf(0 == filter->argc, "Filter already prepared");
	switch(filter->type) {
		case EFSLinkedFromFilter: {
			filter->matchFile = QUERY(db, MATCH_FILE(LINKED_FROM));
			sqlite3_bind_text(filter->matchFile, 1, filter->data.string, -1, SQLITE_STATIC);
			filter->matchAge = QUERY(db, MATCH_AGE(LINKED_FROM));
			sqlite3_bind_text(filter->matchAge, 1, filter->data.string, -1, SQLITE_STATIC);
			filter->argc = 1;
			return 0;
		}
		case EFSLinksToFilter: {
			filter->matchFile = QUERY(db, MATCH_FILE(LINKS_TO));
			sqlite3_bind_text(filter->matchFile, 1, filter->data.string, -1, SQLITE_STATIC);
			filter->matchAge = QUERY(db, MATCH_AGE(LINKS_TO));
			sqlite3_bind_text(filter->matchAge, 1, filter->data.string, -1, SQLITE_STATIC);
			filter->argc = 1;
			return 0;
		}
		case EFSIntersectionFilter:
		case EFSUnionFilter: {
			EFSFilterList const *const list = filter->data.filters;
			for(index_t i = 0; i < list->count; ++i) {
				if(EFSFilterPrepare(list->items[i], db) < 0) return -1;
			}
			return 0;
		}
		default: {
			assertf(0, "Unknown filter type %d\n", filter->type);
			return -1;
		}
	}
}
int64_t EFSFilterMatchFile(EFSFilterRef const filter, int64_t const sortID, int64_t const lastFileID) {
	switch(filter->type) {
		case EFSNoFilter:
		case EFSPermissionFilter: {
			if(lastFileID < sortID) return sortID;
			return -1;
		}
		case EFSFileTypeFilter:
		case EFSFullTextFilter:
		case EFSLinkedFromFilter:
		case EFSLinksToFilter: {
			sqlite3_bind_int64(filter->matchFile, filter->argc + 1, sortID);
			sqlite3_bind_int64(filter->matchFile, filter->argc + 2, lastFileID);
			int64_t fileID = -1;
			if(
				SQLITE_ROW == sqlite3_step(filter->matchFile) &&
				SQLITE_NULL != sqlite3_column_type(filter->matchFile, 0)
			) {
				fileID = sqlite3_column_int64(filter->matchFile, 0);
			}
			sqlite3_reset(filter->matchFile);
			if(fileID < 0) return -1;
			if(EFSFilterMatchAge(filter, fileID) < sortID) return -1;
			return fileID;
		}
		case EFSIntersectionFilter:
		case EFSUnionFilter: {
			EFSFilterList const *const list = filter->data.filters;
			int64_t firstFileID = INT64_MAX;
			for(index_t i = 0; i < list->count; ++i) {
				int64_t const fileID = EFSFilterMatchFile(list->items[i], sortID, lastFileID);
				if(fileID < 0) continue;
				if(fileID < firstFileID) firstFileID = fileID;
			}
			if(INT64_MAX == firstFileID) return -1;

			for(index_t i = 0; i < list->count; ++i) {
				int64_t const age = EFSFilterMatchAge(list->items[i], firstFileID);
				if(EFSIntersectionFilter == filter->type) {
					if(age > sortID) return -1;
				} else {
					if(age < sortID) return -1;
				}
			}
			return firstFileID;
		}
		default: {
			assertf(0, "Unknown filter type %d\n", filter->type);
			return -1;
		}
	}
}
int64_t EFSFilterMatchAge(EFSFilterRef const filter, int64_t const fileID) {
	switch(filter->type) {
		case EFSNoFilter:
		case EFSPermissionFilter:
			return fileID;
		case EFSFileTypeFilter:
		case EFSFullTextFilter:
		case EFSLinkedFromFilter:
		case EFSLinksToFilter: {
			sqlite3_bind_int64(filter->matchAge, filter->argc + 1, fileID);
			int64_t age = INT64_MAX;
			if(
				SQLITE_ROW == sqlite3_step(filter->matchAge) &&
				SQLITE_NULL != sqlite3_column_type(filter->matchAge, 0)
			) {
				age = sqlite3_column_int64(filter->matchAge, 0);
			}
			sqlite3_reset(filter->matchAge);
			return age;
		}
		case EFSIntersectionFilter:
		case EFSUnionFilter: {
			EFSFilterList const *const list = filter->data.filters;
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

