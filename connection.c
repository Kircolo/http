#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "connection.h"

int listener_init(Listener_Socket *s, int port) {
    if (!s) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t) port);
    addr.sin_addr.s_addr = INADDR_ANY;

    s->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->fd < 0) return -1;

    if (bind(s->fd, (struct sockaddr *)&addr, (socklen_t) sizeof(addr)) < 0)
        return -1;

    // A backlog of 128 (0x80) was identified from the disassembly.
    if (listen(s->fd, 128) < 0)
        return -1;

    return 0;
}

int listener_accept(Listener_Socket *s) {
    if (!s) return -1;
    
    int client_socket_fd = accept(s->fd, NULL, NULL);
    if (client_socket_fd < 0) {
        return client_socket_fd;
    }

    // Apply a symmetric 5-second timeout for both read and write operations.
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(client_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    return client_socket_fd;
}

ssize_t read_n_bytes(int fd, char *buf, size_t n) {
    size_t have = 0;
    while (have < n) {
        ssize_t r = read(fd, buf + have, n - have);
        if (r > 0) {
            have += (size_t) r;
        } else if (r == 0) {
            break; // EOF before n bytes were read.
        } else {
            return -1; // An error occurred.
        }
    }
    return (ssize_t) have;
}

ssize_t write_n_bytes(int fd, char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w > 0) {
            off += (size_t) w;
        } else {
            return -1; // An error or short/blocked write occurred.
        }
    }
    return (ssize_t) off;
}

ssize_t read_until(int fd, char buf[], size_t n, char *needle) {
    if (!buf || !needle) return -1;
    size_t have = 0;

    while (have < n) {
        ssize_t r = read(fd, buf + have, n - have);
        if (r > 0) {
            have += (size_t) r;

            // This temporary copy pattern was identified in the disassembly.
            char *tmp = (char *) malloc(have + 1);
            if (!tmp) return -1; // Malloc failure.
            
            memcpy(tmp, buf, have);
            tmp[have] = '\0';

            int found = strstr(tmp, needle) != NULL;
            free(tmp);

            if (found) break;
        } else if (r == 0) {
            break; // EOF
        } else {
            return -1; // Error
        }
    }
    return (ssize_t) have;
}

ssize_t pass_n_bytes(int src, int dst, size_t n) {
    char tmp[4096];
    size_t total = 0;

    while (total < n) {
        size_t chunk = n - total < sizeof(tmp) ? n - total : sizeof(tmp);

        ssize_t got = read_n_bytes(src, tmp, chunk);
        if (got < 0) return -1; // Read error.
        if (got == 0) break;    // EOF.

        ssize_t w = write_n_bytes(dst, tmp, (size_t) got);
        if (w < 0) return -1; // Write error.

        total += (size_t) w;
    }
    return (ssize_t) total;
}
