#define DEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "board.h"
#include "debug.h"
#include "util.h"
#include "engine.h"
#include "engines/external.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#endif


/* Internal engine state. */
typedef struct {
	char *cmd;

#ifdef _WIN32
 	HANDLE phandle;		/* windows has both process handles and pids... */
#endif
	int pid;
	int input;		/* engine input  - ie for writing */
	int output;		/* engine output - ie for reading */
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
	chomp(buf);	/* remove newline / carriage return */
	
	int status = (buf[0] == '=');
	if (buf[0] == '=') {				/* success */
		if (reply)  *reply = buf + 2;
	} else if (buf[0] == '?') {			/* error */
		if (error)  *error = buf + 2;
	} else
		die("external engine: malformed answer: '%s'\n", buf);

	// XXX we only handle single-line answers here.
	
	if (!fgets(buf2, sizeof(buf2), pp->output_stream))  fail("fgets");

	/* Should end with a trailing newline */
	if (strcmp(buf2, "\n") && strcmp(buf2, "\r\n"))
		die("external engine: bad reply, aborting:\n'%s'\n", buf2);
	
	return status;
}

bool
external_engine_started(engine_t *e)
{
	external_engine_t *pp = (external_engine_t*)e->data;	
	return pp->started;
}


#ifdef _WIN32

/* Create child process.
 * Stores child pid in pp->pid, child HANDLE in pp->phandle */
static bool
create_child_process(external_engine_t *pp, HANDLE *pipe1, HANDLE *pipe2)
{
	PROCESS_INFORMATION proc_info;
	memset(&proc_info, 0, sizeof(proc_info));

	/* We want child's stderr as /dev/null */
	int devnull_fd = open("nul:", O_WRONLY);
	if (devnull_fd == -1) {  win_perror("open(\"nul:\")");  return false;  }
	HANDLE devnull = (HANDLE)_get_osfhandle(devnull_fd);
	assert(devnull != INVALID_HANDLE_VALUE);
	/* Mark inheritable so child can get it */
	if (!SetHandleInformation(devnull, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
		win_perror("SetHandleInformation(\"nul:\")");  return false;
	}
	
	STARTUPINFO start_info;
	memset(&start_info, 0, sizeof(start_info));
	start_info.cb = sizeof(start_info);
	start_info.hStdError  = devnull;	// stderr = /dev/null
	start_info.hStdOutput = pipe2[1];	// stdout = second pipe input
	start_info.hStdInput  = pipe1[0];       // stdin  = first pipe output
	start_info.dwFlags |= STARTF_USESTDHANDLES;

	char *cmd = pp->cmd;   // TODO convert to TCHAR to handle unicode...

	/* fork() on windows is fun stuff. */
	if (!CreateProcess(NULL,
			   cmd,			// cmdline
			   NULL,		// process security attributes
			   NULL,		// primary thread security attributes
			   TRUE,		// handles are inherited
			   0,			// creation flags
			   NULL,		// use parent's environment
			   NULL,		// use parent's current directory
			   &start_info,		// STARTUPINFO
			   &proc_info)) {	// PROCESS_INFORMATION
		win_perror("CreateProcess()");
		return false;
	}

	/* Close handles to the child process and its primary thread. */
	CloseHandle(proc_info.hProcess);
	CloseHandle(proc_info.hThread);

	/* Close stdin and stdout handles belonging to the child process. If they are not 
	 * closed explicitly there is no way to recgognize that the child process has ended. */
	CloseHandle(pipe2[1]);
	CloseHandle(pipe1[0]);

	/* Save child pid and HANDLE. */
	pp->pid = proc_info.dwProcessId;
	pp->phandle = OpenProcess(PROCESS_QUERY_INFORMATION, false, pp->pid);   // or QUERY_LIMITED_INFORMATION ?
	
	return (pp->phandle != INVALID_HANDLE_VALUE);
}

static bool
start_external_engine_windows(engine_t *e, board_t *b)
{
        external_engine_t *pp = (external_engine_t*)e->data;

	SECURITY_ATTRIBUTES attr;

	/* Set the bInheritHandle flag so pipe handles are inherited. */
	attr.nLength = sizeof(SECURITY_ATTRIBUTES);
	attr.bInheritHandle = true;
	attr.lpSecurityDescriptor = NULL;
	
        HANDLE pipe1[2] = { NULL, NULL };	// First pipe  = child's input
	HANDLE pipe2[2] = { NULL, NULL };	// Second pipe = child's output

	if (!CreatePipe(&pipe1[0], &pipe1[1], &attr, 0))	      {  win_perror("CreatePipe()");  return false;  }
	if (!CreatePipe(&pipe2[0], &pipe2[1], &attr, 0))	      {  win_perror("CreatePipe()");  return false;  }
	
	/* Ensure the write handle to the pipe for STDIN is not inherited
	 *            read  handle                 STDOUT                  */
	if (!SetHandleInformation(pipe1[1], HANDLE_FLAG_INHERIT, 0))  {  win_perror("SetHandleInformation()");  return false;  }
	if (!SetHandleInformation(pipe2[0], HANDLE_FLAG_INHERIT, 0))  {  win_perror("SetHandleInformation()");  return false;  }

	/* Convert HANDLEs to file descriptors */
	pp->input  = _open_osfhandle((intptr_t)pipe1[1], 0);
	pp->output = _open_osfhandle((intptr_t)pipe2[0], 0);

	pp->output_stream = fdopen(pp->output, "r");
	if (!pp->output_stream)  {  perror("fdopen");  return false;  }

	/* Create child process */
	if (!create_child_process(pp, pipe1, pipe2))
		return false;
	
	usleep(100 * 1000);

	long unsigned int exit_code = 0;
	if (GetExitCodeProcess(pp->phandle, &exit_code) && exit_code != STILL_ACTIVE)
		return false;		// Child dead already

	return true;
}

#else

static bool
start_external_engine_posix(engine_t *e, board_t *b)
{
	external_engine_t *pp = (external_engine_t*)e->data;
	
	int pipe1[2];		/* first pipe  = child's input  */
	int pipe2[2];		/* second pipe = child's output */
	if (pipe(pipe1))  {  perror("pipe");  return false;  }
	if (pipe(pipe2))  {  perror("pipe");  return false;  }
	pp->input  = pipe1[1];
	pp->output = pipe2[0];
	
	pp->output_stream = fdopen(pp->output, "r");
	if (!pp->output_stream)  {  perror("fdopen");  return false;  }

	int pid = fork();
	if (pid == -1)  {  perror("fork");  return false;  }

	if (!pid) {  /* This part run by the child, never returns. */
		close(pipe1[1]);	/* close fds belonging to parent */
		close(pipe2[0]);

		/* stdin = first pipe read side, stdout = second pipe write side */
		if (dup2(pipe1[0], 0) == -1)  fail("dup2");
		if (dup2(pipe2[1], 1) == -1)  fail("dup2");
		
		/* stderr = /dev/null */
		int err = open("/dev/null", O_WRONLY);
		if (err == -1)           fail("open /dev/null");
		if (dup2(err, 2) == -1)  fail("dup2");
		
		execl("/bin/sh", "sh", "-c", pp->cmd, (char *) NULL);
		fail("execl");
	}
	
	/* This part run by parent */
	close(pipe1[0]);	/* close stdin and stdout handles belonging to the child. */
	close(pipe2[1]);
	
	pp->pid = pid;
	
	usleep(100 * 1000);
	
	int status;
	int r = waitpid(pid, &status, WNOHANG);
	if (r == pid)			     return false;	// Child dead already.
	if (r == -1)  {  perror("waitpid");  return false;  }

	return true;
}

#endif /* _WIN32 */


static bool
start_external_engine(engine_t *e, board_t *b)
{
	external_engine_t *pp = (external_engine_t*)e->data;
	assert(pp->cmd);
	if (DEBUGL(3))  fprintf(stderr, "external engine cmd: '%s'\n", pp->cmd);
	
#ifdef _WIN32
	bool r = start_external_engine_windows(e, b);
#else
	bool r = start_external_engine_posix(e, b);
#endif

	if (!r) {
		fprintf(stderr, "external engine: couldn't run '%s'\n", pp->cmd);
		return r;
	}

	// Show engine name and version.
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
	
#if _WIN32
	WaitForSingleObject(pp->phandle, INFINITE);
	CloseHandle(pp->phandle);
	pp->phandle = NULL;
#else
	int status;
	waitpid(pp->pid, &status, 0);
#endif
	pp->pid = 0;
}

static coord_t
external_engine_genmove(engine_t *e, board_t *b, time_info_t *ti, enum stone color, bool pass_all_alive)
{
	assert(0);
	return pass;
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
