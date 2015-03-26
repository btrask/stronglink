typedef err_t (*write_fn)(void *ctx, byte_t const *const buf, size_t const len);

#include <stdarg.h>

err_t wprintf(write_fn const write, void *const ctx, strarg_t const fmt, ...) {
	va_list args = va_start(fmt);
	err_t const r = async_vprintf(write, ctx, fmt, args);
	va_end(args);
	return r;
}
err_t vwprintf(write_fn const write, void *const ctx, strarg_t const fmt, va_list args) {
	strarg_t pos = fmt;
	off_t i = 0;
	for(;;) {
		if('\0' != pos[i] && '%' != pos[i]) { ++i; continue; }
		if(i) {
			if(write(ctx, (byte_t const *)pos, i) < 0) return -1;
			pos += i;
			i = 0;
		}
		if('\0' == pos[0]) break;
		switch(pos[1]) {
			case 's':
				strarg_t const str = va_arg(args, strarg_t);
				if(write(ctx, (byte_t const *)str, strlen(str)) < 0) return -1;
				pos += 2;
				break;
			default:
				BTAssert(0, "Format specifier %%%c not yet implemented", pos[1]);
				return -1;
		}
	}
	return 0;
}

