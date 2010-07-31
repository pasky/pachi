#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "move.h"
#include "joseki/base.h"


struct joseki joseki_pats[1 << joseki_hash_bits];
