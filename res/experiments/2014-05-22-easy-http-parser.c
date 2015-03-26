
typedef char str_t;

typedef void (*header_cb)(void *const context, str_t const *const field, str_t const *const value);

#define HEADER_MAX_FIELD 40
#define HEADER_MAX_VALUE 1024

typedef struct {
	// Public
	header_cb callback;
	void *context;

	// Private
	str_t field[HEADER_MAX_FIELD];
	size_t field_length;
	str_t value[HEADER_MAX_VALUE];
	size_t value_length;
} header_parser;

static void header_emit(header_parser *const parser) {
	parser->callback(parser->context, &parser->field, &parser->value);
	memset(&parser->field, 0, parser->field_length);
	memset(&parser->value, 0, parser->value_length);
	parser->field_length = 0;
	parser->value_length = 0;
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
void header_parse_done(header_parser *const parser) {
	if(parser->field_length || parser->value_length) header_emit(parser);
}

