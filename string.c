#include "string.h"
#include <stdio.h>

char *strncpyz(char *dest, const char *src, size_t n)
{
	strncpy(dest, src, n - 1);
	dest[n - 1] = '\0';
	return dest;
}

char *strencpyz(char *dest, const char *src, size_t n, const char *elipsis)
{
	size_t offset = 0, srclen = strlen(src);
	if (srclen >= n) offset = srclen - n + 1;
	strncpy(dest, src + offset, n - 1);
	if (offset)
		for (size_t e_i = 0; elipsis[e_i] && e_i < n; ++e_i)
			dest[e_i] = elipsis[e_i];
	dest[n - 1] = '\0';
	return dest;
}

unsigned strtosi(char *str, size_t n, size_t num, unsigned fnum)
{
	static const char *sibase = " KMGTPE";
	const char *si = sibase;
	size_t d = num;
	unsigned rem = 0;
	while (d >= 1024) {
		rem = d % 1024;
		d /= 1024;
		++si;
	}
	if (!fnum || si == sibase)
		snprintf(str, n, "%u%cB", (unsigned)d, *si);
	else {
		char sbuf[32];
		snprintf(sbuf, sizeof sbuf, "%%u.%%0%du%%cB", fnum);
		snprintf(str, n, sbuf, (unsigned)d, (unsigned)(rem / 1.024f), *si);
	}
	return (unsigned)(si - sibase);
}
