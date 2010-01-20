#ifndef ZZGO_GTP_H
#define ZZGO_GTP_H

struct board;
struct engine;
struct time_info;

void gtp_parse(struct board *b, struct engine *e, struct time_info *ti, char *buf);

#endif
