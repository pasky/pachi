#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "board.h"
#include "engine.h"
#include "montecarlo/montecarlo.h"
#include "random/random.h"
#include "gtp.h"

int main(int argc, char *argv[])
{
	struct board *b = board_init();
	//struct engine *e = engine_random_init();
	struct engine *e = engine_montecarlo_init();
	char buf[256];
	while (fgets(buf, 256, stdin)) {
		//fprintf(stderr, "IN: %s", buf);
		gtp_parse(b, e, buf);
		//board_print(b, stderr);
	}
	return 0;
}
