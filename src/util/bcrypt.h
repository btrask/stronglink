#include <stdbool.h>

#define GENSALT_INPUT_SIZE 16

bool checkpass(char const *const pass, char const *const hash);
char *hashpass(char const *const pass);

