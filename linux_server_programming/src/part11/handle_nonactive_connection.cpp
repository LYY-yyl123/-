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
#include "lst_timer.h"

#define MAX_EVENT_NUMBER 1024
#define FD_LIMIT 65535
#define TIMESLOT 5

static int pipefd[2];
/*利用升序链表来管理定时器*/
static sort_time_lst timer_lst;
static int epollfd = 0;

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

void timer_handler()
{
    /*定时处理任务*/
    timer_lst.tick();
    /*一次alarm调用只触发一个SIGALRM，要重新定时，以不断触发SIGALRM信号*/
    alarm(TIMESLOT);
}

/*定时器回调函数，删除非活动连接socket上的注册事件并关闭*/
void cb_func(client_data *user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    printf("close fd %d\n", user_data->sockfd);
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

    addsig(SIGALRM);
    addsig(SIGTERM);            //终止进程，kill默认发送的命令就是SIGTERM
    bool stop_server = false;

    client_data *users = new client_data[FD_LIMIT];
    bool timeout = false;       /*是否该处理非活动连接的标志*/
    alarm(TIMESLOT);            /*定时,信号处理函数的功能是发送信号到pipefd[0],目的是定期清理非活动连接*/

    while (!stop_server)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);    //io复用
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
                users[connfd].address = client_address;
                users[connfd].sockfd = connfd;

                /*创建定时器，设置其回调函数和超时时间，然后绑定定时器和用户数据，最后将定时器添加到timer_lst链表中*/
                util_timer *timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer;
                timer_lst.add_timer(timer);
            }

            /*处理信号*/
            else if (sockfd == pipefd[0])
            {
                int sig;
                char signals[1024];
                ret = recv(sockfd, &signals, sizeof(signals), 0);
                if (ret == -1)
                {
                    //handle the error
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
                            case SIGALRM:
                            {
                                /*用timeout变量标记有定时任务需要处理，但不立即处理定时任务。这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务*/
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            /*处理客户连接上接受到的数据*/
            else if (events[i].events & EPOLLIN)
            {
                memset(users[sockfd].buf, '\0', BUFFER_SIZE);
                ret = recv(sockfd, users[sockfd].buf, BUFFER_SIZE - 1, 0);
                printf("get %d bytes of client data %s from %d\n", ret, users[sockfd].buf, sockfd);
                util_timer *timer = users[sockfd].timer;
                if (ret < 0)
                {
                    /*如果发生读错误，关闭连接并移除对应的定时器(注：没有处理相应的users)*/
                    if (errno != EAGAIN)
                    {
                        cb_func(&users[sockfd]);
                        if (timer)
                        {
                            timer_lst.delete_timer(timer);
                        }
                    }
                }
                else if (ret == 0)
                {
                    /*如果对方关闭连接，我们也关闭连接，并移除对应的定时器(注：没有处理相应的users)*/
                    cb_func(&users[sockfd]);
                    if (timer)
                    {
                        timer_lst.delete_timer(timer);
                    }
                }
                /*ret > 0*/
                else
                {
                    /*如果某个客户端上有数据可读，则我们要调整该连接对应的定时器，以延迟该连接被关闭的时间*/
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        printf("adjust timer once\n");
                        timer_lst.adjust_timer(timer);
                    }
                }
            }
            else
            {
                //other
            }
        }
        /*最后处理定时事件，因为io有更高的优先级。当然，这样会导致定时任务不会精确地按照预期事件执行*/
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
    }
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete []users;
    return 0;
}
