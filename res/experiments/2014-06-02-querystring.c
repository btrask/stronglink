

void doSomething(str_t const *const qs) {
	str_t const *rest = qs;
	for(;;) {
		if('\0' == rest[0]) break; // Or other length check.
		if('#' == rest[0]) break;
		// just check for ? and & ?
	}
}


