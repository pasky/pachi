#ifndef PACHI_UTIL_H
#define PACHI_UTIL_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#undef MIN
#undef MAX

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define swap(x, y)  do { typeof(x) __tmp;  __tmp = (x);  (x) = (y);  (y) = __tmp;  } while(0)

#ifdef __cplusplus
#define typeof decltype
#define restrict __restrict__
#endif


/* Returns true if @str starts with @prefix */
int str_prefix(char *prefix, char *str);

/* Warn user (popup on windows) */
void warning(const char *format, ...)
	__attribute__ ((format (printf, 1, 2)));

/* Warning + terminate process */
void die(const char *format, ...)
	__attribute__ ((noreturn))
	__attribute__ ((format (printf, 1, 2)));

/* Terminate after system call failure (similar to perror()) */
void fail(char *msg)
	__attribute__ ((noreturn));

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
 * 3) exe's directory
 * Copies first match to @buffer (if no match @filename is still copied). */
#define get_data_file(buffer, filename)    get_data_file_(buffer, sizeof(buffer), filename)
void get_data_file_(char buffer[], int size, const char *filename);

/* get_data_file() + fopen() */
FILE *fopen_data_file(const char *filename, const char *mode);


/**************************************************************************************************/
/* Portability definitions. */

#ifdef _WIN32

#define sleep(seconds)  pachi_sleep(seconds)
void pachi_sleep(int seconds);

/* No line buffering on windows, set to unbuffered. */
#define setlinebuf(file)   setvbuf(file, NULL, _IONBF, 0)

/* Windows MessageBox() */
#define popup(msg)	pachi_popup(msg)
void pachi_popup(const char *msg);

/* MinGW gcc, no function prototype for built-in function stpcpy() */ 
char *stpcpy (char *dest, const char *src);

const char *strcasestr(const char *haystack, const char *needle);

/* Like perror() for windows API calls */
void win_perror(char *function);

#endif /* _WIN32 */


/**************************************************************************************************/
/* const-safe versions of some string functions */

#ifdef strchr
#undef strchr
#endif

/* https://fanf.livejournal.com/144696.html */
#define strchr(str, chr)  ((__typeof__(&(str)[0])) strchr((str), (chr)))
#define strrchr(str, chr) ((__typeof__(&(str)[0])) strrchr((str), (chr)))
#define index(str, chr)   ((__typeof__(&(str)[0])) index((str), (chr)))
#define rindex(str, chr)  ((__typeof__(&(str)[0])) rindex((str), (chr)))


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
	if (!p)  die("%s:%u: %s: OUT OF MEMORY calloc(%u, %u)\n",
		     filename, line, func, (unsigned) nmemb, (unsigned) size);
	return p;
}

static inline void *
checked_realloc(void *ptr, size_t size, char *filename, unsigned int line, const char *func)
{
	void *p = realloc(ptr, size);
	if (!p)
		die("%s:%u: %s: OUT OF MEMORY realloc(%u)\n",
		    filename, line, func, (unsigned) size);
	return p;
}

/* casts: make c++ happy */
#define cmalloc(size)        checked_malloc((size), __FILE__, __LINE__, __func__)
#define ccalloc(nmemb, size) checked_calloc((nmemb), (size), __FILE__, __LINE__, __func__)
#define malloc2(type)        ((type*)cmalloc(sizeof(type)))
#define calloc2(nmemb, type) ((type*)ccalloc(nmemb, sizeof(type)))
#define crealloc(ptr, size)  checked_realloc((ptr), (size), __FILE__, __LINE__, __func__)

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

/* Create string buffer for use within current function (stack-allocated). */
#define strbuf(buf, size)  \
	char buffer_[(size)];  strbuf_t strbuf_; \
	strbuf_t *buf = strbuf_init(&strbuf_, buffer_, sizeof(buffer_));

/* Create static string buffer: can return buf->str (but not buf). */
#define static_strbuf(buf, size)  \
	static char buffer_[(size)];  strbuf_t strbuf_; \
	strbuf_t *buf = strbuf_init(&strbuf_, buffer_, sizeof(buffer_));

/* String buffer version of printf():
 * Use sbprintf(buf, format, ...) to accumulate output. */
int strbuf_printf(strbuf_t *buf, const char *format, ...)
	__attribute__ ((format (printf, 2, 3)));
int strbuf_vprintf(strbuf_t *buf, const char *format, va_list ap);

#define sbprintf strbuf_printf


/**************************************************************************************************/

/* like mkstemp() but takes care of creating file in system's temp directory 
 * on return @pattern contains the full path to the file. */
int pachi_mkstemp(char *pattern, size_t max_size);

/* Remove trailing '\n'		(or "\r\n" on windows) */
void chomp(char *line);


#endif
