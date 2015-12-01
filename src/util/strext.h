// Copyright 2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdarg.h>
#include <unistd.h>

char *vaasprintf(char const *const fmt, va_list ap) __attribute__((format(printf, 1, 0)));
char *aasprintf(char const *const fmt, ...) __attribute__((format(printf, 1, 2)));

int time_iso8601(char *const out, size_t const max);

void valogf(char const *const fmt, va_list ap) __attribute__((format(printf, 1, 0)));
void alogf(char const *const fmt, ...) __attribute__((format(printf, 1, 2)));

int uripathcmp(char const *const literal, char const *const input, char const **const qs);

