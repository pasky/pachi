#pragma once

#include <stdint.h> /* for size_t */

typedef struct {} regex_t;
typedef struct {} regmatch_t;

#define REG_ESPACE (-((1 << 9) + 42))

static inline int regcomp(regex_t *preg, const char *regex, int cflags)
{
    return REG_ESPACE;
}

static inline int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags)
{
    return REG_ESPACE;
}

#include <string.h>
static inline size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size)
{
    if (errbuf_size)
    {
        errbuf[errbuf_size-1] = '\0';
        strncpy(errbuf, "no regex support on this platform", errbuf_size-1);
    }
    return errbuf_size;
}

static inline void regfree(regex_t *preg) {}

#   define REG_EXTENDED (-1)
#   define REG_ICASE (-1)
