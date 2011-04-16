#ifndef PACHI_NETWORK_H
#define PACHI_NETWORK_H

#include <netinet/in.h>

int port_listen(char *port, int max_connections);
int open_server_connection(int socket, struct in_addr *client);
void open_log_port(char *port);
void open_gtp_connection(int *socket, char *port);

#endif
