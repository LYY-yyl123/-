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
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/shm.h>

#define USER_LIMIT 5
#define BUFFER_SIZE 1024
#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define PROCESS_LIMIT 65535
/*一个子进程处理一个客户连接*/

/*处理一个客户连接必要的数据*/
struct client_data
{
    sockaddr_in address;   /*客户端的socket地址*/
    int connfd;            /*socket文件描述符*/
    pid_t pid;             /*处理这个连接的子进程的PID*/
    int pipefd[2];         /*和父进程通信的管道*/
};

static const char *shm_name = "/my_shm";   /*共享内存名*/
int sig_pipefd[2];
int epollfd;
int listenfd;
int shmfd;
char *share_mem = 0;
/*客户连接数组，进程用客户连接的编号来索引这个数组，即可取得相关的客户连接数据*/
client_data *users = 0;
/*子进程和客户连接的映射关系表，用进程的PID来索引这个数组，即可取得该进程所处理的客户连接的编号*/
int *sub_process = 0;
/*当前客户数量*/
int user_count = 0;
bool stop_child = false; //是否停止子进程的标志

int setnonblocking (int fd) 
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL);
    return old_option;
}

void addfd(int epollfd, int fd) //注册内核事件
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void sig_handler(int sig) 
{
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void addsig(int sig, void(*handler)(int), bool restart = true)
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

void del_resource()
{
    close(sig_pipefd[0]);
    close(sig_pipefd[1]);
    close(listenfd);
    close(epollfd);
    shm_unlink(shm_name); //关闭使用的共享内存对象
    delete []users;
    delete []sub_process;
}

/*停止一个子进程*/
void child_term_handler(int sig)
{
    stop_child = true;
}

/*子进程运行的函数。参数idx指出该子进程处理的客户连接的编号，users是保存所有客户连接数据的数组，参数share_mem指出的是共享内存的起始地址*/
int run_child(int idx, client_data *users, char *share_men)
{
    epoll_event events[MAX_EVENT_NUMBER];
    /*子进程使用IO复用来同时监听两个文件描述符：客户连接socket,与父进程通信的管道文件描述符*/
    int child_epollfd = epoll_create(5);
    assert(child_epollfd != -1);
    int connfd = users[idx].connfd;
    addfd(child_epollfd, connfd);
    int pipefd = users[idx].pipefd[1];
    addfd(child_epollfd, pipefd);
    int ret;
    /*子进程需要设置自己的SIGTERM信号处理函数*/
    addsig(SIGTERM, child_term_handler, false);

    while (!stop_child) /*stop是一个全局变量，是子进程是否停止的标志*/
    {
        int number = epoll_wait(child_epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR)) //EINTR慢系统调用阻塞，被信号中断，设置errno为EINTR
        {
            printf("epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            /*本子进程负责的客户链接有数据到达，将数据放入共享内存的适当位置后通过pipefd管道通知父进程第几个连接上有数据到达服务器，以便父进程将该数据分发到其他连接中*/
            if (sockfd == connfd && (events[i].events & EPOLLIN))
            {
                memset(share_mem + idx*BUFFER_SIZE, '\0', BUFFER_SIZE);
                /*将客户数据读到对应的读缓存中，该读缓存时共享内存的一段，他开始与idx_BUFFER_SIZE处，长度为BUFFER_SIZE字节。因此，各个客户连接的读缓存是共享的*/
                ret = recv(connfd, share_mem + idx*BUFFER_SIZE, BUFFER_SIZE - 1, 0);
                if (ret < 0)
                {
                    if (errno != EAGAIN)
                    {
                        stop_child = true;
                    }
                }
                else if (ret == 0) //连接断开，关闭子进程
                {
                    stop_child = true;
                }
                else
                {
                    /*成功读取客户数据后就通知主进程(通过管道)来处理。通知主进程第几个连接有数据*/
                    send(pipefd, (char*)&idx, sizeof(idx), 0);
                }
            }
            /*主进程通知本进程（通过管道pipefd[1]）将第client个客户的数据发送到本进程负责的客户端,主要是client参数*/
            else if ((sockfd == pipefd) && (events[i].events & EPOLLIN))
            {
                int client = 0;
                /*接收主进程发送来的数据，即有客户数据到达的连接的编号（主进程发送的是编号？）*/
                ret = recv(sockfd, (char*)&client, sizeof(client), 0);
                if (ret < 0)
                {
                    if (errno != EAGAIN)
                    {
                        stop_child = true;
                    }
                }
                else if (ret == 0)
                {
                    stop_child = true;
                }
                else 
                {
                    send(connfd, share_mem + client * BUFFER_SIZE, BUFFER_SIZE, 0);
                }
            }
            else
            {
                continue;
            }
        }
    }
    close(connfd);
    close(pipefd);
    close(child_epollfd);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    user_count = 0;  //当前客户的数量
    users = new client_data[USER_LIMIT + 1];
    sub_process  = new int[PROCESS_LIMIT];
    for (int i = 0; i <PROCESS_LIMIT; ++i)
    {
        sub_process[i] = -1;
    }

    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);//这个是全局的双向管道
    assert(ret != -1);
    setnonblocking(sig_pipefd[1]);
    addfd(epollfd, sig_pipefd[0]);

    addsig(SIGCHLD, sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGINT, sig_handler);
    addsig(SIGPIPE, SIG_IGN); //连接建立，若某一端关闭连接，而另一端仍然向它写数据，第一次写数据后会收到RST响应，此后再写数据，内核将向进程发出SIGPIPE信号，通知进程此连接已经断开。而SIGPIPE信号的默认处理是终止程序，导致上述问题的发生。
    bool stop_server = false;
    bool terminate = false;

    /*创建共享内存，作为所有客户socket连接的读缓存*/
    shmfd = shm_open(shm_name, O_CREAT | O_RDWR, 0666); //创建共享内存对象
    assert(shmfd != -1);
    ret = ftruncate(shmfd, USER_LIMIT * BUFFER_SIZE);//改变文件大小
    assert(ret != -1);

    share_mem  = (char*)mmap(NULL, USER_LIMIT * BUFFER_SIZE, PROT_READ | PROT_WRITE , MAP_SHARED, shmfd, 0);//把共享内存对象映射到新创建的虚拟内存区域，返回指向这个区域的指针
    assert(share_mem != MAP_FAILED);
    close(shmfd);

    while (!stop_server)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && errno != EINTR)
        {
            printf("epoll failure\n");
            break;
        }
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            /*新的客户连接到来*/
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                if (connfd < 0)
                {
                    printf("erron is: %d\n", errno);
                    continue;
                }
                if (user_count >= USER_LIMIT)
                {
                    const char* info = "too many users\n";
                    printf("%s", info);
                    send(connfd, info, strlen(info), 0);
                    close(connfd);
                    continue;
                }
                /*保存第user_count个客户链接的相关数据*/
                users[user_count].address = client_address;
                users[user_count].connfd = connfd;
                /*在主进程和子进程之间建立管道，以传递必要的数据*/
                ret = socketpair(PF_UNIX, SOCK_STREAM, 0, users[user_count].pipefd); //建立管道
                assert(ret != -1);
                pid_t pid = fork(); //给新的连接分配一个子进程处理。子进程会继承父进程的信号处理函数，所以对于添加的几个信号处理的办法是通过管道，发送给父进程
                if (pid < 0)
                {
                    close(connfd);
                    continue;
                }
                else if (pid == 0)  //子进程
                {
                    close(epollfd);
                    close(listenfd);
                    close(users[user_count].pipefd[0]); //子进程关闭pipefd[0],这意味着子进程可以给父进程发送信息，而父进程只能接收信息
                    close(sig_pipefd[0]);   //关闭全局的一个管道
                    close(sig_pipefd[1]);   //question：如果子进程关闭了管道sig_pipefd[],那么如果在子进程中有信号发生时，还能通过继承的信号处理程序通过管道发送信息给父进程吗？0
                    run_child(user_count, users, share_mem);
                    munmap((void*)share_mem, USER_LIMIT * BUFFER_SIZE);
                    exit(0);
                }
                else        //父进程
                {
                    close(connfd);//关闭父进程的连接socket，是因为子进程继承了父进程的打开文件，并且不需要父进程处理连接数据，所以关闭父进程的连接socket connfd
                    close(users[user_count].pipefd[1]);//主进程关闭pipefd[1]
                    addfd(epollfd, users[user_count].pipefd[0]);//把pipefd[0]上的数据可读事件写入内核
                    users[user_count].pid = pid;
                    /*记录新的客户连接在数组users中得索引值，建立进程pid和该索引之间的映射关系*/
                    sub_process[pid] = user_count;
                    user_count++;
                }
            }
            /*处理信号事件*/
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
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
                    for (int i = 0; i < ret; ++i)
                    {
                        switch(signals[i])
                        {
                            /*子进程退出，表示有某个客户端关闭了连接*/
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
                                {
                                    /*用子进程的pid取得被关闭的客户端连接的编号*/
                                    int del_user = sub_process[pid];
                                    sub_process[pid] = -1;
                                    if ((del_user < 0) || (del_user > USER_LIMIT))
                                    {
                                        continue;
                                    }
                                    /*清除第del_user个客户连接使用的相关数据*/
                                    epoll_ctl(epollfd, EPOLL_CTL_DEL, users[del_user].pipefd[0], 0);
                                    close(users[del_user].pipefd[0]);
                                    users[del_user] = users[--user_count];//把最大的边界数据往前挪
                                    sub_process[users[del_user].pid] = del_user;
                                }
                                if (terminate && user_count == 0)
                                {
                                    stop_server = true;
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                /*结束服务器程序*/
                                printf("kill all the child now\n");
                                if (user_count == 0)
                                {
                                    stop_server = true;
                                    break;
                                }
                                for (int i = 0; i < user_count; ++i)
                                {
                                    int pid = users[i].pid;
                                    kill(pid, SIGTERM);
                                }
                                terminate = true;
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            /*某个子进程向父进程写入了数据*/
            else if (events[i].events & EPOLLIN)
            {
                int child = 0;
                /*读取管道中的数据，child变量记录了是哪个客户连接有数据到达*/
                ret = recv(sockfd, (char*)&child, sizeof(child), 0);
                printf("read data from child accross pipe\n");
                if (ret != -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    /*向除负责处理第child个客户连接的子进程之外的其他子进程发送消息，通知他们有客户数据要写*/
                    for (int j = 0; j < user_count; ++j)
                    {
                        if (users[j].pipefd[0] != sockfd)
                        {
                            printf("send data to child accross pipe\n");
                            send(users[j].pipefd[0], (char*)&child, sizeof(child), 0);
                        }
                    }
                }
            }
        }
    }
    del_resource();
    return 0;
}