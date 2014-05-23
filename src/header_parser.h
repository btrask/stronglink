#include <unistd.h>

typedef void (*header_cb)(void *const context, char const *const field, char const *const value);

#define HEADER_MAX_FIELD 40
#define HEADER_MAX_VALUE 1024

typedef struct {
	// Public
	header_cb callback;
	void *context;

	// Private
	char field[HEADER_MAX_FIELD];
	size_t field_length;
	char value[HEADER_MAX_VALUE];
	size_t value_length;
} header_parser;

void header_parse_field(header_parser *const parser, char const *const at, size_t const len);
void header_parse_value(header_parser *const parser, char const *const at, size_t const len);
void header_parse_complete(header_parser *const parser);
void header_parse_clear(header_parser *const parser);

