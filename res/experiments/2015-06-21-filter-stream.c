static int sendURIBatch(SLNSessionRef const session, SLNFilterRef const filter, SLNFilterOpts *const opts, HTTPConnectionRef const conn) {
	size_t count;
	str_t *URIs[QUERY_BATCH_SIZE];
	opts->count = QUERY_BATCH_SIZE;
	int rc = SLNSessionCopyFilteredURIs(session, filter, opts, URIs, &count);
	if(DB_SUCCESS != rc) return rc;
	if(0 == count) return DB_NOTFOUND;
	rc = 0;
	for(size_t i = 0; i < count; i++) {
		uv_buf_t const parts[] = {
			uv_buf_init((char *)URIs[i], strlen(URIs[i])),
			uv_buf_init((char *)STR_LEN("\r\n")),
		};
		rc = HTTPConnectionWriteChunkv(conn, parts, numberof(parts));
		if(rc < 0) break;
	}
	for(size_t i = 0; i < count; i++) FREE(&URIs[i]);
	if(rc < 0) return DB_EIO;
	return DB_SUCCESS;
}
static bool parse_wait(strarg_t const str) {
	if(!str) return true;
	if(0 == strcasecmp(str, "")) return false;
	if(0 == strcasecmp(str, "0")) return false;
	if(0 == strcasecmp(str, "false")) return false;
	return true;
}
static void SLNFilterResultsWrite(SLNSessionRef const session, SLNFilterRef const filter, SLNFilterOpts *const opts, HTTPConnectionRef const conn) {
	// TODO: Accept count and use it for the total number of results.
	opts->count = 0;

	// We're sending a series of batches, so reversing one batch
	// doesn't make sense.
	opts->outdir = opts->dir;

	static strarg_t const fields[] = { "wait" };
	str_t *values[numberof(fields)] = {};
	QSValuesParse(qs, values, fields, numberof(fields));
	bool const wait = parse_wait(values[0]);
	QSValuesCleanup(values, numberof(values));

	// I'm aware that we're abusing HTTP for sending real-time push data.
	// I'd also like to support WebSocket at some point, but this is simpler
	// and frankly probably more widely supported.
	// Note that the protocol doesn't really break even if this data is
	// cached. It DOES break if a proxy tries to buffer the whole response
	// before passing it back to the client. I'd be curious to know whether
	// such proxies still exist in 2015.
	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn,
		"Content-Type", "text/uri-list; charset=utf-8");
	HTTPConnectionWriteHeader(conn, "Cache-Control", "no-store");
	HTTPConnectionWriteHeader(conn, "Vary", "*");
	HTTPConnectionBeginBody(conn);
	int rc;

	for(;;) {
		rc = sendURIBatch(session, filter, opts, conn);
		if(DB_NOTFOUND == rc) break;
		if(DB_SUCCESS == rc) continue;
		fprintf(stderr, "Query error: %s\n", db_strerror(rc));
		goto cleanup;
	}

	if(!wait || opts->dir < 0) goto cleanup;

	SLNRepoRef const repo = SLNSessionGetRepo(session);
	for(;;) {
		uint64_t const timeout = uv_now(async_loop)+(1000 * 30);
		rc = SLNRepoSubmissionWait(repo, opts->sortID, timeout);
		if(UV_ETIMEDOUT == rc) {
			uv_buf_t const parts[] = { uv_buf_init((char *)STR_LEN("\r\n")) };
			rc = HTTPConnectionWriteChunkv(conn, parts, numberof(parts));
			if(rc < 0) break;
			continue;
		}
		assert(rc >= 0); // TODO: Handle cancellation?

		for(;;) {
			rc = sendURIBatch(session, filter, opts, conn);
			if(DB_NOTFOUND == rc) break;
			if(DB_SUCCESS == rc) continue;
			fprintf(stderr, "Query error: %s\n", db_strerror(rc));
			goto cleanup;
		}
	}

cleanup:
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);
	SLNFilterOptsCleanup(opts);
}

