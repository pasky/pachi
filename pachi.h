#ifndef PACHI_PACHI_H
#define PACHI_PACHI_H

#include <stdbool.h>
#include "gtp.h"

struct board;
struct engine;

/* Free globals */
void pachi_done();

/* Init engines */
void pachi_engine_init(struct engine *e, int id, struct board *b);

/* Pachi binary */
extern char *pachi_exe;

/* Ruleset from cmdline, if present. */
extern char *forced_ruleset;

/* Don't pass first ? Needed when playing chinese rules on kgs or cleanup phase can be abused. */
bool pachi_nopassfirst(struct board *b);


#endif
