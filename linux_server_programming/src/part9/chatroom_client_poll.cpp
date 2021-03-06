
#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>
#include <poll.h>
#include <fcntl.h>

#define BUFFER_SIZE 64

/*同时监听用户输入和网络连接，利用splice将用户输入内容直接定向到网络连接上以发送之*/
int main(int argc, char* argv[])
{
    if (argc <= 2)
    {
        printf("usage:%s ip_address, port_number\n",basename(argv[0]));
        return 1;
    }
    
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in server_address;
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_address.sin_addr);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sockfd > 0);

    if (connect(sockfd, (struct sockaddr*)&server_address, sizeof(server_address))< 0)
    {
        printf("connection failed\n");
        close(sockfd);
        return 1;
    }
    /*注册标准输入和sockfd上的读事件*/
    struct pollfd fds[2];
    fds[0].fd = 0;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN | POLLRDHUP;
    fds[1].revents = 0;

    char buf_read[BUFFER_SIZE];
    int piped[2];
    int ret = pipe(piped);
    assert(ret != -1);

    while (1)
    {
        ret = poll(fds, 2, -1);
        if (ret < 0)
        {
            printf("poll faileure\n");
            break;
        }
        if (fds[1].revents & POLLRDHUP)
        {
            printf("server connection close\n");
            break;
        }
        else if (fds[1].revents & POLLIN)
        {
            memset(buf_read, '\0', BUFFER_SIZE);
            recv(fds[1].fd, buf_read, BUFFER_SIZE - 1, 0);
            printf("%s\n", buf_read);
        }
        if (fds[0].revents & POLLIN)
        {
            /*使用splice将用户输入的数据写道sockfd上(零拷贝)*/
            ret = splice(0, NULL, piped[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
            ret = splice(piped[0], NULL, sockfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        }
    }

    close(sockfd);
    return 0;
}