#include "common.h"

#define GENSALT_INPUT_SIZE 16

int passcmp(volatile strarg_t const a, volatile strarg_t const b);
bool checkpass(strarg_t const pass, strarg_t const hash);
str_t *hashpass(strarg_t const pass);

