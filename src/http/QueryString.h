// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdbool.h>
#include <string.h>

void QSValuesParse(char const *const qs, char *values[], char const *const fields[], size_t const count);
void QSValuesCleanup(char **const values, size_t const count);

char *QSUnescape(char const *const s, size_t const slen, bool const decodeSpaces);
char *QSEscape(char const *const s, size_t const slen, bool const encodeSpaces);

