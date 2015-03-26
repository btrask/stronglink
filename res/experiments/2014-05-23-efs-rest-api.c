
void HTTPResponseBegin(fd_t const stream, uint16_t const status, str_t const *const message) {
	// TODO: TCP_CORK?
	str_t *str;
	BTErrno(asprintf(&str, "HTTP/1.1 %d %s\r\n", status, message);
	write(stream, str, strlen(str));
	free(str);
}
void HTTPHeaderWrite(fd_t const stream, str_t const *const field, str_t const *const value) {
	write(stream, field, strlen(field));
	write(stream, ": ", 2);
	write(stream, value, strlen(value));
	write(stream, "\r\n", 2);
}
void HTTPResponseEnd(fd_t const stream) {
	HTTPHeaderWrite(stream, "Connection", "close");
	write(stream, "\r\n", 2); // TODO: Safe for HEAD requests?
	// TODO: TCP_CORK?
}


static bool_t getFile(fd_t const res, str_t const *const method, str_t const *const URI) {

	// TODO: How are we supposed to access our headers?

	if(strcasecmp("GET", method)) return false;

	HTTPResponseBegin(res, 200, "OK");
	HTTPHeaderWrite(res, "Content-Type", "text/plain; charset=utf-8");
	HTTPResponseEnd(res);
	sendfile();
	close(res);

	return true;
}

