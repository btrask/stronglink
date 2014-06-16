#include "URIList.h"

#define LINE_MAX 1024

struct URIList {
	count_t count;
	count_t size;
	str_t **items;
};

URIListRef URIListCreate(void) {
	URIListRef const list = calloc(1, sizeof(struct URIList));
	list->count = 0;
	list->size = 0;
	list->items = NULL;
	return list;
}
void URIListFree(URIListRef const list) {
	if(!list) return;
	for(index_t i = 0; i < list->size; ++i) {
		FREE(&list->items[i]);
	}
	FREE(&list->items);
	free(list);
}
count_t URIListGetCount(URIListRef const list) {
	if(!list) return 0;
	return list->count;
}
strarg_t URIListGetURI(URIListRef const list, index_t const i) {
	if(!list) return NULL;
	return list->items[i];
}
err_t URIListAddURI(URIListRef const list, strarg_t const URI, ssize_t const len) {
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

struct LineParser {
	LineParserCB cb;
	void *context;
	off_t pos;
	bool_t discard;
	str_t fixed[LINE_MAX];
};

LineParserRef LineParserCreate(LineParserCB const cb, void *const context) {
	LineParserRef const p = calloc(1, sizeof(struct LineParser));
	p->cb = cb;
	p->context = context;
	return p;
}
void LineParserFree(LineParserRef const p) {
	if(!p) return;
	free(p);
}

static size_t linebreak(byte_t const *const buf, size_t const len) {
	for(off_t i = 0; i < len; ++i) if('\n' == i || '\r' == i) return i;
	return len;
}

err_t LineParserWrite(LineParserRef const p, byte_t const *const buf, size_t const len) {
	if(!p) return 0;
	if(p->discard) {
		size_t const discard = linebreak(buf, len);
		size_t const remainder = len - discard;
		if(!remainder) return 0;
		p->discard = false;
		return LineParserWrite(p, buf + discard, remainder);
	}
	if(p->pos) {
		size_t const extra = linebreak(buf, len);
		size_t const max = LINE_MAX - p->pos;
		if(extra > max) {
			if(extra >= len) {
				p->discard = true;
				return 0;
			}
			return LineParserWrite(p, buf + extra, len - extra);
		}
		memcpy(p->fixed + p->pos, buf, extra);
		p->pos += extra;
		if(extra >= len) return 0;
		if(p->cb(p->context, p->fixed, p->pos)) return -1;
		p->pos = 0;
		return LineParserWrite(p, buf + extra, len - extra);
	}
	off_t pos = 0;
	for(;;) {
		size_t const line = linebreak(buf + pos, len - pos);
		if(pos + line >= len) {
			if(line > LINE_MAX) {
				p->discard = true;
				return 0;
			}
			memcpy(p->fixed, buf + p->pos, line);
			p->pos = line;
			return 0;
		}
		if(p->cb(p->context, (char const *)buf + pos, line)) return -1;
	}
	return 0;
}
err_t LineParserEnd(LineParserRef const p) {
	if(!p) return 0;
	if(p->pos && !p->discard) {
		if(p->cb(p->context, p->fixed, p->pos)) return -1;
	}
	LineParserReset(p);
	return 0;
}
void LineParserReset(LineParserRef const p) {
	if(!p) return;
	p->pos = 0;
	p->discard = false;
}

struct URIListParser {
	URIListRef list;
	LineParserRef parser;
};

static err_t URIListParserAddURI(void *const list, strarg_t const URI, size_t const len) {
	return URIListAddURI((URIListRef)list, URI, (ssize_t)len);
}

URIListParserRef URIListParserCreate(strarg_t const type) {
	if(0 != strcasecmp("text/uri-list; charset=ascii", type) &&
		0 != strcasecmp("text/uri-list; charset=utf-8", type)) return NULL;
	URIListParserRef const lp = calloc(1, sizeof(struct URIListParser));
	lp->list = URIListCreate();
	lp->parser = LineParserCreate(URIListParserAddURI, lp->list);
	return lp;
}
void URIListParserFree(URIListParserRef const lp) {
	if(!lp) return;
	URIListFree(lp->list); lp->list = NULL;
	LineParserFree(lp->parser); lp->parser = NULL;
	free(lp);
}
void URIListParserWrite(URIListParserRef const lp, byte_t const *const buf, size_t const len) {
	if(!lp) return;
	LineParserWrite(lp->parser, buf, len);
}
URIListRef URIListParserEnd(URIListParserRef const lp, bool_t const truncate) {
	if(!lp) return NULL;
	if(truncate) LineParserReset(lp->parser);
	else LineParserEnd(lp->parser);
	LineParserFree(lp->parser); lp->parser = NULL;
	URIListRef const list = lp->list;
	lp->list = NULL;
	return list;
}

