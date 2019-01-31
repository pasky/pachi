#ifndef PACHI_ENGINES_JOSEKIPLAY_H
#define PACHI_ENGINES_JOSEKIPLAY_H

void josekiplay_set_jdict(struct engine *e, struct joseki_dict *jdict);
void engine_josekiplay_init(struct engine *e, char *arg, struct board *b);

#endif
