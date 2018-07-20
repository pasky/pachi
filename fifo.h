#ifndef PACHI_FIFO_H
#define PACHI_FIFO_H

#ifdef PACHI_FIFO

void fifo_init();
int  fifo_task_queue();
void fifo_task_done(int ticket);

#else
#define fifo_init()
#endif /* FIFO */

#endif /* PACHI_FIFO_H */
