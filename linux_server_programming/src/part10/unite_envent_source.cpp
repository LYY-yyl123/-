#include <sys/epoll.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <libgen.h>

#define MAX_EVENT_NUMBER 1024
static int pipefd[2];

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL);
    return old_option;
}

void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/*信号处理函数*/
void sig_handler(int sig)
{
    //保存原来的errno,在函数的最后恢复，保证函数的可重入性
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0); //将信号值写入管道，通知主循环
    errno = save_errno;
}

/*设置信号的处理函数*/
void addsig(int sig)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sigaction));
    sa.sa_handler = sig_handler;
    sa.sa_flags |=  SA_RESTART;
    sigfillset(&sa.sa_mask);   //信号集中设置所有信号
    assert(sigaction(sig, &sa, NULL) != -1);
}

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("usage:%s ip_address, port_number\n",basename(argv[0]));
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int ret = 0;

    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen >= 0);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    if (ret == -1)
    {
        printf("errno is %d\n", errno);
        return 1;
    }

    ret = listen(listenfd, 5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create1(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd);

    /*使用socketpair创建管道，并注册pipefd[0]上的可读事件*/
    ret = socketpair(PF_INET, SOCK_STREAM, 0,pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0]);

    /*设置一些信号的处理函数*/
    addsig(SIGHUP);         //控制终端挂起
    addsig(SIGCHLD);       //子进程状态发生变化(退出或暂停)
    addsig(SIGINT);       //键盘输入已终止进程 (ctrl + c)
    addsig(SIGTERM);     //终止进程，kill默认发送的命令就是SIGTERM
    bool stop_server = false;

    while (!stop_server)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))   //如果进程在一个慢系统调用(slow system call)中阻塞时，当捕获到某个信号且相应信号处理函数返回时，这个系统调用被中断，调用返回错误，设置errno为EINTR（相应的错误描述为“Interrupted system call”）。
        {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            /*如果就绪描述符是sockfd，则处理新的连接*/
            if (sockfd == listenfd)
            {   
                struct sockaddr_in client_address;
                socklen_t clientaddress_length = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &clientaddress_length);
                addfd(epollfd, connfd);
            }

            /*如果就绪的文件描述符是pipefd[0],则处理信号*/
            else if (sockfd == pipefd[0])
            {
                int sig;
                char signals[1024];
                ret = recv(sockfd, &signals, sizeof(signals), 0);
                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    /*因为每个信号占用一个字节，所以按字节来逐个接受信号。以SIGTERM为例说明如何安全地终止服务器主循环*/
                    for (int i = 0; i < ret; i++)
                    {
                        switch(signals[i])
                        {
                            case SIGCHLD:
                            case SIGHUP:
                            {
                                continue;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            else
            {
            }
        }
    }
    printf("close fds\n");
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    return 0;
}