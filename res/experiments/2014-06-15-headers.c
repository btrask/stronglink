
typedef struct {
	strarg_t const name;
	size_t const size;
} HeaderField;
typedef struct {
	count_t count;
	HeaderField *items;
} HeaderFieldList;

typedef struct {
	str_t *content_type;
} EFSHeaders;
static HeaderField const EFSHeaderFields[] = {
	{"content-type", 100},
};
static HeaderFieldList const EFSHeaderFieldList = {
	.count = numberof(EFSHeaderFields),
	.items = &EFSHeaderFields,
};


