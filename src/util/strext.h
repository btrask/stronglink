// Copyright 2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdarg.h>

char *vaasprintf(char const *const fmt, va_list ap);
char *aasprintf(char const *const fmt, ...) __attribute__((format(printf, 1, 2)));

int time_iso8601(char *const out, size_t const max);

void valogf(char const *const fmt, va_list ap);
void alogf(char const *const fmt, ...);

