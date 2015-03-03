


typedef struct {
	str_t *name;
	str_t *value;
	str_t *path;
	uint64_t maxage;
} cookie_t;

int cookie_init(cookie_t *const cookie, strarg_t const str) {
	if(!str) return -1;
	strarg_t mark = str;
	strarg_t x = str;
	while('\0' != *x && '=' != *x) x++;
	if(x <= mark) goto err;
	cookie->name = strndup(mark, x-mark);
	if(!name) goto err;
	mark = x;
	while('\0' != *x && ';' != *x) x++;
	if(x <= mark) goto err;
	cookie->value = strndup(mark, x-mark);
	if(!cookie->value) goto err;

	cookie->path = NULL;
	cookie->maxage = 0;
	// TODO: Parse other fields
	/*while(';' == *x) {
		mark = x;
		while('\0' != *x && '=' != *x) x++;
	}*/
	return 0;

err:
	cookie_destroy(cookie);
	return -1;
}
void cookie_destroy(cookie_t *const) {
	if(!cookie) return;
	FREE(&cookie->name);
	FREE(&cookie->value);
	FREE(&cookie->path);
	cookie->maxage = 0;
}


