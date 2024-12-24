#ifndef PACHI_PACHI_H
#define PACHI_PACHI_H

#include <stdbool.h>
#include "gtp.h"

struct board;
struct engine;

/* Global options */
typedef struct {
	bool    kgs;
	bool    nopassfirst;		/* don't pass first when playing chinese */
	bool    guess_unclear_groups;   /* ok to guess unclear dead groups ? (smart pass) */
	bool    tunit_fatal;            /* abort on failed t-unit test */
	enum rules forced_rules;
} pachi_options_t;


/* Free globals */
void pachi_done();

/* Get global options */
const pachi_options_t *pachi_options();

/* Pachi binary */
extern char *pachi_exe;


#endif
