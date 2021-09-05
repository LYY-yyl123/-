#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <libgen.h>

#define BUF_SIZE 1024
static int connfd;

/*SIGURG信号的处理函数*/
void sig_urg(int sig)
{
    int save_errno = errno;
    char buf[BUF_SIZE];
    memset(buf, '\0', BUF_SIZE);
    int ret = recv(connfd, buf, BUF_SIZE - 1, MSG_OOB);
    printf("get %d bytes of OOB data %s\n", ret);
    errno = save_errno;
}

void addsig(int sig, void (*sig_handler)(int))
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sig_urg;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}



int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("usage:%s ip_address, port_number\n", basename(argv[0])); // char *basename(char *path); 成功： 返回截取path中的去目录部分的最后的文件或路径名指针。  失败： 返回 NULL

        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    inet_pton(AF_INET, ip, &address.sin_addr);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    int ret = bind(sock, (struct sockaddr *)&address, sizeof(address));
    printf("ret = %d\n", ret);
    assert(ret != -1);

    ret = listen(sock, 5);
    assert(ret != -1);

    struct sockaddr_in client;
    socklen_t client_length = sizeof(client);
    int connfd = accept(sock, (struct sockaddr *)&client, &client_length);

    if (connfd < 0)
        printf("errno number is %d\n", errno);
    else
    {
        addsig(SIGURG, sig_urg);
        //使用SIGURG之前必须设置socket的宿主进程或者进程组
        fcntl(connfd, F_SETOWN, getpid());

        char buffer[BUF_SIZE];
        while (1) //循环接受普通数据
        {
            memset(buffer, '\0', BUF_SIZE);
            ret = recv(connfd, buffer, BUF_SIZE - 1, 0);
            if (ret < 0)
            {
                break;
            }
            printf("get %d bytes of normal data: %s\n", ret, buffer);
        }
        close(connfd);
    }
    close(sock);
    return 0;
}
