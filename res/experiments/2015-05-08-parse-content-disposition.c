

// raw filename "\".txt:
// Content-Disposition: form-data; name="file"; filename="\"\\".txt"

static int parse_content_disposition(strarg_t const x, str_t **const name, str_t **const filename) {


	sscanf(x, "");




}



static size_t scan_quoted(char const *const v) {
	size_t i = 0;
	if('"' != v[i++]) return 0;
	for(;; i++) {
		if('\0' == v[i]) break;
		if('"' == v[i]) break;
		if('\\' == v[i] && '"' == v[i+1]) i++;
	}
	return i;
}
static char *copy_quoted(char const *const v, size_t const vlen) {
	char *const r = malloc(vlen);
	if(!r) return NULL;
	size_t i;
	if('"' == v[i]) i++;
	for(;;) {
		if('\0' == v[i]) break;
		if(
	}
}

static size_t quoted(char const *const src, char *const dst, size_t const max) {
	size_t s = 0;
	size_t d = 0;
	if('"' != src[s++]) return 0;
	for(;;) {
		if('\0' == src[s]) break;
		if('"' == src[s]) break;
		if('\\' != src[s]) { dst[d++] = *s++; continue; }
		if('"' == src[s+1]) { dst[d++] = '"'; continue; }
		dst[d++] = '\\';
	}
	dst[d++] = '\0';
	return s;
}



void MIMEOptionsParse(char const *const opts, char *values[], char const *const fields[], size_t const count) {
	for(size_t i = 0; i < count; i++) assert(!values[i]);
	size_t x = 0;
	for(;;) {
		if(';' != opts[x++]) break;
		while(isspace(opts[x])) x++;
		char const *const f = opts+x;
		while('=' != opts[x] && ';' != opts[x] && '\0' != opts[x]) x++;
		size_t const flen = opts+x - f;
		if('=' == opts[x]) x++;

		char const *v;
		size_t vlen;
		if('"' == opts[x]) {
			size_t y = 0;
			for(;;) {
				
			}
		} else {
			v = opts+x;
			while(';' != opts[x] && '\0' != opts[x]) x++;
			vlen = opts+x - v;
		}

		for(size_t i = 0; i < count; i++) {
			if(!substr(fields[i], f, flen)) continue;
			if(values[i]) break;
			if(vlen) values[i] = strndup(v, vlen);
			else values[i] = strdup("true");
			break;
		}
	}
}
void MIMEOptionsCleanup(char **const values, size_t const count) {
	for(size_t i = 0; i < count; ++i) {
		free(values[i]); values[i] = NULL;
	}
}















