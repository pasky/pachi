#ifndef PACHI_PACHI_H
#define PACHI_PACHI_H

/* Ruleset from cmdline, if present. */
extern char *forced_ruleset;

/* Don't pass first ? Needed on kgs or cleanup phase can be abused. */
extern bool nopassfirst;

#endif
