#define DEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "board.h"
#include "debug.h"
#include "engine.h"
#include "engines/external.h"

/* Internal engine state. */
typedef struct {
	char *cmd;
    
	int pid;
	int input;	/* engine input  - ie for writing */
	int output;	/* engine output - ie for reading */
        FILE *output_stream;
	bool started;
} external_engine_t;

int
external_engine_send_cmd(engine_t *e, char *cmd, char **reply, char **error)
{
	static char buf[1024];
	char buf2[10];
	external_engine_t *pp = (external_engine_t*)e->data;
	
	strncpy(buf, cmd, sizeof(buf));
	int n = strlen(buf);    
	if (n && buf[n-1] == '\n')		/* remove newline if present */
		buf[--n] = 0;
	
	if (DEBUGL(3))  fprintf(stderr, "external engine: sending '%s'\n", buf);
	
	buf[n] = '\n';  buf[n+1] = 0;	/* add newline */    
	int r = write(pp->input, buf, strlen(buf));
	if (r != (int)strlen(buf))  fail("pipe write");
	
	if (!fgets(buf, sizeof(buf), pp->output_stream))  fail("fgets");
	n = strlen(buf);  assert(buf[n-1] == '\n');  buf[n-1] = 0;	/* chomp newline */
	int status = (buf[0] == '=');
	if (buf[0] == '=') {  /* successful */
		if (reply)  *reply = buf + 2;
	} else if (buf[0] == '?') {
		if (error)  *error = buf + 2;
	} else
		die("external engine: malformed answer: '%s'\n", buf);
	
	if (!fgets(buf2, sizeof(buf2), pp->output_stream))  fail("fgets");
	assert(!strcmp(buf2, "\n"));
	
	return status;
}

bool
external_engine_started(engine_t *e)
{
	external_engine_t *pp = (external_engine_t*)e->data;	
	return pp->started;
}
	
static bool
start_external_engine(engine_t *e, board_t *b)
{
	external_engine_t *pp = (external_engine_t*)e->data;
	assert(pp->cmd);
	if (DEBUGL(3))  fprintf(stderr, "external engine cmd: '%s'\n", pp->cmd);

	int pipe1[2], pipe2[2];
	if (pipe(pipe1))  {  perror("pipe");  return false;  }
	if (pipe(pipe2))  {  perror("pipe");  return false;  }
	pp->input = pipe1[1];
	pp->output = pipe2[0];
	
	pp->output_stream = fdopen(pp->output, "r");
	if (!pp->output_stream)  {  perror("fdopen");  return false;  }

	int pid = fork();
	if (pid == -1)  {  perror("fork");  return false;  }

	if (!pid) {  /* child, never return */
		close(pipe1[1]);
		close(pipe2[0]);
		if (dup2(pipe1[0], 0) == -1)  fail("dup2");
		if (dup2(pipe2[1], 1) == -1)  fail("dup2");
		
		/* standard error -> /dev/null */
		int err = open("/dev/null", O_WRONLY);
		if (err == -1)           fail("open /dev/null");
		if (dup2(err, 2) == -1)  fail("dup2");
		
		execl("/bin/sh", "sh", "-c", pp->cmd, (char *) NULL);
		fail("execl");
	}
	
	/* parent */
	close(pipe1[0]);
	close(pipe2[1]);
	
	pp->pid = pid;
	
	usleep(100 * 1000);
	int status;
	int r = waitpid(pid, &status, WNOHANG);
	if (r == pid) {
		fprintf(stderr, "external engine: couldn't run '%s'\n", pp->cmd);
		return false;
	}
	if (r == -1)  {  perror("waitpid");  return false;  }

	char *reply;
	if (external_engine_send_cmd(e, "name", &reply, NULL))
		if (DEBUGL(2))  fprintf(stderr, "External engine: %s ", reply);
	if (external_engine_send_cmd(e, "version", &reply, NULL))
		if (DEBUGL(2))  fprintf(stderr, "version %s\n", reply);

	return true;
}

static void
stop_external_engine(engine_t *e)
{
	external_engine_t *pp = (external_engine_t*)e->data;
	if (!pp->pid)  return;
	
	if (DEBUGL(2))  fprintf(stderr, "shutting down external engine ...\n");
	int r = external_engine_send_cmd(e, "quit", NULL, NULL);
	assert(r);
	fclose(pp->output_stream);
	close(pp->input);
	
	int status;
	waitpid(pp->pid, &status, 0);
	pp->pid = 0;
}

static coord_t
external_engine_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	assert(0);
}

static void
external_done(engine_t *e)
{
	external_engine_t *pp = (external_engine_t*)e->data;

	if (pp->started) {
		stop_external_engine(e);
		pp->started = false;
	}

	if (pp->cmd) {
		free(pp->cmd);
		pp->cmd = NULL;
	}
}


#define NEED_RESET   ENGINE_SETOPTION_NEED_RESET
#define option_error engine_setoption_error

static bool
external_engine_setoption(engine_t *e, board_t *b, const char *optname, char *optval,
			  char **err, bool setup, bool *reset)
{
	static_strbuf(ebuf, 256);
	external_engine_t *pp = (external_engine_t*)e->data;

	if (!strcasecmp(optname, "cmd") && optval)
		pp->cmd = strdup(optval);
	else
		option_error("external engine: Invalid engine argument %s or missing value\n", optname);

	return true;
}

external_engine_t *
external_engine_state_init(engine_t *e, board_t *b)
{
	options_t *options = &e->options;
	external_engine_t *pp = calloc2(1, external_engine_t);
	e->data = pp;
	
	/* Process engine options. */
	for (int i = 0; i < options->n; i++) {
		char *err;
		if (!engine_setoption(e, b, &options->o[i], &err, true, NULL))
			die("%s", err);
	}

	return pp;
}

void
external_engine_init(engine_t *e, board_t *b)
{
	e->name = "External";
	e->comment = "";
	e->genmove = external_engine_genmove;
	e->setoption = external_engine_setoption;
	e->done = external_done;
	external_engine_state_init(e, b);

	external_engine_t *pp = (external_engine_t*)e->data;	
	pp->started = start_external_engine(e, b);
}
