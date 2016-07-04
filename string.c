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

void streta(char *str, size_t n, struct timespec start, struct timespec now, size_t p_end, size_t p_now)
{
	// XXX use this
	(void)p_end;
	struct timespec tmp;
	char sbuf[32];
	// XXX potential overflow in struct timespec
	if (start.tv_sec > now.tv_sec) {
		tmp = start;
		start = now;
		now = tmp;
	}
	time_t dt_sec;
	long dt_msec;
	if (now.tv_nsec > start.tv_nsec) {
		dt_sec = now.tv_sec - start.tv_sec;
		dt_msec = (now.tv_nsec - start.tv_nsec) / 1000000L;
	} else {
		dt_sec = now.tv_sec - start.tv_sec - 1;
		dt_msec = (1000000000L + now.tv_nsec - start.tv_nsec) / 1000000L;
	}
	unsigned long dt = dt_sec * 1000 + dt_msec;
	float p_speed = dt ? p_now * 1000.0f / dt : p_now;
	strtosi(sbuf, sizeof sbuf, p_speed, 3);
	snprintf(str, n, "@%s/s in %lums", sbuf, dt);
}
