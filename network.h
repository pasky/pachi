#ifndef PACHI_NETWORK_H
#define PACHI_NETWORK_H

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

int port_listen(char *port, int max_connections);
int open_server_connection(int socket, struct in_addr *client);
void open_log_port(char *port);
void open_gtp_connection(int *socket, char *port);

#endif
