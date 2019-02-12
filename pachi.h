#ifndef PACHI_PACHI_H
#define PACHI_PACHI_H

#include <stdbool.h>

struct board;
struct engine;

/* Free globals */
void pachi_done();

/* Init engines */
void pachi_engine_init(struct engine *e, int id, char *e_arg, struct board *b);


/* Pachi binary */
extern char *pachi_exe;

/* Ruleset from cmdline, if present. */
extern char *forced_ruleset;

/* Don't pass first ? Needed on kgs or cleanup phase can be abused. */
extern bool nopassfirst;

#endif
