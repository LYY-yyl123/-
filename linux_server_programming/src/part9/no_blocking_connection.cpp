#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <libgen.h>

#define BUFFER_SIZE 1023
/*非阻塞socket可能导致connect一直失败。select可能对处于EINPROGRESS状态下的socket可能不起作用*/
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/*超时连接函数，参数分别是服务器IP地址，端口号和超时事件(毫秒)。函数成功时返回已经处于连接状态的socket,失败返回-1*/
int unblock_connect(const char* ip, int port, int time)
{
    int ret = 0;

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    int fdop = setnonblocking(sockfd);
    ret = connect(sockfd, (struct sockaddr*)&address, sizeof(address));

    if (ret == 0)
    {
        //如果连接成功，则恢复sockfd的属性并立即返回
        printf("connection success\n");
        fcntl(sockfd, F_SETFL, fdop);
        return sockfd;
    }
    else if (errno != EINPROGRESS)
    {
        /*如果连接还没有建立,则只有当errno是EINPROGRESS时代表连接还在建立，否则返回错误*/
        printf("unblock connection not support\n");
        return -1;
    }

    fd_set readfds;
    fd_set writefds;
    struct timeval timeout;

    FD_ZERO(&readfds);
    FD_SET(sockfd, &writefds);
    
    timeout.tv_sec = time;
    timeout.tv_usec = 0;
    ret = select(sockfd + 1, NULL, &writefds, NULL, &timeout);

    if (ret <= 0)
    {
        /*select超时或出错，立即返回*/
        printf("connection time out\n");
        close(sockfd);
        return -1;
    }

    if (!FD_ISSET(sockfd, &writefds))
    {
        printf("no event on sockfds found\n");
        close(sockfd);
        return -1;
    }

    int error = 0;
    socklen_t length = sizeof(error);
    /*调用getsockopt清除sockfd上的错误并获取*/
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &length) < 0)
    {
        printf("get errno failure\n");
        close(sockfd);
        return -1;
    }

    //错误号不为0表示连接出错
    if (error != 0)
    {
        printf("connection failure after select with the error:%d\n", error);
        close(sockfd);
        return -1;
    }
    /*连接成功*/
    printf("connection success after select with the error:%d\n", error);
    
    fcntl(sockfd, F_SETFL, fdop);
    return sockfd;
}


int main(int argc, char* argv[])
{
    if (argc <= 2)
    {
        printf("%s usage: ip_address, port number\n", basename(argv[0]));
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    int sockfd = unblock_connect(ip, port, 10);
    if (sockfd < 0)
    {
        return 1;
    }
    close(sockfd);
    return 0;
}