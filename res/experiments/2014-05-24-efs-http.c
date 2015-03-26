
void HTTPWriteResponseBegin(fd_t const stream, uint16_t const status, str_t const *const message) {
	// TODO: TCP_CORK?
	str_t *str;
	BTErrno(asprintf(&str, "HTTP/1.1 %d %s\r\n", status, message);
	write(stream, str, strlen(str));
	free(str);
}
void HTTPWriteHeader(fd_t const stream, str_t const *const field, str_t const *const value) {
	write(stream, field, strlen(field));
	write(stream, ": ", 2);
	write(stream, value, strlen(value));
	write(stream, "\r\n", 2);
}
void HTTPWriteResponseEnd(fd_t const stream) {
//	HTTPWriteHeader(stream, "Connection", "close");
	write(stream, "\r\n", 2); // TODO: Safe for HEAD requests?
	// TODO: TCP_CORK?
}

typedef struct {
	HTTPMethod method;
	str_t *URI;
	str_t **fields;
	str_t **values;
	EFSCount headerCount;
	byte_t *
} HTTPRequest;

err_t HTTPReadRequest(fd_t const stream, HTTPRequest *const req) {
	
}
ssize_t HTTPReadBody(fd_t const stream, byte_t *const buf, size_t const len) {

}




typedef struct {
	fd_t stream;

	// Request
	HTTPMethod method;
	str_t *URI;
	str_t **fields;
	str_t **values;
	count_t headerCount;

	// Response
	bool_t keepalive;

	// Private
	http_parser parser;
	byte_t *peek;
	size_t peekLength;

} HTTPConnection;


void HTTPConnectionBeginResponse(HTTPConnection *const conn, uint16_t const status, str_t const *const msg) {

}
void HTTPConnectionWriteHeader(HTTPConnection *const conn, str_t const *const field, str_t const *const value) {

}
void HTTPConnectionBeginBody(HTTPConnection *const conn) {

}

void HTTPConnectionParseRequest(HTTPConnection *const conn) {
	http_parser_init(&conn->parser, HTTP_REQUEST);
	conn->parser.data = conn;

	for(;;) {
		byte_t buf[BUFFER_SIZE];
		ssize_t const readLength = read(conn->stream, buf, BUFFER_SIZE);

		if(-1 == readLength) ; // TODO: Bail.

		size_t const parseLength = http_parser_execute(&conn->parser, settings, buf, readLength);


	}
}






typedef struct {
	HTTPMethod method;
	str_t *URI;
	str_t **fields;
	str_t **values;
	count_t headerCount;
} HTTPRequest;
typedef struct {
	fd_t stream;
	http_parser parser;
	HTTPRequest *req;
} HTTPConnection;

void serverthing() {
	HTTPConnection conn = {};
	conn.stream = stream;
	conn.parser.data = &conn;
	http_parser_init(&conn, HTTP_REQUEST);
	listener(&conn);
}
void HTTPParseRequest(HTTPConnection *const conn, HTTPRequest *const req) {
	conn->req = req;
	for(;;) {
		byte_t buf[BUFFER_SIZE];
		ssize_t const rlen = read(conn->stream, buf, BUFFER_SIZE);
		if(-1 == readLength) ; // TODO: Bail.
		size_t const plen = http_parser_execute(&conn->parser, &settings, buf, rlen);


	}
}
ssize_t HTTPReadBody(HTTPConnection *const conn, byte_t *const buf, size_t const blen) {
	if(conn->peak) {
		memcpy(buf, conn->peak, MIN(conn->peakLength, );
		
	}
	ssize_t const rlen = read(conn->stream, buf, blen);
	if(-1 == readLength) ; // TODO: Bail.
	size_t const plen = http_parser_execute(&conn->parser, &settings, buf, rlen);
	
}















void readChunk(HTTPConnection *const conn) {

	

}


















typedef struct {
	str_t *field;
	size_t fsize;
	str_t *value;
	size_t vsize;
} HTTPHeader;
typedef struct {
	count_t count;
	HTTPHeader items[0];
} HTTPHeaderList;

typedef struct HTTPConnection* HTTPConnectionRef;

typedef struct HTTPConnection {
	// Connection
	fd_t stream;
	http_parser *parser;
	byte_t *buf;

	// Request
//	HTTPMethod method; // parser->method
	str_t *URI;
	size_t URISize;
	count_t headersSize;
	HTTPHeaderList *headers;
	byte_t *chunk;
	size_t chunkLength;
	bool_t eof;
} HTTPConnection;


HTTPMethod HTTPConnectionGetRequestMethod(HTTPConnectionRef const conn) {
	if(!conn) return 0;
	return conn->parser.method;
}
str_t const *HTTPConnectionGetRequestURI(HTTPConnectionRef const conn) {
	if(!conn) return NULL;
	return conn->URI;
}
HTTPHeaderList const *HTTPConnectionGetHeaders(HTTPConnectionRef const conn) {
	// TODO: Convenient method for random access lookup.
	if(!conn) return NULL;
	return conn->headers;
}
ssize_t HTTPConnectionRead(HTTPConnectionRef const conn, byte_t *const buf, size_t const len) {
	// TODO: Zero-copy version that provides access to the original buffer.
	if(!conn) return -1;
	if(!conn->chunkLength) {
		return conn->eof ? 0 : -1;
	}
	size_t const used = MIN(len, conn->chunkLength);
	memcpy(buf, conn->chunk, used);
	conn->chunk += used;
	conn->chunkLength -= used;
	if(-1 == readOnce(conn)) return -1;
	return used;
}

static err_t append(str_t **const dst, size_t *const dsize, str_t const *const src, size_t const len) {
	size_t const old = *dst ? strlen(*dst) : 0;
	if(old + len > *dsize) {
		*dsize = MAX(10, MAX(*dsize * 2, len+1));
		*dst = realloc(*dst, *dsize);
		if(!*dst) {
			*dsize = 0;
			return -1;
		}
	}
	memcpy(*dst + old, src, len);
	(*dst)[old+len] = '\0';
	return 0;
}
static int on_url(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	return append(&conn->URI, &conn->URISize, at, len);
}
static int on_header_field(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	if(conn->headers->items[conn->headers->count].value) {
		if(conn->headers->count >= conn->headersSize) {
			conn->headersSize *= 2;
			conn->headers = realloc(conn->headers, sizeof(HTTPHeaderList) + sizeof(HTTPHeader) * conn->headersSize);
			if(!conn->headers) return -1;
			memset(&conn->headers[conn->headers->count], 0, sizeof(HTTPHeader) * (conn->headersSize - conn->headers->count));
		}
		++conn->headers->count;
	}
	HTTPHeader *const header = &conn->headers->items[conn->headers->count];
	return append(&header->field, &header->fsize, at, len);
}
static int on_header_value(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	HTTPHeader *const header = &conn->headers->items[conn->headers->count];
	return append(&header->value, &header->vsize, at, len);
}
static int on_headers_complete(http_parser *const parser) {
	HTTPConnectionRef const conn = parser->data;
	++conn->headers->count; // Last header finished.
	// TODO: Lowercase and sort by field name for faster access.
	return 0;
}
static int on_body(http_parser *const parser, char const *const at, size_t const len) {
	HTTPConnectionRef const conn = parser->data;
	assert(!conn->chunkLength && "Chunk already waiting");
	conn->chunk = at;
	conn->chunkLength = len;
	return 0;
}
static int on_message_complete(http_parser *const parser) {
	HTTPConnectionRef const conn = parser->data;
	conn->eof = true;
	return 0;
}
static struct http_parser_settings const settings = {
	.on_message_begin = NULL,
	.on_url = on_url,
	.on_header_field = on_header_field,
	.on_header_value = on_header_value,
	.on_headers_complete = on_headers_complete,
	.on_body = on_body,
	.on_message_complete = on_message_complete,
};

static err_t readOnce(HTTPConnectionRef const conn) {
	assert(!conn->eof && "Reading past end of message");
	ssize_t const rlen = read(conn->stream, conn->buf, BUFFER_SIZE);
	if(-1 == rlen) return -1;
	size_t const plen = http_parser_execute(conn->parser, &settings, conn->buf, rlen);
	if(-1 == plen) return -1;
	return 0;
}
static err_t readHeaders(HTTPConnectionRef const conn) {
	for(;;) {
		if(-1 == readOnce(conn)) return -1;
		if(conn->chunkLength) return 0;
	}
}
static void handleMessage(HTTPServer *const server, HTTPConnectionRef const conn) {
	conn->headersSize = 10;
	conn->headers = calloc(1, sizeof(HTTPHeaderList) + sizeof(HTTPHeader) * conn->headersSize);

	if(-1 == readHeaders(conn)) {
		server->listener(server->context, NULL);
		return;
	}
	server->listener(server->context, conn);

	free(conn->headers); conn->headers = NULL;
	conn->headerSize = 0;
}






