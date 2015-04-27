#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "QueryString.h"

void QSValuesParse(char const *const qs, char *values[], char const *const fields[], size_t const count) {
	for(size_t i = 0; i < count; i++) assert(!values[i]);
	char const *pos = qs;
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

		for(size_t i = 0; i < count; ++i) {
			if(0 != strncasecmp(fields[i], pos, flen)) continue;
			if(values[i]) continue;
			if(sep) {
				values[i] = QSUnescape(pos+flen+sep, vlen, true);
			} else {
				values[i] = strdup("true");
			}
		}
		pos += flen+sep+vlen;
	}
}
void QSValuesCleanup(char **const values, size_t const count) {
	for(size_t i = 0; i < count; ++i) {
		free(values[i]); values[i] = NULL;
	}
}

// Ported from Node.js QueryString.unescapeBuffer
char *QSUnescape(char const *const s, size_t const slen, bool const decodeSpaces) {
	char *const out = malloc(slen+1);
	if(!out) return NULL;
	enum { CHAR, HEX0, HEX1 } state = CHAR;
	char n = 0, m = 0, hexchar = 0;

	size_t outIndex = 0;
	for(size_t inIndex = 0; inIndex < slen; ++inIndex) {
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

// Ripped from V8
static bool unescape(char const cc) {
      if (isalnum(cc)) return true;
      // !
      if (cc == 33) return true;
      // '()*
      if (39 <= cc && cc <= 42) return true;
      // -.
      if (45 <= cc && cc <= 46) return true;
      // _
      if (cc == 95) return true;
      // ~
      if (cc == 126) return true;

      return false;
}
char *QSEscape(char const *const s, size_t const slen, bool const encodeSpaces) {
	char *const out = malloc(slen*3+1); // Worst case
	if(!out) return NULL;
	char const *const map = "0123456789ABCDEF";
	size_t j = 0;
	for(size_t i = 0; i < slen; i++) {
		char const cc = s[i];
		if(encodeSpaces && isspace(cc)) {
			out[j++] = '+';
		} else if(unescape(cc)) {
			out[j++] = cc;
		} else {
			out[j++] = '%';
			out[j++] = map[0xf & (cc >> 4)];
			out[j++] = map[0xf & (cc >> 0)];
		}
	}
	out[j++] = '\0';
	return out;
}

