

typedef struct EFSURIList* EFSURIListRef;

struct EFSURIList {
	count_t count;
	count_t size;
	str_t **items;
}

EFSURIListRef EFSURIListCreate(void) {
	EFSURIListRef const list = calloc(1, sizeof(EFSURIList));
	list->count = 0;
	list->size = 0;
	list->items = NULL;
	return list;
}
void EFSURIListFree(EFSURIListRef const list) {
	if(!list) return;
	for(index_t i = 0; i < list->size; ++i) {
		FREE(&list->items[i]);
	}
	FREE(&list->items);
	free(list);
}
count_t EFSURIListGetCount(EFSURIListRef const list) {
	if(!list) return 0;
	return list->count;
}
strarg_t EFSURIListGetURI(EFSURIListRef const list, index_t const i) {
	if(!list) return NULL;
	return list->items[i];
}
err_t EFSURIListAddURI(EFSURIListRef const list, strarg_t const URI, ssize_t const len) {
	if(!list) return 0;
	if(!URI) return 0;
	if(++list->count >= list->size) {
		list->size = MAX(10, list->size * 2);
		list->items = realloc(list->items, list->size * sizeof(str_t *));
		if(!list->items) {
			list->count = 0;
			list->size = 0;
			return -1;
		}
		memset(&list->items[list->count], 0, (list->size - list->count) * sizeof(str_t *));
	}
	list->items[list->count-1] = len < 0 ? strdup(URI) : strndup(URI, len);
	return 0;
}


typedef struct EFSMetaFileIndexer* EFSMetaFileIndexerRef;

#define BUFFER_SIZE_MAX (1024 * 100)

struct EFSMetaFileIndexer {
	str_t *buf;
	size_t size;
	size_t used;

	str_t *sourceURI;
	EFSURIListRef targetURIs;
}

EFSMetaFileIndexerRef EFSMetaFileIndexerCreate(strarg_t const type) {
	size_t plen = prefix("text/efs-meta", type);
	if(!plen) return NULL;
	if('\0' != type[plen] && ';' != type[plen]) return NULL;

	EFSMetaFileIndexerRef const parser = calloc(1, sizeof(struct EFSMetaFileIndexer));


	return parser;
}

ssize_t EFSMetaFileIndexerWrite(EFSMetaFileIndexerRef const parser, byte_t const *const buf, size_t const len) {
	if(!parser) return 0;
	if(parser->used + len + 1 >= parser->size) {
		parser->size = MIN(BUFFER_SIZE_MAX+1, MAX(parser->used + len, parser->size * 2));
		parser->buf = realloc(parser->buf, parser->size);
		if(!parser->buf) return -1;
	}
	size_t const remaining = parser->size - parser->used - 1;
	size_t const use = MIN(remaining, len);
	memcpy(parser->buf + parser->used, buf, use);
	parser->used += use;
	parser->buf[parser->used] = '\0';
	return use;
}
void EFSMetaFileIndexerEnd(EFSMetaFileIndexerRef const parser) {
	if(!parser) return NULL;

	// <http://daringfireball.net/2010/07/improved_regex_for_matching_urls>
	regex_t exp;
	regcomp(&exp,
		"\\b((?:[a-z][\\w\\-]+:(?:\\/{1,3}|[a-z0-9%])"
		"|www\\d{0,3}[.]|[a-z0-9.\\-]+[.][a-z]{2,4}\\/)"
		"(?:[^\\s()<>]+|\\(([^\\s()<>]+|(\\([^\\s()<>]+\\)))*\\)"
		")+(?:\\(([^\\s()<>]+|(\\([^\\s()<>]+\\)))*\\)"
		"|[^\\s`!()\\[\\]{};:'\".,<>?«»“”‘’]))",
		REG_ICASE | REG_NEWLINE);

	parser->targetURIs = EFSURIListCreate();

	off_t pos = 0;
	int eflags = 0;
	regmatch_t match;
	parser->buf[parser->used] = '\0';
	while(REG_NOMATCH != regexec(&exp, parser->buf + pos, 1, &match, REG_NOTBOL)) {

		str_t *URI = strndup(parser->buf + pos + match.rm_so, match.rm_eo - match.rm_so);
		EFSURIListAddURI(parser->targetURIs, URI);
		FREE(&URI);

		pos += match.rm_eo;
		eflags = ;
	}

	regfree(&exp);

	FREE(&parser->buf);
	parser->size = 0;
	parser->used = 0;
}
strarg_t EFSMetaFileIndexerGetSourceURI(EFSMetaFileIndexerRef const parser) {
	if(!parser) return NULL;
	return parser->sourceURI;
}
EFSURIListRef EFSMetaFileIndexerGetTargetURIs(EFSMetaFileIndexerRef const parser) {
	if(!parser) return NULL;
	return parser->targetURIs;
}


typedef struct {
	cothread_t parent;
	str_t *str;
	size_t len;
	off_t pos;
} parsestream;
char peek(parsestream *const stream) {
	if(stream->pos >= stream->len) co_switch(stream->parent);
	return stream->buf[stream->pos];
}
char pop(parsestream *const stream) {
	char c = peek(stream);
	++stream->pos;
	return c;
}

int isscheme(char const c) {
	if(isalpha(c) || isnum(c)) return 1;
	switch(c) {
		case '-': case '+': case '.':
			return 1;
		default:
			return 0;
	}
}
int isdomain(char const c) {
	if(isalpha(c) || isnum(c)) return 1;
	switch(c) {
		case '@': case '_': case '-':
		case '.': case ':':
			return 1;
		default:
			return 0;
	}
}
int ispath(char const c) {
	if(isalpha(c) || isnum(c)) return 1;
	switch(c) {
		case '@': case '_': case '-':
		case '.': case ':':
		case '/': case '=': case '&':
		case '+': case '?': case '#':
		case '~': case '%':
			return 1;
		default:
			return 0;
	}
}

/*
parsers to write(?)
- meta-file
- multipart

*/

#define URI_MAX 1024

size_t readURI(byte_t const *const buf, size_t const len) {

	size_t r = 0;

	while(scheme(src)) append(dst, pop(src));
	if(':' != append(dst, pop(src))) { return 0; }
	if('/' != append(dst, pop(src))) { return 0; }
	if('/' != append(dst, pop(src))) { return 0; }
	while(domain(src)) append(dst, pop(src));
	while(path(src)) append(dst, pop(src));

	return r;

}

static void parse(parsestream *const src) {

	while(space(s)) pop(src);
	parser->URI = readURI(src);
	while(space(s)) pop(src);
	if('\n' != pop(src)) return -1;
	readOptionalTitle();
	readBlankLine();
	readBody();


}




