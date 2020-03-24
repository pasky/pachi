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
#include <shlwapi.h>
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
		char *exe = strdup(pachi_exe);
		char *exe_dir = dirname(exe);
		sbprintf(buf, "%s/%s", exe_dir, filename);
		free(exe);
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

/* like mkstemp() but takes care of creating file in system's temp directory.
 * on return @pattern contains the full path to the file. */
int
pachi_mkstemp(char *pattern, size_t max_size)
{
#ifdef _WIN32
	char *    dir = getenv("TEMP");
	if (!dir) dir = getenv("TMP");
	if (!dir) die("couldn't find temp directory\n");
#else
	char *dir = "/tmp";
#endif

	size_t res_len = strlen(dir) + strlen(pattern) + 1;
	assert(max_size >= res_len + 1);
	assert(max_size >  res_len + 1 + strlen(pattern) + 1);  /* copy */

	char *tmp = pattern + res_len + 1;
	strcpy(tmp, pattern);
	
	size_t r = snprintf(pattern, max_size, "%s/%s", dir, tmp);	assert(r == res_len);
	return mkstemp(pattern);
}


char *gnugo_exe = NULL;

bool
check_gnugo()
{
#ifdef _WIN32
  
	static char path[MAX_PATH] = "gnugo.exe";
	
	if (file_exists("gnugo.exe"))     {  gnugo_exe = "gnugo";  return true;  }
	if (file_exists("gnugo.bat"))     {  gnugo_exe = "gnugo";  return true;  }
	if (PathFindOnPathA(path, NULL))  {  gnugo_exe = "gnugo";  return true;  }

#else
  
	char *cmds[] = { "./gnugo", "gnugo", "/usr/games/gnugo", NULL };
	char cmd[256];

	for (int i = 0; cmds[i]; i++) {
		snprintf(cmd, sizeof(cmd), "%s -h >/dev/null 2>&1", cmds[i]);
		if (system(cmd) == 0) {
			gnugo_exe = cmds[i];
			return true;
		}
	}
	
#endif
	return false;	
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
