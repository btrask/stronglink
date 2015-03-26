




int64_t EFSFilterMatchingFileID(EFSFilterRef const filter, int64_t const sort_id) {
	if(!filter) return -1;
	switch(filter->type) {

		case EFSFileLinksFilter:

SELECT f.file_id
FROM file_uris AS f
INNER JOIN meta_data AS md
	ON (f.uri_sid = md.uri_sid)
INNER JOIN strings AS field
	ON (md.field_sid = field.sid)
INNER JOIN strings AS value
	ON (md.value_sid = value.sid)
WHERE field.string = 'link'
AND value.string = ?
AND md.meta_file_id = ?


	}
}


(intersection
	(type markdown)
	(links-to "efs://user"))

sort1: file1, markdown
sort2: file1, "efs://user"

/*

1. for a given sort-id, check filters until a filter matches
	each sort-id corrosponds to a single file-id, so this can be returned by the filters or checked in advance
	meta-files should not declare their own meta-data (links mainly)
2. check to make sure that the filter didn't match on any previous sort-id
3. check to make sure all of the other filters matched on this sort-id or earlier

*/

typedef enum {
	EFSMatchError = -1,
	EFSNoMatch = 0,
	EFSOldMatch = 1,
	EFSNewMatch = 2,
} EFSMatchStatus;



	switch(filter->type) {



SELECT md.meta_file_id
FROM meta_data AS md
INNER JOIN strings AS field
	ON (md.field_sid = field.sid)
INNER JOIN strings AS value
	ON (md.value_sid = value.sid)
WHERE field.string = 'link'
AND value.string = ?
ORDER BY md.meta_file_id ASC LIMIT 1


		case EFSIntersectionFilter:
		case EFSUnionFilter:
			return EFSCollectionFilterMatch(filter, sortID);
	}
}

EFSMatchStatus EFSCollectionFilterMatch(EFSFilterRef const filter, int64_t const sortID) {
	bool_t match = false, new = false;
	EFSFilterList const *const filters = filter->data.filters;
	if(!filters) return EFSMatchError;
	for(index_t i = 0; i < filters->count; ++i) {
		switch(EFSFilterMatch(filters->items[i], sortID, fileID)) {
			case EFSMatchError: return EFSMatchError;
			case EFSNewMatch: new = true; /* fallthrough */
			case EFSOldMatch: match = true; break;
			case EFSNoMatch:
				switch(filter->type) {
					case EFSIntersectionFilter: return EFSNoMatch;
					case EFSUnionFilter: break;
					default: assertf(0, "Invalid filter type %d", filter->type);
				}
				break;
			default: assertf(0, "Invalid match status");
		}
	}
	if(!match) return EFSNoMatch;
	if(!new) return EFSOldMatch;
	return EFSNewMatch;
}



SELECT meta_file_id, file_id
FROM file_uris AS f
INNER JOIN meta_data AS md
	ON (f.uri_sid = md.uri_sid)
WHERE sort_id > ?1
OR (sort_id = ?1 AND file_id > ?2)
GROUP BY sort_id, file_id
ORDER BY sort_id ASC, file_id ASC


int64_t EFSFilterGetFileSortID(EFSFilterRef const filter, int64_t const fileID) {

}
int64_t *EFSFilterGetTouchedFiles(EFSFilterRef const filter, int64_t const sortID) {

}


one meta-file can simultaneously satisfy two different filters for two different files
thats even if we limit each meta-file to applying to a single file (not even itself)
and yes, those two filters can be relevant at the same time with a union

so at present, our order isnt even strictly defined

the reasonable thing to do is sort based on meta_data_id

but even a single field can produce arbitrary many results

it turns out that our current/old filter system is remarkably accurate
we just need to make sure to sort by `sort, file_id`

stepping through one row at a time is completely necessary
batching rows is just an optimization if anything



typedef struct {
	int64_t sortID;
	int64_t fileID;
} EFSMatch;

bool_t EFSFilterGetNextMatch(EFSFilterRef const filter, EFSMatch *const match) {


SELECT meta_data_id AS sort_id, file_id
FROM meta_data AS md
INNER JOIN file_uris AS f
	ON (md.uri_sid = f.uri_sid)
INNER JOIN strings AS field
	ON (md.field_sid = field.sid)
INNER JOIN strings AS value
	ON (md.value_sid = value.sid)
WHERE (
	sort_id > ?1
	OR (sort_id = ?1 AND file_id > ?2)
) AND value.string = ?
ORDER BY sort_id ASC LIMIT 1



}
bool_t EFSFilterHasMatch(EFSFilterRef const filter, EFSMatch const *const match) {

}






asprintf(sql, "")



SELECT x1.file_id, MAX(x1.sort_id, x2.sort_id)
FROM (subquery) AS x1
INNER JOIN (subquery) as x2 ON (x1.file_id = x2.file_id)








int64_t EFSFilterMatch(EFSFilterRef const filter, int64_t const sortID, int64_t const lastFileID) {

SELECT f.file_id
FROM file_uris AS f
INNER JOIN meta_data AS md
	ON (f.uri_sid = md.uri_sid)
INNER JOIN strings AS field
	ON (md.field_sid = field.sid)
INNER JOIN strings AS value
	ON (md.value_sid = value.sid)
WHERE (
	field.string = 'link'
	AND value.string = ?
)
AND (md.meta_file_id = ? AND f.file_id > ?)
ORDER BY f.file_id ASC LIMIT 1

}
int64_t EFSFilterMatchAge(EFSFilterRef const filter, int64_t const fileID) {


	

SELECT md.meta_file_id
FROM file_uris AS f
INNER JOIN meta_data AS md
	ON (f.uri_sid = md.uri_sid)
INNER JOIN strings AS field
	ON (md.field_sid = field.sid)
INNER JOIN strings AS value
	ON (md.value_sid = value.sid)
WHERE (
	field.string = 'link'
	AND value.string = ?
)
AND (f.file_id = ?)
ORDER BY md.meta_file_id ASC LIMIT 1


}

EFSLinkingToFilter
EFSLinkedFromFilter

#define MATCH(str) \
	str "\n" \
	"AND (sort_id = ? AND file_id > ?)\n" \
	"ORDER BY file_id ASC LIMIT 1"

#define MATCH_AGE(str) \
	str "\n" \
	"AND (file_id = ?)\n" \
	"ORDER BY sort_id ASC LIMIT 1"


#define LINKS_TO \
	"SELECT f.file_id AS file_id,\n" \
	"	md.meta_file_id AS sort_id\n" \
	"FROM file_uris AS f\n" \
	"INNER JOIN meta_data AS md\n" \
	"	ON (f.uri_sid = md.uri_sid)\n" \
	"INNER JOIN strings AS field\n" \
	"	ON (md.field_sid = field.sid)\n" \
	"INNER JOIN strings AS value\n" \
	"	ON (md.value_sid = value.sid)\n" \
	"WHERE (\n" \
	"	field.string = 'link'\n" \
	"	AND value.string = ?\n" \
	")"

#define LINKED_FROM \
	"SELECT f.file_id AS file_id,\n" \
	"	md.meta_file_id AS sort_id\n" \
	"FROM file_uris AS f\n" \
	"INNER JOIN meta_data AS md\n" \
	"	ON (f.uri_sid = md.value_sid)\n" \
	"INNER JOIN strings AS field\n" \
	"	ON (md.field_sid = field.sid)\n" \
	"INNER JOIN strings AS value\n" \
	"	ON (md.uri_sid = value.sid)\n" \
	"WHERE (\n" \
	"	field.string = 'link'\n" \
	"	AND value.string = ?\n" \
	")"

err_t EFSFilterPrepare(EFSFilterRef const filter, sqlite3 *const db) {
	assertf(!filter->match, "Filter already prepared");
	assertf(!filter->matchAge, "Filter already prepared");
	assertf(0 == filter->argc, "Filter already prepared");
	switch(filter->type) {
		case EFSLinkedFromFilter: {
			filter->match = QUERY(db, MATCH(LINKED_FROM));
			sqlite3_bind_text(filter->match, 1, filter->data.string, -1, SQLITE_STATIC);
			filter->matchAge = QUERY(db, MATCH_AGE(LINKED_FROM));
			sqlite3_bind_text(filter->matchAge, 1, filter->data.string, -1, SQLITE_STATIC);
			filter->argc = 1;
			return 0;
		}
		case EFSLinksToFilter: {
			filter->match = QUERY(db, MATCH(LINKS_TO));
			sqlite3_bind_text(filter->match, 1, filter->data.string, -1, SQLITE_STATIC);
			filter->matchAge = QUERY(db, MATCH_AGE(LINKS_TO));
			sqlite3_bind_text(filter->matchAge, 1, filter->data.string, -1, SQLITE_STATIC);
			filter->argc = 1;
			return 0;
		}
		case EFSIntersectionFilter:
		case EFSUnionFilter: {
			EFSFilterList const *const list = filter->data.filters;
			for(index_t i = 0; i < list->count; ++i) {
				if(EFSFilterPrepare(list->items[i]) < 0) return -1;
			}
			return 0;
		}
		default: {
			assertf(0, "Unknown filter type %d\n", filter->type);
			return -1;
		]
	}
}
int64_t EFSFilterMatch(EFSFilterRef const filter, int64_t const sortID, int64_t const lastFileID) {
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
			sqlite3_bind_int64(filter->match, filter->argc + 1, sortID);
			sqlite3_bind_int64(filter->match, filter->argc + 2, lastFileID);
			int64_t fileID = -1;
			if(SQLITE_ROW == sqlite3_step(filter->match)) {
				fileID = sqlite3_column_int64(filter->match, 0);
			}
			sqlite3_reset(filter->match);
			return fileID;
		}
		case EFSIntersectionFilter:
		case EFSUnionFilter: {
			EFSFilterList const *const list = filter->data.filters;
			int64_t firstFileID = INT64_MAX;
			for(index_t i = 0; i < list->count; ++i) {
				int64_t const fileID = EFSFilterMatch(list->items[i], sortID, lastFileID);
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
		case EFSBacklinkFilesFilter:
		case EFSFileLinksFilter: {
			sqlite3_bind_int64(filter->matchAge, filter->argc + 1, fileID);
			int64_t age = INT64_MAX;
			if(SQLITE_ROW == sqlite3_step(filter->matchAge)) {
				age = sqlite3_column_int64(filter->matchAge, 1);
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





