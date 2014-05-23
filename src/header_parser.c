#include <string.h>
#include "header_parser.h"

// TODO: Find where this stupid macro is declared
#define MIN(a, b) ({ __typeof__(a) const __a = (a); __typeof__(b) const __b = (b); __a < __b ? __a : __b; })

static void header_emit(header_parser *const parser) {
	parser->callback(parser->context, parser->field, parser->value);
	header_parse_clear(parser);
}

void header_parse_field(header_parser *const parser, char const *const at, size_t const len) {
	if(parser->value_length) header_emit(parser);
	size_t safeLen = MIN(len, HEADER_MAX_FIELD - parser->field_length);
	memcpy(parser->field + parser->field_length, at, safeLen);
	parser->field_length += safeLen;
}
void header_parse_value(header_parser *const parser, char const *const at, size_t const len) {
	size_t safeLen = MIN(len, HEADER_MAX_VALUE - parser->value_length);
	memcpy(parser->value + parser->value_length, at, safeLen);
	parser->value_length += safeLen;
}
void header_parse_complete(header_parser *const parser) {
	if(parser->field_length || parser->value_length) header_emit(parser);
}
void header_parse_clear(header_parser *const parser) {
	memset(&parser->field, 0, parser->field_length);
	memset(&parser->value, 0, parser->value_length);
	parser->field_length = 0;
	parser->value_length = 0;
}


