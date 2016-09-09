#ifndef PACHI_UTIL_H
#define PACHI_UTIL_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>


#define MIN(a, b) ((a) < (b) ? (a) : (b));
#define MAX(a, b) ((a) > (b) ? (a) : (b));

/* Returns true if @str starts with @prefix */
int str_prefix(char *prefix, char *str);

/* Terminate with error msg */
void die(const char *format, ...)  __attribute__ ((noreturn));

/* Terminate after system call failure (calls perror()) */
void fail(char *msg) __attribute__ ((noreturn));


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
#define __sync_fetch_and_add(ap, b) InterlockedExchangeAdd((LONG volatile *) (ap), (b));
#define __sync_fetch_and_sub(ap, b) InterlockedExchangeAdd((LONG volatile *) (ap), -((LONG)b));

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


/**************************************************************************************************/

/* Data-loading definitions */
static inline const char *
get_data_file(const char *filename)
{
  struct stat s;

  if (stat(filename, &s) == 0) {
    return filename;
  }

#ifdef DATA_DIR
  char *data_dir_filename = malloc(strlen(DATA_DIR) + 1 + strlen(filename) + 1);
  sprintf(data_dir_filename, "%s/%s", DATA_DIR, filename);
  if (stat(data_dir_filename, &s) == 0) {
    return (const char *)data_dir_filename;
  }
#endif

  return NULL;
}


#endif
