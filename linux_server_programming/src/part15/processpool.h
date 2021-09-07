/*半同步，半异步并发模式的进程池*/
#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

/*描述一个子进程的类，m_pid是目标子进程的PID，m_pipefd是父进程和子进程通信的管道*/
class process
{
public:
    process():m_pid(-1){}
public:
    pid_t m_pid;
    int m_pipefd[2];
};

/*进程池类,将他定义为模板类是为了代码复用。其模板参数是处理逻辑任务的类*/
template<typename T>
class processpool
{
private:
    /*将构造函数定义为私有的，因此只能通过后面的create静态函数来创建processpool实例*/
    processpool(int listenfd, int process_number = 8);
public:
    /*单例模式，保证程序最多创建一个processpool实例，这是程序正确处理信号的必要条件*/
    static processpool<T>* create(int listenfd, int process_number = 8)
    {
        if (!m_instance)
        {
            m_instance = new processpool<T>(listenfd, process_number);
        }
        return m_instance;
    }
    ~processpool()
    {
        delete []m_sub_process;
    }

    /*启动线程池*/
    void run();
pricvate:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

private:
    /*进程池允许的最大子进程数量*/
    static const int MAX_PROCESS_NUMBER = 16;
    /*每个子进程能处理的客户数量*/
    static const int USER_PER_PROCESS = 65535;
    /*epoll最多能处理的事件数*/
    static const int MAX_ENENT_NUMBER = 10000;
    /*进程池中的进程总数量*/
    int m_process_number;
    /*子进程在池中的序号，从0开始*/
    int m_idx;
    /*每个进程都有一个epoll内核事件表，用m_epollfd标识*/
    int m_epollfd;
    /*监听socket*/
    int m_listenfd;
    /*子进程通过m_stop来决定是否运行*/
    int m_stop;
    /*保存所有子进程的描述信息*/
    process *m_sub_process;
    /*进程池静态实例*/
    static processpool<T>* m_instance;
};

template<typename T>
processpool<T>* processpool<T>::m_instance = NULL;

/*用于处理信号的管道，以实现统一事件源。后面称之为信号管道*/
static int sig_pipefd[2];

static int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL,new_option);
    return old_option;
}

static void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/*从epollfd标识的epoll内核事件表中删除fd上所有的注册事件*/
static void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

static void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

static void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

/*进程池构造函数。参数listenfd是监听socket，它必须在创建进程池之前被创建，否则子进程无法直接引用它。参数process_number指定进程池中子进程的数量*/
template<typename T>
processpool<T>::processpool (int listenfd, int process_number):m_listenfd(listenfd), m_process_number(process_number),m_idx(-1),m_stop(false)
{
    assert((process_number > 0) && (process_number <= MAX_PROCESS_NUMBER));
    m_sub_process = new process[process_number];
    assert(m_sub_process);
    /*创建process_number个子进程，并建立他们和父进程之间的管道*/
    for (int i = 0; i < process_number; ++i)
    {
        int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
        assert(ret == 0);

        m_sub_process[i].m_pid = fork();
        assert(m_sum_process[i].m_pid >= 0);
        if (m_sub_process[i].m_pid > 0)
        {
            close(m_sub_process[i].m_pipefd[1]);
            continue;
        }
        else
        {
            close(m_sub_process[i].m_pipefd[0]);
            m_idx = i;
            break;
        }
    }
}

/*统一事件源*/
template<typename T>
void  processpool<T>::setup_sig_pipe()
{
    /*创建epoll事件监听列表和信号管道*/
    m_epollfd= epol_create(5);
    assert(m_epollfd != -1);

    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);

    setnonblocking(sig_pipefd[1]);
    addfd(m_epollfd, sig_pipefd[0]);

    /*设置信号处理函数*/
    addsig(SIGCHLD, sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGPIPE, SIG_IGN);
}

/*父进程中m_idx值为-1，子进程中m_idx值大于等于0，我们据此判断接下来要运行的是父进程代码还是子进程代码*/
template<typename T>
void processpool<T>::run()
{
    if (m_idx 1= -1)
    {
        run_child();
        return;
    }
    run_parent();
}

template<typename T>
void processpool<T>::run_child()
{
    setup_sig_pipe();
    /*每个子进程都通过其在进程池中的序号值m_idx找到与父进程通信的管道*/
    int pipefd = m_sub_process[m_idx].m_pipefd[1];
    /*子进程需要监听管道文件描述符pipefd，因为父进程将通过它来通知子进程accept连接*/
    addfd(m_epollfd, pipefd);
    epoll_event events[MAX_EVENT_NUMBER];
    T* users = new T[USER_PER_PROCESS];
    assert(users);
    int number = 0;
    int ret = -1;
    
    while (! m_stop)
    {
        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (numbr < 0 && errno != EINTR)
        {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            if ((sockfd == pipefd) && (evnets[i].events & EPOLLIN))
            {
                int client = 0;
                /*从父，子进程之间的管道读取数据，并将结果保存在变量client中.如果读取成功，则表示有新客户连接到来*/
                ret = recv(sockfd, (char *)&client, sizeof(client), 0);
                if (((ret < 0) && (errno != EAGAIN)) || ret == 0)
                {
                    continue;
                }
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength = sizeof(client_address);
                    int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                    if (connfd < 0)
                    {
                        printf("errno is :%d\n", errno);
                        continue;
                    }
                    addfd(m_epollfd, connfd);
                    /*模板类T必须实现init方法，以初始化一个客户连接。我们直接使用connfd来索引逻辑处理对象(T类型的对象)*/
                }
            }
        }
    }
}


#endif