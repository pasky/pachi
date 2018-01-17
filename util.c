
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include "util.h"

int
file_exists(const char *name)
{
	struct stat st;
	return (stat(name, &st) == 0);
}

#ifndef DATA_DIR
#define DATA_DIR "/usr/local/share/pachi"
#endif

void
get_data_file_(char buffer[], int size, const char *filename)
{
	struct stat st;
	strbuf_t strbuf;
	
	/* Try current directory first. */
	if (stat(filename, &st) == 0) {
		strbuf_t *buf = strbuf_init(&strbuf, buffer, size);
		sbprintf(buf, "%s", filename);
		return;
	}
	
	/* Try DATA_DIR environment variable / default */
	const char *data_dir = (getenv("DATA_DIR") ? getenv("DATA_DIR") : DATA_DIR);
	{
		strbuf_t *buf = strbuf_init(&strbuf, buffer, size);
		sbprintf(buf, "%s/%s", data_dir, filename);
		if (stat(buf->str, &st) == 0)
			return;
	}

	/* Not found, copy filename. */
	strbuf_t *buf = strbuf_init(&strbuf, buffer, size);
	sbprintf(buf, "%s", filename);
}

FILE *
fopen_data_file(const char *filename, const char *mode)
{
	FILE *f = fopen(filename, mode);
	if (f)  return f;

	char buf[256];
	get_data_file(buf, filename);
	return fopen(buf, mode);
}

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


int
str_prefix(char *prefix, char *str)
{
	return (!strncmp(prefix, str, strlen(prefix)));
}


static void
vwarning(const char *format, va_list ap)
{
	vfprintf(stderr, format, ap);

#ifdef _WIN32    /* Display popup */	
	char buf[2048];
	vsnprintf(buf, sizeof(buf), format, ap);
	popup(buf);
#endif
}

void
warning(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vwarning(format, ap);
	va_end(ap);
}

void
die(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vwarning(format, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void
fail(char *msg)
{
	warning("%s: %s\n", msg, strerror(errno));
	exit(42);
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
