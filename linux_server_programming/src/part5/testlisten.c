#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <netinet/in.h>

#define bool char
static bool stop = 0;

static void handle_term(int sig)
{
    stop = 1;
}

int main(int argc, char *argv[])
{
    if (argc <= 3)
        printf("usage: %s ip_address port_number backlog\n", basename(argv[0]));

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int backlog = atoi(argv[3]);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    //创建一个ipv4地址
    struct sockaddr_in address;
    bzero(&address, sizeof(address));

    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int ret = bind(sock, (struct sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(sock, 5);
    assert(ret != -1);

    while (!stop)
        sleep(1);

    close(sock);

    return 0;
}