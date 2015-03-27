


struct EFSMetaFileParser {

};



// we dont even need an object
// its just a single parser function


int EFSMetaFileParse(DB_txn *const txn, uint64_t fileID, strarg_t const path, strarg_t const type) {

}


// that easy?




// okay, so far so good
// but our error handling is messed up
// first of all, the aboslute most likely and important error scenario is a json parse error from a malformed meta-file
// second most important is a database error
// and then finally a read error
// parse errors should not be retried and should be reported to the caller
// for syncs, the entire sync probably needs to be disabled until the user can intervene
// for file submissions, the problem should be reported to the user
// even though the user doesn't directly generate meta-files
// really it's an application problem


// our old meta-file code just uses assertions on all database errors...
// i'm kind of thinking abort() on error isnt a bad idea
// well its bad for a server...


// well, one thing
// ...
// forgot

// oh
// error handling is a good argument against having a single writer for all submissions
// we need to be able to track what should happen when a submissionf fails
// for syncs, we need to stop all subsequent submissions
// for regular uploads, each submission should be independent

// so maybe we need two "layers" of aggregation
// the bottom layer each item is independent, but they're all wrapped in one big transaction
// when an item fails, its nested transaction is rolled back but the rest continue
// and each item reports its error to the fiber that submitted it
// then the second layer, all of the items must succeed or fail together

// the purpose of the two layers is that if we have 1000 different syncs open
// but each sync is only getting 1 file per second
// we still want to do some sort of batching



// okay, so what are we doing?
// in theory we could have some sort of "retriable error" class
// so we could know whether to retry or fail
// but in practice it's probably easier to just retry a few times and then fail
// or use exponential backoff

// in which case returning -1 or w/e is okay...?


// interesting...
// wrapping http and multipart in simple functions that returned the state worked well
// but i dont think it would actually work for json parsing
// because with say http, the number of possible next states is typically no more than two
// whereas with json you have to handle each possible type



int EFSSubmissionParseMetaFile(EFSSubmissionRef const sub, uint64_t const fileID, DB_txn *const txn) {
	if(!sub) return DB_EINVAL;
	if(!fileID) return DB_EINVAL;
	if(!txn) return DB_EINVAL;
	int rc = 0;
	uv_buf_t buf[1] = {};
	yajl_handle parser = NULL;
	DB_txn *subtxn = NULL;

	buf->base = uv_buf_init(malloc(1024 * 8), 1024 * 8);
	if(!buf->base) { rc = DB_ENOMEM; goto cleanup; }

	int64_t pos = 0;
	ssize_t len = async_fs_read(sub->tmpfile, buf, 1, pos);
	if(len < 0) { rc = -1; goto cleanup; }

	for(size_t i = 0; i < MIN(len, URI_MAX); i++) if('\n' == buf->base[i]) break;
	if(i >= MIN(len, URI_MAX)) { rc = -1; goto cleanup; }
	str_t targetURI[URI_MAX];
	memcpy(targetURI, buf->base, i);
	targetURI[i] = '\0';
	pos += i+1;

	// TODO: Support sub-transactions in LevelDB backend.
	// TODO: db_txn_begin should support NULL env.
//	rc = db_txn_begin(NULL, txn, DB_RDWR, &subtxn);
//	if(DB_SUCCESS != rc) goto cleanup;
	subtxn = txn;

	uint64_t metaFileID = 0;
	rc = addMetaFile(subtxn, fileID, target, &metaFileID);
	if(DB_SUCCESS != rc) goto cleanup;

	parser_context context = {
		.txn = subtxn,
		.metaFileID = metaFileID,
		.targetURI = targetURI,
		.state = s_start,
		.field = NULL,
	};
	parser = yajl_alloc(&callbacks, NULL, &context);
	if(!parser) { rc = DB_ENOMEM; goto cleanup; }
	yajl_config(parser, yajl_allow_partial_values, (int)true);

	yajl_status status = yajl_status_ok;
	for(;;) {
		len = async_fs_read(sub->tmpfile, buf, 1, pos);
		if(len < 0) { rc = -1; goto cleanup; }
		if(0 == len) break;
		status = yajl_parse(parser, buf->base, buf->len);
		if(yajl_status_ok != status) { rc = -1; goto cleanup; }
		pos += len;
	}
	status = yajl_complete_parse(parser);
	if(yajl_status_ok != status) { rc = -1; goto cleanup; }

//	rc = db_txn_commit(subtxn); subtxn = NULL;

cleanup:
//	db_txn_abort(subtxn); subtxn = NULL;
	FREE(&buf->base);
	yajl_free(parser); parser = NULL;
	return rc;
}























