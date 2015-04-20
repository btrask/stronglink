#include "../StrongLink.h"
#include "../http/QueryString.h"

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
	if(!max) return 0;
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
	parse_start(values[0], &opts->URI, &opts->dir);
	opts->sortID = opts->dir > 0 ? 0 : UINT64_MAX;
	opts->fileID = opts->dir > 0 ? 0 : UINT64_MAX;
	opts->outdir = parse_dir(values[1]);
	opts->count = parse_count(values[2], max);
	QSValuesCleanup(values, numberof(values));
	return DB_SUCCESS;
}
void SLNFilterOptsCleanup(SLNFilterOpts *const opts) {
	assert(opts);
	FREE(&opts->URI);
	opts->sortID = 0;
	opts->fileID = 0;
	opts->dir = 0;
	opts->outdir = 0;
	opts->count = 0;
	assert_zeroed(opts, 1);
}

