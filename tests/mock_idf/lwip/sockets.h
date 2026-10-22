#ifndef TESTS_MOCK_LWIP_SOCKETS_H
#define TESTS_MOCK_LWIP_SOCKETS_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef closesocket
#define closesocket(s) close(s)
#endif

int mock_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
#define bind(s, a, l) mock_bind(s, a, l)

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_LWIP_SOCKETS_H
