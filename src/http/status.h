// Ripped from Node.js

static strarg_t statusstr(uint16_t const status) {
	switch(status) {
		case 100: return "Continue";
		case 101: return "Switching Protocols";
		case 102: return "Processing"; // RFC 2518, obsoleted by RFC 4918
		case 200: return "OK";
		case 201: return "Created";
		case 202: return "Accepted";
		case 203: return "Non-Authoritative Information";
		case 204: return "No Content";
		case 205: return "Reset Content";
		case 206: return "Partial Content";
		case 207: return "Multi-Status"; // RFC 4918
		case 300: return "Multiple Choices";
		case 301: return "Moved Permanently";
		case 302: return "Moved Temporarily";
		case 303: return "See Other";
		case 304: return "Not Modified";
		case 305: return "Use Proxy";
		case 307: return "Temporary Redirect";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 402: return "Payment Required";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 406: return "Not Acceptable";
		case 407: return "Proxy Authentication Required";
		case 408: return "Request Time-out";
		case 409: return "Conflict";
		case 410: return "Gone";
		case 411: return "Length Required";
		case 412: return "Precondition Failed";
		case 413: return "Request Entity Too Large";
		case 414: return "Request-URI Too Large";
		case 415: return "Unsupported Media Type";
		case 416: return "Requested Range Not Satisfiable";
		case 417: return "Expectation Failed";
		case 418: return "I'm a teapot";               // RFC 2324
		case 422: return "Unprocessable Entity";       // RFC 4918
		case 423: return "Locked";                     // RFC 4918
		case 424: return "Failed Dependency";          // RFC 4918
		case 425: return "Unordered Collection";       // RFC 4918
		case 426: return "Upgrade Required";           // RFC 2817
		case 428: return "Precondition Required";      // RFC 6585
		case 429: return "Too Many Requests";          // RFC 6585
		case 431: return "Request Header Fields Too Large";// RFC 6585
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 504: return "Gateway Time-out";
		case 505: return "HTTP Version Not Supported";
		case 506: return "Variant Also Negotiates";    // RFC 2295
		case 507: return "Insufficient Storage";       // RFC 4918
		case 509: return "Bandwidth Limit Exceeded";
		case 510: return "Not Extended";               // RFC 2774
		case 511: return "Network Authentication Required"; // RFC 6585
		default: return NULL;
	}
}

