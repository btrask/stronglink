/*
 function sortMerge(relation left, relation right, attribute a)
     var relation output
     var list left_sorted := sort(left, a) // Relation left sorted on attribute a
     var list right_sorted := sort(right, a)
     var attribute left_key, right_key
     var set left_subset, right_subset // These sets discarded except where join predicate is satisfied
     advance(left_subset, left_sorted, left_key, a)
     advance(right_subset, right_sorted, right_key, a)
     while not empty(left_subset) and not empty(right_subset)
         if left_key = right_key // Join predicate satisfied
             add cross product of left_subset and right_subset to output
             advance(left_subset, left_sorted, left_key, a)
             advance(right_subset, right_sorted, right_key, a)
         else if left_key < right_key
            advance(left_subset, left_sorted, left_key, a)
         else // left_key > right_key
            advance(right_subset, right_sorted, right_key, a)
     return output
*/

typedef enum {
	EFS_ASC,
	EFS_DESC,
} EFSSortDirection;

typedef struct {
	int64_t sortID;
	int64_t fileID;
} EFSMatch;

static bool_t EFSMatchInvalid(EFSMatch const *const a) {
	return a->sortID < 0 || a->fileID < 0
		|| INT64_MAX == a->sortID || INT64_MAX == b->sortID;
}
static bool_t EFSMatchEQ(EFSMatch const *const a, EFSMatch const *const b) {
	return a->sortID == b->sortID && a->fileID == b->fileID;
}
static bool_t EFSMatchLTE(EFSMatch const *const a, EFSMatch const *const b) {
	return (a->sortID == b->sortID && a->fileID <= b->fileID)
		|| a->sortID <= b->sortID;
}
static bool_t EFSMatchGTE(EFSMatch const *const a, EFSMatch const *const b) {
	return (a->sortID == b->sortID && a->fileID >= b->fileID)
		|| a->sortID >= b->sortID;
}

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

err_t EFSFilterPrepare(EFSFilterRef const filter, EFSMatch const pos, EFSSortDirection const dir) {
	assert(filter);
	filter->dir = dir;
	switch(filter->type) {
		case EFSNoFilterType: {
			EFSQueryFilterRef const f = (EFSQueryFilterRef)filter;
			f->state = pos;
			f->advance = QUERY(db, ADVANCE_DESC(ALL));
			f->age = QUERY(db, AGE(ALL));
			filter->argc = 0;
			EFSFilterAdvance(filter);
			return 0;
		}
		case EFSLinkedFromFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			f->state = pos;
			f->advance = QUERY(db, ADVANCE_DESC(LINKED_FROM));
			sqlite3_bind_text(filter->advance, 1, f->string, -1, SQLITE_STATIC);
			f->age = QUERY(db, AGE(LINKED_FROM));
			sqlite3_bind_text(filter->age, 1, f->string, -1, SQLITE_STATIC);
			filter->argc = 1;
			EFSFilterAdvance(filter);
			return 0;
		}
		case EFSLinksToFilterType: {
			EFSStringFilterRef const f = (EFSStringFilterRef)filter;
			f->state = pos;
			f->advance = QUERY(db, ADVANCE_DESC(LINKS_TO));
			sqlite3_bind_text(filter->advance, 1, f->string, -1, SQLITE_STATIC);
			f->age = QUERY(db, AGE(LINKS_TO));
			sqlite3_bind_text(filter->age, 1, f->string, -1, SQLITE_STATIC);
			filter->argc = 1;
			EFSFilterAdvance(filter);
			return 0;
		}
		case EFSIntersectionFilterType:
		case EFSUnionFilterType: {
			EFSCollectionFilterRef const f = (EFSCollectionFilterRef)filter;
			EFSFilterList const *const list = f->filters;
			for(index_t i = 0; i < list->count; ++i) {
				EFSFilterPrepare(list->items[i], pos, dir);
			}
			f->advance->count = 0;
			return 0;
		}
	}
}
static EFSMatch EFSCollectionFilterMatch(EFSCollectionFilterRef const filter);
EFSMatch EFSFilterMatch(EFSCollectionFilterRef const filter) {
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
	assert(0 == advance->count);

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
		EFSFilterAdvance(filter);
		assert(0 == advance->count);
	}
}
void EFSFilterAdvance(EFSFilterRef const filter) {

	switch(filter->type) {
		case EFSNoFilterType:
		case EFSPermissionFilterType:
		case EFSFileTypeFilterType:
		case EFSFullTextFilterType:
		case EFSLinkedFromFilterType:
		case EFSLinksToFilterType: {
			EFSQueryFilterRef const f = (EFSQueryFilterRef)filter;
			EFSMatch *const state = &f->state;
			sqlite3_bind_int64(f->advance, filter->argc+1, state->sortID);
			sqlite3_bind_int64(f->advance, filter->argc+2, state->fileID);
			sqlite3_bind_int64(f->advance, filter->argc+3, state->sortID);
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

	}
}










