
typedef struct LineParser* LineParserRef;
typedef int (*LineParserCB)(void *, strarg_t, size_t);

#define LINE_MAX 1024

struct LineParser {
	LineParserCB cb;
	void *context;
	off_t pos;
	bool_t discard;
	byte_t fixed[LINE_MAX];
}

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

void LineParserWrite(LineParserRef const p, byte_t const *const buf, size_t const len) {
	if(!p) return;
	if(p->discard) {
		size_t const discard = linebreak(buf, len);
		size_t const remainder = len - discard;
		if(!remainder) return;
		p->discard = false;
		return LineParserWrite(p, buf + discard, remainder);
	}
	if(p->pos) {
		size_t const extra = linebreak(buf, len);
		size_t const max = LINE_MAX - p->pos;
		if(extra > max) {
			if(extra >= len) {
				p->discard = true;
				return;
			}
			return LineParserWrite(p, buf + extra, len - extra);
		}
		memcpy(p->fixed + p->pos, buf, extra);
		p->pos += extra;
		if(extra >= len) return;
		p->cb(p->context, p->fixed, p->pos);
		p->pos = 0;
		return LineParserWrite(p, buf + extra, len - extra);
	}
	off_t pos = 0;
	for(;;) {
		size_t const line = linebreak(buf + pos, len - pos);
		if(pos + line >= len) {
			if(line > LINE_MAX) {
				p->discard = true;
				return;
			}
			memcpy(p->fixed, buf + p->pos, line);
			p->pos = line;
			return;
		}
		p->cb(p->context, buf + pos, line);
	}
}
void LineParserEnd(LineParserRef const p) {
	if(!p) return;
	if(p->pos && !p->discard) {
		p->cb(p->context, p->fixed, p->pos);
	}
	LineParserReset(p);
}
void LineParserReset(LineParserRef const p) {
	if(!p) return;
	p->pos = 0;
	p->discard = false;
}


