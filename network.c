/* Utility functions to redirect stdin, stdout, stderr to sockets. */

#define DEBUG
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#endif

#include "debug.h"
#include "util.h"

#define STDIN  0
#define STDOUT 1
#define STDERR 2

#define BSIZE 4096

static inline void
die(char *msg)
{
	perror(msg);
	exit(42);
}

/* Create a socket, bind to it on the given port and listen.
 * This function is restricted to server mode (port has
 * no hostname). Returns the socket. */
int
port_listen(char *port, int max_connections)
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		die("socket");

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;         
	server_addr.sin_port = htons(atoi(port));     
	server_addr.sin_addr.s_addr = INADDR_ANY; 

	const char val = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)))
		die("setsockopt");
	if (bind(sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1)
		die("bind");
	if (listen(sock, max_connections) == -1)
		die("listen");
	return sock;
}

/* Returns true if in private address range: 10.0.0.0/8 172.16.0.0/12 192.168.0.0/16 */
static bool
is_private(struct in_addr *in)
{
	return (ntohl(in->s_addr) & 0xff000000) >> 24 == 10
	    || (ntohl(in->s_addr) & 0xfff00000) >> 16 == 172 * 256 + 16
	    || (ntohl(in->s_addr) & 0xffff0000) >> 16 == 192 * 256 + 168;
}

/* Waits for a connection on the given socket, and returns the file descriptor.
 * Updates the client address if it is not null.
 * WARNING: the connection is not authenticated. As a weak security measure,
 * the connections are limited to a private network. */
int
open_server_connection(int socket, struct in_addr *client)
{
	assert(socket >= 0);
	for (;;) {
		struct sockaddr_in client_addr;
		int sin_size = sizeof(struct sockaddr_in);
		int fd = accept(socket, (struct sockaddr *)&client_addr, (socklen_t *)&sin_size);
		if (fd == -1) {
			die("accept");
		}
		if (is_private(&client_addr.sin_addr)) {
			if (client)
				*client = client_addr.sin_addr;
			return fd;
		}
		close(fd);
	}
}

/* Opens a new connection to the given port name, which must
 * contain a host name. Returns the open file descriptor,
 * or -1 if the open fails. */
static int
open_client_connection(char *port_name)
{
	char hostname[BSIZE];
	strncpy(hostname, port_name, sizeof(hostname));
	char *port = strchr(hostname, ':');
	assert(port);
	*port++ = '\0';

	struct hostent *host = gethostbyname(hostname);
	if (!host)
		return -1;
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		die("socket");
	struct sockaddr_in sin;
	memcpy(&sin.sin_addr.s_addr, host->h_addr, host->h_length);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(atoi(port));

	if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		close(sock);
		return -1;
	}
	return sock;
}

/* Allow connexion queue > 1 to avoid race conditions. */
#define MAX_CONNEXIONS 5

struct port_info {
	int socket;
	char *port;
};

/* Wait at most 30s between connection attempts. */
#define MAX_WAIT 30

/* Open a connection on the given socket/port.
 * Act as server if the port doesn't contain a hostname,
 * as a client otherwise. If socket < 0 or in client mode,
 * create the socket from the given port and update socket.
 * Block until the connection succeeds.
 * Return a file descriptor for the new connection. */
static int
open_connection(struct port_info *info)
{
	int conn;
	char *p = strchr(info->port, ':');
	if (p) {
		for (int try = 1;; ) {
			conn = open_client_connection(info->port);
			if (conn >= 0) break;
			sleep(try);
			if (try < MAX_WAIT) try++;
		}
		info->socket = conn;
	} else {
		if (info->socket < 0)
			info->socket = port_listen(info->port, MAX_CONNEXIONS);
		conn = open_server_connection(info->socket, NULL);
	}
	return conn;
}

/* Open the log connection on the given port, redirect stderr to it. */
static void
open_log_connection(struct port_info *info)
{
	int log_conn = open_connection(info);
	if (dup2(log_conn, STDERR) < 0)
		die("dup2");
	if (DEBUGL(0))
		fprintf(stderr, "log connection opened\n");
}

/* Thread keeping the log connection open and redirecting stderr to it.
 * It also echoes its input, which can be used to check if the
 * program is alive. As a weak identity check, in server mode the input
 * must start with "Pachi" (without the quotes). */
static void * __attribute__((noreturn))
log_thread(void *arg)
{
	struct port_info *info = arg;
	assert(info && info->port);
	for (;;) {
		char buf[BSIZE];
		int size;
		bool check = !strchr(info->port, ':');
		if (!check)
			write(STDERR, "Pachi\n", 6);
		while ((size = read(STDERR, buf, BSIZE)) > 0) {
			if (check && strncasecmp(buf, "Pachi", 5)) break;
			check = false;
			write(STDERR, buf, size);
		}
		fflush(stderr);
		open_log_connection(info);
	}
	pthread_exit(NULL);
}

/* Open the log connection on the given port, redirect stderr to it,
 * and keep reopening it if the connection is closed. */
void
open_log_port(char *port)
{
	pthread_t thread;
	static struct port_info log_info = { .socket = -1 };
	log_info.port = port;
	open_log_connection(&log_info);

	/* From now on, log_info may only be modified by the single
	 * log_thread so static allocation is ok and there is no race. */
	pthread_create(&thread, NULL, log_thread, (void *)&log_info);
}

/* Open the gtp connection on the given port, redirect stdin & stdout to it. */
void
open_gtp_connection(int *socket, char *port)
{
	static struct port_info gtp_info = { .socket = -1 };
	gtp_info.port = port;
	int gtp_conn = open_connection(&gtp_info);
	for (int d = STDIN; d <= STDOUT; d++) {
		if (dup2(gtp_conn, d) < 0)
			die("dup2");
	}
	if (DEBUGL(0))
		fprintf(stderr, "gtp connection opened\n");
}
