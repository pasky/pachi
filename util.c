
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include "util.h"


#ifdef _WIN32

const char *
strcasestr(const char *haystack, const char *needle)
{
	for (const char *p = haystack; *p; p++) {
		for (int ni = 0; needle[ni]; ni++) {
			if (!p[ni])
				return NULL;
			if (toupper(p[ni]) != toupper(needle[ni]))
				goto more_hay;
		}
		return p;
more_hay:;
	}
	return NULL;
}

#endif /* _WIN32 */


/**************************************************************************************************/

int
str_prefix(char *prefix, char *str)
{
	return (!strncmp(prefix, str, strlen(prefix)));
}


/**************************************************************************************************/
/* String buffer */

strbuf_t *
strbuf_init(strbuf_t *buf, char *buffer, int size)
{
	buf->str = buf->cur = buffer;
	buf->remaining = size;
	return buf;
}

strbuf_t *
strbuf_init_alloc(strbuf_t *buf, int size)
{
	char *str = malloc(size);
	return strbuf_init(buf, str, size);
}

strbuf_t *
new_strbuf(int size)
{
	strbuf_t *buf = malloc(sizeof(strbuf_t));
	char *str = malloc(size);
	strbuf_init(buf, str, size);
	return buf;	
}


int
strbuf_printf(strbuf_t *buf, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	int n = vsnprintf(buf->cur, buf->remaining, format, ap);
	assert(n >= 0);
	
	if (n >= buf->remaining) {
		fprintf(stderr, "strbuf_printf(): not enough space, aborting !\n");
		abort();
	}
	
	buf->cur += n;
	buf->remaining -= n;

	va_end(ap);
	return n;
}
