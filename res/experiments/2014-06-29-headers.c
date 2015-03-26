
typedef struct Headers* HeadersRef;

typedef enum {
	HEADER_FIELD_RETAINED = 1 << 0,
	HEADER_VALUE_RETAINED = 1 << 1,
} HeaderFlags;
typedef struct {
	strarg_t field;
	size_t flen;
	strarg_t value;
	size_t vlen;
	HeaderFlags flags;
} Header;
struct Headers {
	count_t count;
	count_t size;
	Header *items;
};

HeadersRef HeadersCreate(void) {
	
}

