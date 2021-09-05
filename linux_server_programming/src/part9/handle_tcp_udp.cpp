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
#include <sys/epoll.h>
#include <fcntl.h>
#include <libgen.h>
#include <pthread.h>

#define MAX_EVENT_NUMBER 1024
#define TCP_BUFFER_SIZE 512
#define UDP_BUFFER_SIZE 1024

/*处理一个端口上的TCP和UDP请求*/

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL);
    return old_option;
}

/*将文件描述符上fd上的EPOLLIN注册到epollfd指示的内核事件表中，并对fd启用ET模式*/
void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);   //将fd设置为非阻塞
}


/*tcp socket和udp socket绑定到同一个sockaddr_in上*/
int main(int argc, char* argv[])
{
    if (argc <= 2)
    {
        printf("usage:%s ip_address, port_number\n", basename(argv[0])); 
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int ret = 0;

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    /*创建tcp socket并绑定到端口上*/
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    printf("ret = %d\n", ret);
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    /*创建udp socket并绑定在端口上*/
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int udpfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    printf("ret = %d\n", ret);
    assert(ret != -1);

    ret = listen(udpfd, 5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5); //提示内核时间表需要多大
    assert(epollfd != -1);
    /*注册udp和tcp上的可读事件和ET模式*/
    addfd(epollfd, listenfd);
    addfd(epollfd, udpfd);

    while (1)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0)
        {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd)   //如果是tcp监听socket listenfd上有数据可读，就建立tcp连接。然后添加连接socket的读写事件和ET模式
            {
                struct sockaddr_in client_address;
                socklen_t clientaddress_length = sizeof(client_address);
                int connfd = accept(connfd, (struct sockaddr*)&client_address, &clientaddress_length);
                addfd(epollfd, connfd); //添加connfd的读写事件
            }
            else if (sockfd == udpfd)
            {
                char buf[UDP_BUFFER_SIZE];
                memset(buf, '\0', UDP_BUFFER_SIZE);
                struct sockaddr_in client_address;
                socklen_t clientaddress_length = sizeof(client_address);
                ret = recvfrom(udpfd, buf, UDP_BUFFER_SIZE-1, 0, (struct sockaddr*)&client_address, &clientaddress_length);
                
                if (ret > 0)
                {
                    sendto(udpfd, buf, UDP_BUFFER_SIZE - 1, 0, (struct sockaddr*)&client_address, clientaddress_length);
                }
            }
            else if (events[i].events & EPOLLIN) //数据可读,因为是ET模式，所以要一次性读完缓冲区的所有数据
            {
                char buf[TCP_BUFFER_SIZE];
                while (1)
                {
                    memset(buf, '\0', TCP_BUFFER_SIZE - 1);
                    ret = recv(sockfd, buf, TCP_BUFFER_SIZE -1 ,0);
                    if (ret < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)//非阻塞IO
                        {
                            break;
                        }
                        close(sockfd);
                        break;
                    }
                    else if (ret == 0) //连接关闭，没有接收到数据
                    {
                        close(sockfd);
                        break;
                    }
                    else
                    {
                        send(sockfd, buf, ret, 0);
                    }
                }
            }
            else
            {
                printf("something else happened");
            }
        }
    }
    close(listenfd);
    return 0;
}