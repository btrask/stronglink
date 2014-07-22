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
void EFSFilterPrint(EFSFilterRef const filter, count_t const indent) {
	if(!filter) {
		fprintf(stderr, "(null-filter)\n");
		return;
	}
	// TODO: Copy and paste is bad.
	for(index_t i = 0; i < indent; ++i) fprintf(stderr, "\t");
	switch(filter->type) {
		case EFSNoFilter:
			fprintf(stderr, "(all)\n");
			break;
		case EFSPermissionFilter:
			fprintf(stderr, "(permission %lld)\n", filter->data.userID);
			break;
		case EFSFileTypeFilter:
			fprintf(stderr, "(file-type %s)\n", filter->data.string);
			break;
		case EFSFullTextFilter:
			fprintf(stderr, "(full-text %s)\n", filter->data.string);
			break;
		case EFSLinkedFromFilter:
			fprintf(stderr, "(linked-from %s)\n", filter->data.string);
			break;
		case EFSLinksToFilter:
			fprintf(stderr, "(links-to %s)\n", filter->data.string);
			break;
		case EFSIntersectionFilter: {
			fprintf(stderr, "(intersection\n");
			EFSFilterList const *const list = filter->data.filters;
			for(index_t i = 0; i < list->count; ++i) {
				EFSFilterPrint(list->items[i], indent+1);
			}
			for(index_t i = 0; i < indent; ++i) fprintf(stderr, "\t");
			fprintf(stderr, ")\n");
			break;
		}
		case EFSUnionFilter: {
			fprintf(stderr, "(union\n");
			EFSFilterList const *const list = filter->data.filters;
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

// It's fine if COUNT() overcounts (e.g duplicates) because it's just an optimization. Not sure whether using DISTINCT makes any difference.
#define MATCH_FILE(str) \
	str "\n" \
	"AND (sort_id = ? AND file_id > ?)\n" \
	"ORDER BY sort_id ASC, file_id ASC\n" \
	"LIMIT 1"

#define MATCH_AGE(str) \
	str "\n" \
	"AND (file_id = ?)\n" \
	"ORDER BY sort_id ASC LIMIT 1"


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
EFSMatch EFSFilterMatchFile(EFSFilterRef const filter, int64_t const sortID, int64_t const lastFileID) {
	switch(filter->type) {
		case EFSNoFilter:
		case EFSPermissionFilter: {
			if(lastFileID < sortID) return (EFSMatch){sortID, false};
			return (EFSMatch){-1, false};
		}
		case EFSFileTypeFilter:
		case EFSFullTextFilter:
		case EFSLinkedFromFilter:
		case EFSLinksToFilter: {
			sqlite3_bind_int64(filter->matchFile, filter->argc + 1, sortID);
			sqlite3_bind_int64(filter->matchFile, filter->argc + 2, lastFileID);
			int64_t fileID = -1;
			bool_t more = false;
			if(SQLITE_ROW == STEP(filter->matchFile)) {
				fileID = sqlite3_column_int64(filter->matchFile, 1);
				more = true;
			}
			sqlite3_reset(filter->matchFile);
			if(fileID < 0) return (EFSMatch){-1, false};
			return (EFSMatch){fileID, more};
		}
		case EFSIntersectionFilter:
		case EFSUnionFilter: {
			EFSFilterList const *const list = filter->data.filters;
			int64_t firstFileID = INT64_MAX;
			bool_t more = false;
			for(index_t i = 0; i < list->count; ++i) {
				EFSMatch const match = EFSFilterMatchFile(list->items[i], sortID, lastFileID);
				if(match.fileID < 0) continue;
				if(INT64_MAX != firstFileID || match.more) more = true;
				if(match.fileID < firstFileID) firstFileID = match.fileID;
			}
			if(INT64_MAX == firstFileID) return (EFSMatch){-1, false};

			for(index_t i = 0; i < list->count; ++i) {
				int64_t const age = EFSFilterMatchAge(list->items[i], firstFileID);
				if(EFSIntersectionFilter == filter->type) {
					if(age <= sortID) continue;
				} else {
					if(age >= sortID) continue;
				}
				if(more) return EFSFilterMatchFile(filter, sortID, firstFileID);
				return (EFSMatch){-1, false};
			}
			return (EFSMatch){firstFileID, more};
		}
		default: {
			assertf(0, "Unknown filter type %d\n", filter->type);
			return (EFSMatch){-1, false};
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
			if(SQLITE_ROW == STEP(filter->matchAge)) {
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

