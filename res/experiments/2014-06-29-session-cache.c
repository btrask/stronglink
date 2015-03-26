#define COOKIE_CACHE_SIZE 1000

struct cached_cookie {
	int64_t sessionID;
	str_t *sessionKey;
	uint64_t atime;
};
static cached_cookie cookies[COOKIE_CACHE_SIZE] = {};


