#ifndef PACHI_DISTRIBUTED_MERGE_H
#define PACHI_DISTRIBUTED_MERGE_H

#include "distributed/protocol.h"

void merge_print_stats(int total_hnodes);
void merge_init(slave_state_t *sstate, int shared_nodes, int stats_hbits, int max_slaves);

#endif
