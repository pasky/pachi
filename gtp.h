#ifndef ZZGO_GTP_H
#define ZZGO_GTP_H

struct board;
struct engine;

void gtp_parse(struct board *b, struct engine *e, char *buf);

#endif
