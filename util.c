#define DEBUG 1

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "pachi.h"
#include "debug.h"
#include "util.h"

#ifdef _WIN32
#include <shlwapi.h>   /* PathFindOnPathA() */
#include <windows.h>
#endif

void
win_set_pachi_cwd(char *pachi)
{
#ifdef _WIN32
	char *pachi_path = strdup(pachi);
	const char *pachi_dir = dirname(pachi_path);
	if (chdir(pachi_dir) != 0)  die("Couldn't cd to %s", pachi_dir);
	free(pachi_path);
#endif
}

int
get_nprocessors()
{
#ifdef _WIN32
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwNumberOfProcessors;
#else
	return sysconf(_SC_NPROCESSORS_ONLN);
#endif	
}

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

	/* Try exe's directory */
	{
		strbuf_t *buf = strbuf_init(&strbuf, buffer, size);
		sbprintf(buf, "%s/%s", pachi_dir, filename);
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

int
str_prefix(char *prefix, char *str)
{
	return (!strncmp(prefix, str, strlen(prefix)));
}

bool
valid_number(char *str)
{
	char c = *str++;
	if (c != '-' && !isdigit(c))
		return false;
	
	while (isdigit(*str))
		str++;
	return (!*str || isspace(*str));
}

bool
valid_float(char *str)
{
	char c = *str++;
	if (c != '-' && c != '.' && !isdigit(c))
		return false;

	while (isdigit(*str) || *str == '.')
		str++;
	return (!*str || isspace(*str));
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

/* Remove trailing '\n'		(or "\r\n" on windows) */
void
chomp(char *line)
{
	int n = strlen(line);
	if (n && line[n - 1] == '\n')		// remove '\n'
		line[n - 1] = 0;
	if (n >= 2 && line[n - 2] == '\r')	// remove '\r'  (windows)
		line[n - 2] = 0;
}


/**************************************************************************************************/
/* String buffer */

strbuf_t *
strbuf_init(strbuf_t *buf, char *buffer, int size)
{
	buf->str = buf->cur = buffer;
	buf->str[0] = 0;
	buf->remaining = size;
	return buf;
}

strbuf_t *
strbuf_init_alloc(strbuf_t *buf, int size)
{
	char *str = (char*)malloc(size);
	return strbuf_init(buf, str, size);
}

strbuf_t *
new_strbuf(int size)
{
	strbuf_t *buf = (strbuf_t*)malloc(sizeof(strbuf_t));
	char *str = (char*)malloc(size);
	strbuf_init(buf, str, size);
	return buf;	
}


int
strbuf_vprintf(strbuf_t *buf, const char *format, va_list ap)
{
	int n = vsnprintf(buf->cur, buf->remaining, format, ap);
	assert(n >= 0);
	
	if (n >= buf->remaining) {
		fprintf(stderr, "strbuf_printf(): not enough space, aborting !\n");
		abort();
	}
	
	buf->cur += n;
	buf->remaining -= n;

	return n;
}

int
strbuf_printf(strbuf_t *buf, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int n = strbuf_vprintf(buf, format, ap);
	va_end(ap);
	return n;
}


/**************************************************************************************************/
/* Windows */

#ifdef _WIN32


void pachi_sleep(int seconds)
{
	Sleep((seconds) * 1000);
}

/* Windows MessageBox() */
void
pachi_popup(const char *msg)
{
	MessageBox(0, msg, "Pachi", MB_OK);
}

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

/* Like perror() for windows API calls */
void
win_perror(char *function)
{
	long error = GetLastError();  
	char *msg = NULL;

	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		      FORMAT_MESSAGE_FROM_SYSTEM |
		      FORMAT_MESSAGE_IGNORE_INSERTS,
		      NULL,
		      error,
		      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		      (char*) &msg,
		      0, NULL);
	
	fprintf(stderr, "%s failed: %s", function, msg);

	// Normally msg is already newline terminated.
	if (msg[0] && msg[strlen(msg) - 1] != '\n')
		fprintf(stderr, "\r\n");
}


#endif /* _WIN32 */
