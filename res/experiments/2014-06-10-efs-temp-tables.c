



"CREATE TEMPORARY TABLE \"results\" (\n"
"\t" "\"resultID\" INTEGER PRIMARY KEY NOT NULL\n"
"\t" "\"fileID\" INTEGER NOT NULL\n"
"\t" "\"sort\" INTEGER NOT NULL\n"
"\t" "\"depth\" INTEGER NOT NULL\n"
")"
"CREATE INDEX \"resultsDepthIndex\" ON \"results\" (\"depth\" ASC)"

"CREATE TEMPORARY TABLE \"depths\" (\n"
"\t" "\"depthID\" INTEGER PRIMARY KEY NOT NULL\n"
"\t" "\"subdepth\" INTEGER NOT NULL\n"
"\t" "\"depth\" INTEGER NOT NULL\n"
")"
"CREATE INDEX \"depthDepthsIndex\" ON \"depths\" (\"depth\" ASC)"



"INSERT INTO \"results\"\n"
"\t" "(\"fileID\", \"sort\", \"depth\")\n"
"SELECT f.\"fileID\", f.\"fileID\", ?\n"
"FROM \"fileContent\" AS f\n"
"LEFT JOIN \"fulltext\" AS t\n"
"\t" "ON (f.\"ftID\" = t.\"rowid\")\n"
"WHERE t.\"text\" MATCH ?"
depth, text

// union
"INSERT INTO \"results\"\n"
"\t" "(\"fileID\", \"sort\", \"depth\")\n"
"SELECT \"fileID\", MIN(\"sort\"), ?\n"
"FROM \"results\"\n"
"WHERE ? = \"depth\" OR ? = \"depth\"\n"
"GROUP BY \"fileID\""
depth, depth1, depth2


"DELETE FROM \"results\" WHERE \"depth\" > ?"
depth


void EFSFilterExec(EFSFilterRef const filter, sqlite3 *const db, int64_t const depth) {
	if(!filter) return;
	switch(filter->type) {
		case EFSNoFilter:
			(void)0;
			sqlite3_stmt *const op = QUERY(db,
				"INSERT INTO \"results\"\n"
				"\t" "(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT \"fileID\", \"fileID\", ?\n"
				"FROM \"files\" WHERE 1");
			sqlite3_bind_int64(op, 1, depth);
			sqlite3_step(op);
			sqlite3_finalize(op);
			break;
		case EFSFullTextFilter:
			(void)0;
			sqlite3_stmt *const op = QUERY(db,
				"INSERT INTO \"results\"\n"
				"\t" "(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT f.\"fileID\", MIN(f.\"metaFileID\"), ?\n"
				"FROM \"fileContent\" AS f\n"
				"LEFT JOIN \"fulltext\" AS t\n"
				"\t" "ON (f.\"ftID\" = t.\"rowid\")\n"
				"WHERE t.\"text\" MATCH ?\n"
				"GROUP BY f.\"fileID\"");
			sqlite3_bind_int64(op, 1, depth);
			sqlite3_bind_text(op, 2, filter->data.string, -1, SQLITE_STATIC);
			sqlite3_step(op);
			sqlite3_finalize(op);
			break;
		case EFSBacklinkFilesFilter:
			(void)0;
			sqlite3_stmt *const op = QUERY(db,
				"INSERT INTO \"results\"\n"
				"\t" "(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT f.\"fileID\", MIN(l.\"metaFileID\"), ?\n"
				"FROM \"fileURIs\" AS f"
				"LEFT JOIN \"links\" AS l\n"
				"\t" "ON (f.\"URIID\" = l.\"sourceURIID\")\n"
				"LEFT JOIN \"URIs\" AS u\n"
				"\t" "ON (l.\"targetURIID\" = u.\"URIID\")\n"
				"WHERE u.\"URI\" = ?\n"
				"GROUP BY f.\"fileID\"");
			sqlite3_bind_int64(op, 1, depth);
			sqlite3_bind_text(op, 2, filter->data.string, -1, SQLITE_STATIC);
			sqlite3_step(op);
			sqlite3_finalize(op);
			break;
		case EFSFileLinksFilter:
			(void)0;
			sqlite3_stmt *const op = QUERY(db,
				"INSERT INTO \"results\"\n"
				"\t" "(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT f.\"fileID\", MIN(l.\"metaFileID\"), ?\n"
				"FROM \"fileURIs\" AS f"
				"LEFT JOIN \"links\" AS l\n"
				"\t" "ON (f.\"URIID\" = l.\"targetURIID\")\n"
				"LEFT JOIN \"URIs\" AS u\n"
				"\t" "ON (l.\"sourceURIID\" = u.\"URIID\")\n"
				"WHERE u.\"URI\" = ?\n"
				"GROUP BY f.\"fileID\"");
			sqlite3_bind_int64(op, 1, depth);
			sqlite3_bind_text(op, 2, filter->data.string, -1, SQLITE_STATIC);
			sqlite3_step(op);
			sqlite3_finalize(op);
			break;
/*		case EFSPermissionFilter:
			QUERY(db,
				"INSERT INTO \"results\"\n"
				"\t" "(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT \"fileID\"\n"
				"FROM \"filePermissions\"\n"
				"WHERE \"userID\" = ?");
			break;*/
		case EFSIntersectionFilter:
		case EFSUnionFilter:
			(void)0;
			EFSFilterList const *const list = filter->data.filters;
			sqlite3_stmt *const insertDepth = QUERY(db,
				"INSERT INTO \"depths\" (\"subdepth\", \"depth\")\n"
				"VALUES (?, ?)");
			for(index_t i = 0; i < list->count; ++i) {
				EFSFilterExec(&list->items[i], db, depth+i+1);
				sqlite3_bind_int64(insertDepth, 1, depth+i+1);
				sqlite3_bind_int64(insertDepth, 2, depth);
				sqlite3_step(insertDepth);
				sqlite3_reset(insertDepth);
			}
			sqlite3_finalize(insertDepth);

			sqlite3_stmt *const op = QUERY(db,
				"INSERT INTO \"results\"\n"
				"\t" "(\"fileID\", \"sort\", \"depth\")\n"
				"SELECT \"fileID\", MIN(\"sort\"), ?\n"
				"FROM \"results\"\n"
				"WHERE \"depth\" IN (\n"
				"\t" "SELECT \"subdepth\" FROM \"depths\"\n"
				"\t" "WHERE \"depth\" = ?)\n"
				"AND COUNT(\"fileID\") >= ?\n"
				"GROUP BY \"fileID\"");
			sqlite3_bind_int64(op, 1, depth);
			sqlite3_bind_int64(op, 2, depth);
			int64_t const threshold =
				EFSUnionFilter == filter-> type ? 1 : count;
			sqlite3_bind_int64(op, 3, threshold);
			sqlite3_step(op);
			sqlite3_finalize(op);

			sqlite3_stmt *const clearDepths = QUERY(db,
				"DELETE FROM \"depths\" WHERE \"depth\" = ?");
			sqlite3_bind_int64(clearDepths, 1, depth);
			sqlite3_step(clearDepths);
			sqlite3_finalize(clearDepths);

			sqlite3_stmt *const clearStack = QUERY(db,
				"DELETE FROM \"results\" WHERE depth > ?");
			sqlite3_bind_int64(clearStack, 1, depth);
			sqlite3_step(clearStack);
			sqlite3_finalize(clearStack);
			break;
		default:
			BTAssert(0, "Unrecognized filter type %d\n", (int)filter->type);
	}
}


