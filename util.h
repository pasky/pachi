#ifndef PACHI_UTIL_H
#define PACHI_UTIL_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#undef MIN
#undef MAX

#define MIN(a, b) ((a) < (b) ? (a) : (b));
#define MAX(a, b) ((a) > (b) ? (a) : (b));

/* Returns true if @str starts with @prefix */
int str_prefix(char *prefix, char *str);

/* Warn user (popup on windows) */
void warning(const char *format, ...);

/* Warning + terminate process */
void die(const char *format, ...)  __attribute__ ((noreturn));

/* Terminate after system call failure (similar to perror()) */
void fail(char *msg) __attribute__ ((noreturn));

int file_exists(const char *name);

/* windows: cd to pachi directory to avoid cwd issues. */
void win_set_pachi_cwd(char *pachi);

/* Get number of processors. */
int get_nprocessors();


/**************************************************************************************************/
/* Data files */

/* Lookup data file in the following places:
 * 1) Current directory
 * 2) DATA_DIR environment variable / compile time default
 * Copies first match to @buffer (if no match @filename is still copied). */
#define get_data_file(buffer, filename)    get_data_file_(buffer, sizeof(buffer), filename)
void get_data_file_(char buffer[], int size, const char *filename);

/* get_data_file() + fopen() */
FILE *fopen_data_file(const char *filename, const char *mode);


/**************************************************************************************************/
/* Portability definitions. */

#ifdef _WIN32

/*
 * sometimes we use winsock and like to avoid a warning to include
 * windows.h only after winsock2.h
 */
#include <winsock2.h>
#include <windows.h>
#include <ctype.h>

#define sleep(seconds) Sleep((seconds) * 1000)

/* No line buffering on windows, set to unbuffered. */
#define setlinebuf(file)   setvbuf(file, NULL, _IONBF, 0)

/* Windows MessageBox() */
#define popup(msg)	MessageBox(0, msg, "Pachi", MB_OK);

/* MinGW gcc, no function prototype for built-in function stpcpy() */ 
char *stpcpy (char *dest, const char *src);

const char *strcasestr(const char *haystack, const char *needle);

#endif /* _WIN32 */


/**************************************************************************************************/
/* Misc. definitions. */

/* Use make DOUBLE_FLOATING=1 in large configurations with counts > 1M
 * where 24 bits of floating_t mantissa become insufficient. */
#ifdef DOUBLE_FLOATING
#  define floating_t double
#  define PRIfloating "%lf"
#else
#  define floating_t float
#  define PRIfloating "%f"
#endif

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect((x), 0)

static inline void *
checked_malloc(size_t size, char *filename, unsigned int line, const char *func)
{
	void *p = malloc(size);
	if (!p)
		die("%s:%u: %s: OUT OF MEMORY malloc(%u)\n",
		    filename, line, func, (unsigned) size);
	return p;
}

static inline void *
checked_calloc(size_t nmemb, size_t size, const char *filename, unsigned int line, const char *func)
{
	void *p = calloc(nmemb, size);
	if (!p)
		die("%s:%u: %s: OUT OF MEMORY calloc(%u, %u)\n",
		    filename, line, func, (unsigned) nmemb, (unsigned) size);
	return p;
}

#define malloc2(size)        checked_malloc((size), __FILE__, __LINE__, __func__)
#define calloc2(nmemb, size) checked_calloc((nmemb), (size), __FILE__, __LINE__, __func__)

#define checked_write(fd, pt, size)	(assert(write((fd), (pt), (size)) == (size)))
#define checked_fread(pt, size, n, f)   (assert(fread((pt), (size), (n), (f)) == (n)))


/**************************************************************************************************/
/* Simple string buffer to store output
 * Initial size must be enough to store all output or program will abort. 
 * String and structure can be allocated using different means, see below. */

typedef struct
{
	int remaining;
	char *str;       /* whole output */
	char *cur;
} strbuf_t;

/* Initialize passed string buffer. Returns buf. */
strbuf_t *strbuf_init(strbuf_t *buf, char *buffer, int size);

/* Initialize passed string buffer. Returns buf. 
 * Internal string is malloc()ed and must be freed afterwards. */
strbuf_t *strbuf_init_alloc(strbuf_t *buf, int size);

/* Create a new string buffer and use a malloc()ed string internally.
 * Both must be free()ed afterwards. */
strbuf_t *new_strbuf(int size);


/* String buffer version of printf():
 * Use sbprintf(buf, format, ...) to accumulate output. */
int strbuf_printf(strbuf_t *buf, const char *format, ...);
#define sbprintf strbuf_printf


#endif
