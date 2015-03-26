#include <stdbool.h>

#define GENSALT_INPUT_SIZE 16

int passcmp(volatile char const *const a, volatile char const *const b);
bool checkpass(char const *const pass, char const *const hash);
char *hashpass(char const *const pass);

