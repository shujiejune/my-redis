#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket creation failed");
        exit(1);
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6379);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1, defined in host byte order
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv < 0) {
        perror("Connect failed");
        exit(1);
    }

    char msg[] = "hello";
    write(fd, msg, strlen(msg));

    char rbuf[64] = {};
    ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        perror("Read failed");
        exit(1);
    }
    printf("Server says: %s\n", rbuf);

    close(fd);

    return 0;
}
