#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <pthread.h>

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 1024

struct fds
{
    int epollfd;
    int sockfd;
};


/*将文件描述符设置成为非阻塞的*/
int setnoblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/*将fd上的EPOLLIN和EPOLLET参数注册到epollfd所指示的内核事件表中.参数oneshot，指定是否注册fd上的EPOLLONESHOT事件*/
void addfd(int epollfd, int fd, bool oneshot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;

    if (oneshot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnoblocking(fd);
}

/*重置fd上的事件。这样操作后，尽管fd上的EPOLLONESHOT事件被注册，但是操作系统仍然会触发fd上的EPOLLIN事件，且只触发一次*/
void reset_oneshot(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}

/*工作线程*/
void* worker(void* arg)
{
    int sockfd = ((fds*)arg)->sockfd;
    int epollfd = ((fds*)arg)->epollfd;
    printf("start new pthread to receive data on fd:%d\n", sockfd);
    char buf[BUFFER_SIZE];
    memset(buf, '\0', BUFFER_SIZE);

    //循环读取sockfd上的数据，直到遇到EAGAIN错误
    while (1)
    {
        int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);
        if (ret == 0)
        {
            close(sockfd);
            printf("foreiner closed the connection\n");
            break;
        }
        else if (ret < 0)
        {
            if (errno == EAGAIN)//EAGAIN用于非阻塞IO,此时无数据读
            {
                reset_oneshot(epollfd, sockfd);
                printf("read laer\n");
                break;
            }
        }
        else 
        {
            printf("get content :%s\n", buf);
            //休眠模拟数据处理过程
            sleep(5);
        }
    }
    printf("end thread receive data on fd: %d\n", sockfd);
}


int main(int argc, char* argv[])
{
    if (argc <= 2)
    {
        printf("uasge:%s use ip_address, port_number\n",basename(argv[0]));
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_port = htonl(port);
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);

    /*注意，监听socket,listenfd是不能注册EPOLLONESHOT事件的，否则应用程序只能处理一个应用连接，因为后序的客户连接请求将不会触发listenfd上的EPOLLIN事件*/
    addfd(epollfd, listenfd, false);

    while (1)
    {
        int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);//-1表示永远阻塞，直到某个事件发生

        if (ret < 0)
        {
            printf("epoll failue\n");
            break;
        }
        for (int i = 0; i < ret; ++i)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t clientaddr_length = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &clientaddr_length);
                //对每个非监听文件的描述符都设置EPOLLNOESHOT事件，以及设置成非阻塞IO
                addfd(epollfd, connfd, true);
            }
            else if (events[i].events & EPOLLIN)
            {
                pthread_t thread;
                fds fds_for_new_worker;
                fds_for_new_worker.epollfd = epollfd;
                fds_for_new_worker.sockfd = sockfd;
                //启动一个工作线程为sockfd服务
                pthread_create(&thread, NULL, worker, (void*) &fds_for_new_worker);
            }
            else
            {
                printf("something else happenrd\n");
            }
        }
    }
    close(listenfd);
    return 0;
}