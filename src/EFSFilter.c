#include "EarthFS.h"

typedef struct {
	count_t count;
	count_t size;
	EFSFilterRef items[0];
} EFSFilterList;

struct EFSFilter {
	EFSFilterType type;
	union {
		str_t *string;
		EFSFilterList *filters;
	} data;
};

EFSFilterRef EFSFilterCreate(EFSFilterType const type) {
	if(EFSFilterInvalid == type) return NULL;
	EFSFilterRef const filter = calloc(1, sizeof(struct EFSFilter));
	filter->type = type;
	return filter;
}
void EFSFilterFree(EFSFilterRef const filter) {
	if(!filter) return;
	switch(filter->type) {
		case EFSNoFilter:
			break;
		case EFSFullTextFilter:
		case EFSBacklinkFilesFilter:
		case EFSFileLinksFilter:
			FREE(&filter->data.string);
			break;
		case EFSIntersectionFilter:
		case EFSUnionFilter: {
			EFSFilterList *const list = filter->data.filters;
			if(list) for(index_t i = 0; i < list->count; ++i) {
				EFSFilterFree(list->items[i]); list->items[i] = NULL;
			}
			FREE(&filter->data.filters);
			break;
		} default:
			BTAssert(0, "Invalid filter type %d", (int)filter->type);
	}
	free(filter);
}
err_t EFSFilterAddStringArg(EFSFilterRef const filter, strarg_t const str, size_t const len) {
	if(!filter) return 0;
	switch(filter->type) {
		case EFSFullTextFilter:
		case EFSBacklinkFilesFilter:
		case EFSFileLinksFilter:
			break;
		default: return -1;
	}
	if(filter->data.string) return -1;
	filter->data.string = strndup(str, len);
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

// TODO: Use a real string library.
static err_t grow(str_t **const a, size_t *const alen, size_t *const asize, size_t const blen) {
	if(*alen+blen+1 > *asize) {
		*asize = MAX(128, MAX(*alen+blen+1, *asize * 2));
		*a = realloc(*a, *asize);
		if(!*a) return -1;
	}
	return 0;
}
static err_t append(str_t **const a, size_t *const alen, size_t *const asize, strarg_t const b, size_t const blen) {
	if(grow(a, alen, asize, blen)) return -1;
	memcpy(*a+*alen, b, blen);
	*alen += blen;
	(*a)[*alen] = '\0';
	return 0;
}
static err_t appendchar(str_t **const a, size_t *const alen, size_t *const asize, char const x, count_t const repeat) {
	if(grow(a, alen, asize, repeat)) return -1;
	memset(*a+*alen, x, repeat);
	*alen += repeat;
	(*a)[*alen] = '\0';
	return 0;
}
#define TAB() ({ \
	if(-1 == appendchar(sql, len, size, '\t', indent)) return -1; \
})
#define APPEND(x) ({ \
	str_t const __x[] = (x); \
	if(-1 == append(sql, len, size, __x, sizeof(__x)-1)) return -1; \
})

// TODO: This isn't actually useful, because we'll have to wrap the filter query in something else before we can use it.
sqlite3_stmt *EFSFilterCreateQuery(EFSFilterRef const filter) {
	if(!filter) return NULL;
	str_t *sql = NULL;
	size_t len = 0;
	size_t size = 0;
	if(-1 == EFSFilterAppendSQL(filter, &sql, &len, &size, 0)) return NULL;
	fprintf(stderr, "Query:\n%s\n%d, %d\n", sql, (int)len, (int)size);
	return NULL;
}
err_t EFSFilterAppendSQL(EFSFilterRef const filter, str_t **const sql, size_t *const len, size_t *const size, off_t const indent) {
	if(!filter) return 0;
	switch(filter->type) {
		case EFSNoFilter:
			TAB(); APPEND("SELECT \"fileID\" FROM\n");
			TAB(); APPEND("\"files\" WHERE TRUE");
			break;
		case EFSFullTextFilter:
			TAB(); APPEND("SELECT f.\"fileID\"\n");
			TAB(); APPEND("FROM \"fileContent\" AS f\n");
			TAB(); APPEND("LEFT JOIN \"fulltext\" AS t\n");
			TAB(); APPEND("\t" "ON (f.\"ftID\" = t.\"rowid\")\n");
			TAB(); APPEND("WHERE t.\"text\" MATCH ?");
			break;
		case EFSBacklinkFilesFilter:
			TAB(); APPEND("SELECT DISTINCT f.\"fileID\"\n");
			TAB(); APPEND("FROM \"fileURIs\" AS f");
			TAB(); APPEND("LEFT JOIN \"links\" AS l\n");
			TAB(); APPEND("\t" "ON (f.\"URIID\" = l.\"sourceURIID\")\n");
			TAB(); APPEND("LEFT JOIN \"URIs\" AS u\n");
			TAB(); APPEND("\t" "ON (l.\"targetURIID\" = u.\"URIID\")\n");
			TAB(); APPEND("WHERE u.\"URI\" = ?");
			break;
		case EFSFileLinksFilter:
			TAB(); APPEND("SELECT DISTINCT f.\"fileID\"\n");
			TAB(); APPEND("FROM \"fileURIs\" AS f");
			TAB(); APPEND("LEFT JOIN \"links\" AS l\n");
			TAB(); APPEND("\t" "ON (f.\"URIID\" = l.\"targetURIID\")\n");
			TAB(); APPEND("LEFT JOIN \"URIs\" AS u\n");
			TAB(); APPEND("\t" "ON (l.\"sourceURIID\" = u.\"URIID\")\n");
			TAB(); APPEND("WHERE u.\"URI\" = ?");
			break;
		case EFSIntersectionFilter:
		case EFSUnionFilter:
			TAB(); APPEND("SELECT f.\"fileID\"\n");
			TAB(); APPEND("FROM \"files\" AS f WHERE\n");
			EFSFilterList const *const list = filter->data.filters;
			if(list) for(index_t i = 0; i < list->count; ++i) {
				TAB(); APPEND("f.\"fileID\" IN (\n");
				if(-1 == EFSFilterAppendSQL(list->items[i], sql, len, size, indent+1)) return -1;
				APPEND("\n");
				if(i < list->count-1) {
					if(EFSIntersectionFilter == filter->type) {
						TAB(); APPEND(") AND\n");
					} else if(EFSUnionFilter == filter->type) {
						TAB(); APPEND(") OR\n");
					}
				} else {
					TAB(); APPEND(")");
				}
			}
			if(!list || !list->count) {
				TAB(); APPEND("\t" "TRUE");
			}
			break;
		default:
			return -1;
	}
	return 0;
}
err_t EFSFilterBindQueryArgs(EFSFilterRef const filter, sqlite3_stmt *const stmt, index_t *const index) {
	if(!filter) return 0;
	switch(filter->type) {
		case EFSNoFilter:
			break;
		case EFSFullTextFilter:
		case EFSBacklinkFilesFilter:
		case EFSFileLinksFilter:
			if(BTSQLiteErr(sqlite3_bind_text(stmt, (*index)++, filter->data.string, -1, SQLITE_TRANSIENT))) return -1;
			break;
		case EFSIntersectionFilter:
		case EFSUnionFilter: {
			EFSFilterList const *const list = filter->data.filters;
			for(index_t i = 0; i < list->count; ++i) {
				if(-1 == EFSFilterBindQueryArgs(filter, stmt, index)) return -1;
			}
			break;
		} default:
			return -1;
	}
	return 0;
}

