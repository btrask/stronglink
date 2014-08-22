#include "../strndup.h"
#include "QueryString.h"

void *QSValuesCopy(strarg_t const qs, strarg_t const fields[], count_t const count) {
	str_t **const values = calloc(count, sizeof(str_t *));
	if(!values) return NULL;
	strarg_t pos = qs;
	for(;;) {
		if('\0' == pos[0]) break;
		if('?' == pos[0] || '&' == pos[0]) ++pos;

		size_t flen = 0, sep = 0;
		for(;;) {
			char const x = pos[flen];
			if('\0' == x) break;
			if('=' == x) { ++sep; break; }
			if('&' == x) break;
			++flen;
		}
		size_t vlen = 0;
		if(sep) for(;;) {
			char const x = pos[flen+sep+vlen];
			if('\0' == x) break;
			if('&' == x) break;
			++vlen;
		}

		for(index_t i = 0; i < count; ++i) {
			if(!substr(fields[i], pos, flen)) continue;
			if(values[i]) continue;
			if(sep) {
				values[i] = QSUnescape(pos+flen+sep, vlen, true);
			} else {
				values[i] = strdup("true");
			}
		}
		pos += flen+sep+vlen;
	}
	return values;
}
void QSValuesFree(QSValues *const valuesptr, count_t const count) {
	str_t **values = *valuesptr;
	for(index_t i = 0; i < count; ++i) {
		FREE(&values[i]);
	}
	FREE((void **)valuesptr); values = NULL;
}

// Ported from Node.js QueryString.unescapeBuffer
str_t *QSUnescape(strarg_t const s, size_t const slen, bool_t const decodeSpaces) {
	str_t *const out = malloc(slen+1);
	if(!out) return NULL;
	enum { CHAR, HEX0, HEX1 } state = CHAR;
	char n = 0, m = 0, hexchar = 0;

	index_t outIndex = 0;
	for(index_t inIndex = 0; inIndex < slen; ++inIndex) {
		char c = s[inIndex];
		switch(state) {
		case CHAR:
			switch(c) {
			case '%':
				n = 0;
				m = 0;
				state = HEX0;
				break;
			case '+':
				if(decodeSpaces) c = ' ';
				// pass through
			default:
				out[outIndex++] = c;
				break;
			}
			break;
		case HEX0:
			state = HEX1;
			hexchar = c;
			if('0' <= c && c <= '9') {
				n = c - '0';
			} else if('a' <= c && c <= 'f') {
				n = c - 'a' + 10;
			} else if('A' <= c && c <= 'F') {
				n = c - 'A' + 10;
			} else {
				out[outIndex++] = '%';
				out[outIndex++] = c;
				state = CHAR;
			}
			break;
		case HEX1:
			state = CHAR;
			if('0' <= c && c <= '9') {
				m = c - '0';
			} else if('a' <= c && c <= 'f') {
				m = c - 'a' + 10;
			} else if('A' <= c && c <= 'F') {
				m = c - 'A' + 10;
			} else {
				out[outIndex++] = '%';
				out[outIndex++] = hexchar;
				out[outIndex++] = c;
				break;
			}
			out[outIndex++] = 16 * n + m;
			break;
		}
	}
	out[outIndex++] = '\0';
	return out;
}

