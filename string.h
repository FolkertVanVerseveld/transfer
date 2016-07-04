#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <string.h>
#include <time.h>

/* equivalent to strncpy but quaranteed to be \0 terminated */
char *strncpyz(char *dest, const char *src, size_t n);
/* adjust src to make sure last characters fit in dest and
prefix dest with elipsis if src does not fit in dest */
char *strencpyz(char *dest, const char *src, size_t n, const char *elipsis);

unsigned strtosi(char *str, size_t n, size_t num, unsigned fnum);

void streta(char *str, size_t n, struct timespec start, struct timespec now, size_t p_end, size_t p_now);

#endif
