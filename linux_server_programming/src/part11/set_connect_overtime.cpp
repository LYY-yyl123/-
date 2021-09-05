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
#include <fcntl.h>
#include <libgen.h>

/*超时连接函数*/
int timeout_connect(const char *ip, int port, int time)
{
    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sockfd >= 0);

    /*通过选项SO_REVTIMEO和SO_SNDTIMEO设置的超时时间类型是timeval,这和select系统调用的时间类型相同*/
    struct timeval timeout;
    timeout.tv_sec = time;
    timeout.tv_usec = 0;

    socklen_t length = sizeof(timeout);
    int ret = setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, length);
    assert(ret != -1);

    ret = accept(sockfd, (struct sockaddr*)&address, (socklen_t*)sizeof(address));
    if (ret == -1)
    {
        //超时对应的错误是EINPROGRESS。下面这个条件如果成立，我们就可以处理定时任务了
        if (errno == EINPROGRESS)
        {
            printf("connect timeout,please process timeout\n");
            return -1;
        }
        printf("errno occur when connect to server");
        return -1;
    }
    return sockfd;
}

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("usage: %s ip_address, port_number\n", basename(argv[0]));
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int sockfd = timeout_connect(ip, port ,10);
    if (sockfd < 0)
        return 1;
    return 0;
}