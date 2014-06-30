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
		case EFSBacklinkFilesFilter:
		case EFSFileLinksFilter:
			break;
		default:
			return NULL;
	}
	EFSFilterRef const filter = calloc(1, sizeof(struct EFSFilter));
	filter->type = type;
	return filter;
}
EFSFilterRef EFSPermissionFilterCreate(int64_t const userID) {
	EFSFilterRef const filter = calloc(1, sizeof(struct EFSFilter));
	filter->type = EFSPermissionFilter;
	filter->data.userID = userID;
	return filter;
}
void EFSFilterFree(EFSFilterRef const filter) {
	if(!filter) return;
	switch(filter->type) {
		case EFSNoFilter:
		case EFSPermissionFilter:
			break;
		case EFSFileTypeFilter:
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
err_t EFSFilterAddStringArg(EFSFilterRef const filter, strarg_t const str, ssize_t const len) {
	if(!filter) return 0;
	switch(filter->type) {
		case EFSFileTypeFilter:
		case EFSFullTextFilter:
		case EFSBacklinkFilesFilter:
		case EFSFileLinksFilter:
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

void EFSFilterCreateTempTables(sqlite3 *const db) {
	EXEC(QUERY(db,
		"CREATE TEMPORARY TABLE \"results\" (\n"
		"	\"resultID\" INTEGER PRIMARY KEY NOT NULL,\n"
		"	\"fileID\" INTEGER NOT NULL,\n"
		"	\"sort\" INTEGER NOT NULL,\n"
		"	\"depth\" INTEGER NOT NULL\n"
		")"));
	EXEC(QUERY(db,
		"CREATE INDEX \"resultsDepthIndex\"\n"
		"ON \"results\" (\"depth\" ASC)"));
	EXEC(QUERY(db,
		"CREATE TEMPORARY TABLE \"depths\" (\n"
		"	\"depthID\" INTEGER PRIMARY KEY NOT NULL,\n"
		"	\"subdepth\" INTEGER NOT NULL,\n"
		"	\"depth\" INTEGER NOT NULL\n"
		")"));
	EXEC(QUERY(db,
		"CREATE INDEX \"depthDepthsIndex\"\n"
		"ON \"depths\" (\"depth\" ASC)"));
}
void EFSFilterExec(EFSFilterRef const filter, sqlite3 *const db, int64_t const depth) {
	if(!filter) return;
	switch(filter->type) {
		case EFSNoFilter: {
			sqlite3_stmt *const op = QUERY(db,
				"INSERT INTO \"results\"\n"
				"	(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT \"fileID\", \"fileID\", ?\n"
				"FROM \"files\" WHERE 1");
			sqlite3_bind_int64(op, 1, depth);
			EXEC(op);
			break;
		} case EFSFileTypeFilter: {
			sqlite3_stmt *const op = QUERY(db,
				"INSERT INTO \"results\"\n"
				"	(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT \"fileID\", \"fileID\", ?\n"
				"FROM \"files\" WHERE \"type\" = ?");
			sqlite3_bind_int64(op, 1, depth);
			sqlite3_bind_text(op, 2, filter->data.string, -1, SQLITE_STATIC);
			EXEC(op);
			break;
		} case EFSFullTextFilter: {
			sqlite3_stmt *const op = QUERY(db,
				"INSERT INTO \"results\"\n"
				"	(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT f.\"fileID\", MIN(f.\"metaFileID\"), ?\n"
				"FROM \"fileContent\" AS f\n"
				"LEFT JOIN \"fulltext\" AS t\n"
				"	ON (f.\"ftID\" = t.\"rowid\")\n"
				"WHERE t.\"text\" MATCH ?\n"
				"GROUP BY f.\"fileID\"");
			sqlite3_bind_int64(op, 1, depth);
			sqlite3_bind_text(op, 2, filter->data.string, -1, SQLITE_STATIC);
			EXEC(op);
			break;
		} case EFSBacklinkFilesFilter: {
			sqlite3_stmt *const op = QUERY(db,
				"INSERT INTO \"results\"\n"
				"	(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT f.\"fileID\", MIN(l.\"metaFileID\"), ?\n"
				"FROM \"fileURIs\" AS f\n"
				"LEFT JOIN \"links\" AS l\n"
				"	ON (f.\"URIID\" = l.\"sourceURIID\")\n"
				"LEFT JOIN \"URIs\" AS u\n"
				"	ON (l.\"targetURIID\" = u.\"URIID\")\n"
				"WHERE u.\"URI\" = ?\n"
				"GROUP BY f.\"fileID\"");
			sqlite3_bind_int64(op, 1, depth);
			sqlite3_bind_text(op, 2, filter->data.string, -1, SQLITE_STATIC);
			EXEC(op);
			break;
		} case EFSFileLinksFilter: {
			sqlite3_stmt *const op = QUERY(db,
				"INSERT INTO \"results\"\n"
				"	(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT f.\"fileID\", MIN(l.\"metaFileID\"), ?\n"
				"FROM \"fileURIs\" AS f\n"
				"LEFT JOIN \"links\" AS l\n"
				"	ON (f.\"URIID\" = l.\"targetURIID\")\n"
				"LEFT JOIN \"URIs\" AS u\n"
				"	ON (l.\"sourceURIID\" = u.\"URIID\")\n"
				"WHERE u.\"URI\" = ?\n"
				"GROUP BY f.\"fileID\"");
			sqlite3_bind_int64(op, 1, depth);
			sqlite3_bind_text(op, 2, filter->data.string, -1, SQLITE_STATIC);
			EXEC(op);
			break;
/*		} case EFSPermissionFilter: {
			QUERY(db,
				"INSERT INTO \"results\"\n"
				"	(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT \"fileID\"\n"
				"FROM \"filePermissions\"\n"
				"WHERE \"userID\" = ?");
			break;*/
		} case EFSIntersectionFilter: {
			// continue;
		} case EFSUnionFilter: {
			EFSFilterList const *const list = filter->data.filters;
			if(!list || !list->count) break;
			sqlite3_stmt *const insertDepth = QUERY(db,
				"INSERT INTO \"depths\" (\"subdepth\", \"depth\")\n"
				"VALUES (?, ?)");
			for(index_t i = 0; i < list->count; ++i) {
				EFSFilterExec(list->items[i], db, depth+i+1);
				sqlite3_bind_int64(insertDepth, 1, depth+i+1);
				sqlite3_bind_int64(insertDepth, 2, depth);
				sqlite3_step(insertDepth);
				sqlite3_reset(insertDepth);
			}
			sqlite3_finalize(insertDepth);

			sqlite3_stmt *const op = QUERY(db,
				"INSERT INTO \"results\"\n"
				"	(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT \"fileID\", MIN(\"sort\"), ?\n"
				"FROM \"results\"\n"
				"WHERE \"depth\" IN (\n"
				"	SELECT \"subdepth\" FROM \"depths\"\n"
				"	WHERE \"depth\" = ?)\n"
				"GROUP BY \"fileID\"\n"
				"HAVING COUNT(\"fileID\") >= ?");
			sqlite3_bind_int64(op, 1, depth);
			sqlite3_bind_int64(op, 2, depth);
			int64_t const threshold =
				EFSUnionFilter == filter-> type ? 1 : list->count;
			sqlite3_bind_int64(op, 3, threshold);
			EXEC(op);

			sqlite3_stmt *const clearDepths = QUERY(db,
				"DELETE FROM \"depths\" WHERE \"depth\" = ?");
			sqlite3_bind_int64(clearDepths, 1, depth);
			EXEC(clearDepths);

			sqlite3_stmt *const clearStack = QUERY(db,
				"DELETE FROM \"results\" WHERE depth > ?");
			sqlite3_bind_int64(clearStack, 1, depth);
			EXEC(clearStack);
			break;
		} default: {
			BTAssert(0, "Unrecognized filter type %d\n", (int)filter->type);
		}
	}
}

