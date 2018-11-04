#ifndef PACHI_ENGINES_JOSEKIPLAY_H
#define PACHI_ENGINES_JOSEKIPLAY_H

void josekiplay_set_jdict(struct engine *e, struct joseki_dict *jdict);
struct engine *engine_josekiplay_init(char *arg, struct board *b);

#endif
