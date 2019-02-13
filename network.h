#ifndef PACHI_NETWORK_H
#define PACHI_NETWORK_H

#ifdef NETWORK

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

int port_listen(char *port, int max_connections);
int open_server_connection(int socket, struct in_addr *client);
void open_log_port(char *port);
void open_gtp_connection(int *socket, char *port);

#else

#define port_listen(port, max_conn)        die("network code not compiled in, enable NETWORK in Makefile\n");
#define open_server_connection(s, c)       die("network code not compiled in, enable NETWORK in Makefile\n");
#define open_log_port(port)                die("network code not compiled in, enable NETWORK in Makefile\n");
#define open_gtp_connection(socket, port)  die("network code not compiled in, enable NETWORK in Makefile\n");

#endif /* NETWORK */

#endif /* PACHI_NETWORK_H */
