

typedef struct EFSConfiguration *EFSConfigurationRef;

/*
- number of download fibers per pull
- pull queue size
- max meta-file size
- fsync (show of effort) durable/consistent/fast
	- ACID, ACI, ?
- blog results per page
- user query restrictions
*/
// blog settings should be in a separate file, or the same file but parsed separately?

// this is... shaping up to look pretty ugly

struct EFSConfiguration {
	size_t pull_concurrent_connections;
	size_t submissions_per_transaction;
	size_t metafile_index_bytes;
	enum durability_mode;
};


int EFSConfigurationCreate(uv_file const fd, EFSConfigurationRef *const out) {
	assert(out);
	if(fd < 0) return UV_EINVAL;

	EFSConfigurationRef conf = calloc(1, sizeof(struct EFSConfiguration));
	if(!conf) return UV_ENOMEM;
	int rc = 0;

	uv_buf_t buf[1];
	buf->base = malloc(BUF_LEN);
	if(!buf->base) { rc = UV_ENOMEM; goto cleanup; }
	buf->len = BUF_LEN;

	ssize_t len;
	int64_t pos = 0;
	for(;;) {
		len = async_fs_read(fd, buf, 1, pos);
		if(len < 0) { rc = len; goto cleanup; }
		
	}



	*out = conf;

cleanup:

	if(rc < 0) EFSConfigurationFree(&conf);
	return rc;
}



























