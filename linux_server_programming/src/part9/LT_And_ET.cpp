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
#define BUFFER_SIZE 10

/*将文件描述符设置成为非阻塞的*/
int setnoblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/*将文件描述符上fd上的EPOLLIN注册到epollfd指示的内核事件表中，参数enable_et指示是否对fd启用ET模式*/
void addfd(int epollfd, int fd, bool enable_et)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    if (enable_et)
    {
        event.events |= EPOLLET;
    }
        epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
        setnoblocking(fd);
}

/*LT模式的工作流程*/
void lt(epoll_event* events, int number, int epollfd, int listenfd) //number对应的是ret
{
    char buf[BUFFER_SIZE];
    for (int i = 0; i < number; ++i)
    {
        int sockfd = events[i].data.fd;
        if (sockfd == listenfd)
        {
            sockaddr_in client_address;
            socklen_t client_addrlength = sizeof(client_address);
            int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
            addfd(epollfd, connfd, false); //对connfd禁用ET
        }
        else if(events[i].events & EPOLLIN)
        {
            //只要sock读缓存中有未读出的数据，下面这段代码被自动触发
            printf("event trigger once\n");
            memset(buf, '\0', BUFFER_SIZE);
            int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);
            if (ret <= 0)
            {
                close(sockfd);
                continue;
            }
            printf("get %d bytes content from %s\n", ret, buf);
        }
        else
        {
            printf("something else happened");
        }
    }
}

/*ET模式的工作流程*/
void et(epoll_event* events, int number, int epollfd, int listenfd)
{
    char buf[BUFFER_SIZE];
    for (int i = 0; i < number; ++i)
    {
        int sockfd = events[i].data.fd;
        if (sockfd == listenfd)
        {
            sockaddr_in client_address;
            socklen_t client_addrlength = sizeof(client_address);
            int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
            addfd(epollfd, connfd, true); //对connfd启用ET
        }
        else if(events[i].events & EPOLLIN)
        {
            /*这段代码不会重复被触发，因此我们循环读出数据，以确保把读缓冲区的数据全部读出*/
            printf("event trigger once\n");
            while (1)
            {
                memset(buf, '\0', BUFFER_SIZE);
                int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);
                if (ret <= 0)
                {
                    /*对于非阻塞IO，下面的条件成立表明数据已经完全读取。此后，epoll就能再次触发epoll上的EPOLLIN事件，已驱动下一次读*/
                    if (errno == EAGAIN && errno == EWOULDBLOCK)//这两个宏在GUN的c库中是同一个值
                    {
                        printf("read later\n");
                        break;
                    }
                    close(sockfd);
                    break;
                }
                else if (ret == 0) //recv为0表示连接关闭，没有接收到任何数据
                {
                    close(sockfd);
                }
                else
                {
                    printf("get %d bytes of content :%s\n", ret, buf);
                }
            }   
        }
        else
        {
            printf("something happened\n");
        }
    }
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
    addfd(epollfd, listenfd, true);

    while (1)
    {
        int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ret < 0)
        {
            printf("epoll failure\n");
            break;
        }
        lt(events, ret, epollfd, listenfd);//使用lt
        //et(events, ret, epollfd, listenfd);
    }
    close(listenfd);
    return 0;
}