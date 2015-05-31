typedef struct {
	uint64_t startSortID;
	uint64_t startFileID;
	str_t *startURI;
	int startDir;
	int sortDir;
	size_t count;
} SLNFilterOpts;

static void parse_start(strarg_t const start, str_t **const URI, int *const dir) {
	if(!start) {
		*URI = NULL;
		*dir = -1;
	} else if('-' == start[0]) { // Unfortunately we can't use + due to encoding...
		*URI = '\0' == start[1] ? NULL : strdup(start+1);
		*dir = +1;
	} else {
		*URI = '\0' == start[0] ? NULL : strdup(start+0);
		*dir = -1;
	}
}
static int parse_dir(strarg_t const dir) {
	if(!dir) return -1;
	if('a' == dir[0]) return +1;
	if('z' == dir[0]) return -1;
	return -1;
}
static size_t parse_count(strarg_t const str, size_t const max) {
	if(!str) return max;
	if('\0' == str[0]) return max;
	long x = strtol(str, NULL, 10);
	if(x < 1) return 1;
	if(x > max) return max;
	return (size_t)x;
}
int SLNFilterOptsParse(strarg_t const qs, size_t const max, SLNFilterOpts *const opts) {
	static strarg_t const fields[] = {
		"start",
		"dir",
		"count",
	};
	str_t *values[numberof(fields)] = {};
	QSValuesParse(qs, values, fields, numberof(fields));
	parse_start(values[0], &opts->startURI, &opts->startDir);
	opts->sortDir = parse_dir(values[1]);
	opts->count = parse_count(values[2], max);
	QSValuesCleanup(values, numberof(values));
	return 0;
}
void SLNFilterOptsCleanup(SLNFilterOpts *const opts) {
	assert(opts);
	opts->startSortID = 0;
	opts->startFileID = 0;
	FREE(&opts->startURI);
	opts->startDir = 0;
	opts->sortDir = 0;
	assert_zeroed(opts, 1);
}




// TODO: If we're passing in the count as part of the options
// how should we get the actual length back?

int SLNSessionCopyFilteredURIs(SLNSessionRef const session, SLNFilterRef const filter, strarg_t const startURI, int const dir, str_t *out[], size_t *const count);

int SLNSessionCopyFilteredURIs(SLNSessionRef const session, SLNFilterRef const filter, SLNFilterOpts *const opts, str_t *out[], size_t *const count);

// yes, i think ssize_t is the appropriate technique

// no good, db errors can be positive too



