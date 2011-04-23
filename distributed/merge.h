#ifndef PACHI_DISTRIBUTED_MERGE_H
#define PACHI_DISTRIBUTED_MERGE_H

#include "distributed/protocol.h"

void merge_print_stats(int total_hnodes);
void merge_init(struct slave_state *sstate, int shared_nodes, int stats_hbits, int max_slaves);

#endif
