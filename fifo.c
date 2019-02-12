#define DEBUG
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include "util.h"
#include "debug.h"
#include "fifo.h"

/* Fifo queue to coordinate multiple pachi instances so that only one runs at
 * a time. Having multiple multi-threaded pachis fight for cpu is not a good
 * idea. Either run each one single threaded or use this.
 *
 * Implemented using shared memory segment + simple robust mutex:
 * - dead-lock free, handles instances disappearing with the lock
 * - ordering not guaranteed but almost 100% fifo in practice
 *
 * If your system uses systemd beware !
 * systemd regularly cleans up what it thinks of as "stale" entries in
 * /dev/shm so if you run Pachi in the background as non-system user shared
 * memory will get broken in mysterious ways:
 *   https://superuser.com/questions/1117764/why-are-the-contents-of-dev-shm-is-being-removed-automatically
 * Edit /etc/systemd/logind.conf and uncomment
 *     RemoveIPC=n
 *
 * For reference:
 * - Real ticket-lock style implementation would give 100% guaranteed fifo
 *   order but not worth the extra complexity imo.
 * - Semaphore looks nice for this on the surface (just what we need, no need
 *   for shm) but doesn't handle processes disappearing with the token
 *   -> timeout and it's a mess...
 * - flock(): easy, may be ok for 2 tasks. Order completely unreliable 
 *   (not fifo at all) as soon as you have 3 instances or more.
 * - same for open(O_EXCL) */


/* Handle running pachi as different users ?
 * Anyone will be able to attach memory segment. */
#define PACHI_FIFO_ALLOW_MULTIPLE_USERS 1


typedef struct {
	pthread_mutex_t mutex;
} ticket_lock_t;

static void
ticket_init(ticket_lock_t *t)
{
	pthread_mutexattr_t mattr;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);
	
	pthread_mutex_init(&t->mutex, &mattr);	
}

/* Returns 0 if owner died */
static int
mutex_lock(pthread_mutex_t *mutex)
{
	if ((errno = pthread_mutex_lock(mutex)) == 0)
		return 1;  /* All good. */
	if (errno == EOWNERDEAD) {
		//fprintf(stderr, "fifo: recovering dead mutex ...\n");
		pthread_mutex_consistent(mutex);
		return 0;
	}
	fail("pthread_mutex_lock");
	return 0;
}

static void
mutex_unlock(pthread_mutex_t *mutex)
{
	errno = pthread_mutex_unlock(mutex);
	if (!errno)  return;  /* All good. */
	fail("pthread_mutex_unlock");
}

static int
ticket_lock(ticket_lock_t *ticket)
{
	if (!mutex_lock(&ticket->mutex))  /* Mutex owner died, recover... */
		if (DEBUGL(2)) fprintf(stderr, "fifo: kicking stale instance\n");
	return 0;
}

static void
ticket_unlock(ticket_lock_t *ticket, int me)
{
	mutex_unlock(&ticket->mutex);
}


/***************************************************************************************************/
/* Shared memory */

#define SHM_NAME    "pachi_fifo"
#define SHM_MAGIC   ((int)0xf1f0c0de)

typedef struct {
	unsigned int size;
	int	     magic;
	int          ready;
	int          timestamp;
	
	/* sched stuff */
	ticket_lock_t queue;
} sched_shm_t;

static unsigned int shm_size = sizeof(sched_shm_t);
static sched_shm_t *shm = 0;


/* Create new shared memory segment */
static void
create_shm()
{
#ifdef PACHI_FIFO_ALLOW_MULTIPLE_USERS
	umask(0);  /* Any user may attach shared memory segment. */
	int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_TRUNC, 0666);
	umask(022);
#else
	int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_TRUNC, 0644);
#endif
	assert(fd != -1);
	int r = ftruncate(fd, shm_size);  assert(r == 0);
	void *pt = mmap(0, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	assert(pt != MAP_FAILED);
       
	shm = pt;
	memset(shm, 0, sizeof(*shm));
	shm->size = shm_size;
	shm->magic = SHM_MAGIC;
	shm->ready = 0;
	shm->timestamp = time(NULL);
       
	ticket_init(&shm->queue);

	shm->ready = 1;
	if (DEBUGL(2)) fprintf(stderr, "Fifo: created shared memory, id: %i\n", shm->timestamp);
}

/* Attach existing shared memory segment */
static int
attach_shm()
{
	int fd = shm_open(SHM_NAME, O_RDWR, 0);
	if (fd == -1)  return 0;  /* Doesn't exist yet... */

	/* Sanity check, make sure it has the right size ... */
	struct stat st;
	if (stat("/dev/shm/" SHM_NAME, &st) != 0)  fail("/dev/shm/" SHM_NAME);
	assert(st.st_size == sizeof(sched_shm_t));
	
	shm = mmap(0, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shm == MAP_FAILED)  fail("mmap");	
	assert(shm->magic == SHM_MAGIC);
	assert(shm->size == shm_size);
	assert(shm->ready);

	if (DEBUGL(2)) fprintf(stderr, "Fifo: mapped shared memory, id: %i\n", shm->timestamp);
	return 1;
}


/***************************************************************************************************/

void
fifo_init(void)
{
	if (!attach_shm())
		create_shm();
}

int
fifo_task_queue(void)
{
	return ticket_lock(&shm->queue);
}

void
fifo_task_done(int ticket)
{
	ticket_unlock(&shm->queue, ticket);
}

