#define _GNU_SOURCE 1
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
#include <poll.h>
#include <fcntl.h>
#include <libgen.h>

#define USER_LIMIT 5   /*最大用户数量*/
#define BUFFER_SIZE 64 /*读缓冲区大小*/
#define FD_LIMIT 65535      /*文件描述符数量限制*/


struct client_data
{
    struct sockaddr_in address;
    char *write_buf;
    char buf[BUFFER_SIZE];
};

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL);
    return old_option;
}

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("uasge:%s use ip_address, port_number\n", basename(argv[0]));
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

    int ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    /*创建一个user数据，分配FD_LIMIT个client_data对象。可以预期：每个可能的socket都可以获得这样一个对象，并且socket的值可以直接用来索引（作为数组下标）
    socket连接对应得client_data对象，这是将socket和客户数据关联得高效得方式*/
    client_data *users = new client_data[FD_LIMIT];
    /*尽管分配了很多client_data对象，但是为了提高poll的性能，仍然有必要限制用户的数量*/
    pollfd fds[USER_LIMIT + 1]; //被监听集合的大小
    int user_counter = 0;

    for (int i = 1; i <= USER_LIMIT; i++)
    {
        fds[i].fd = -1;
        fds[i].events = 0;
    }
    fds[0].fd = listenfd;
    fds[0].events = POLLIN | POLLERR;
    fds[0].revents = 0;

    while (1)
    {
        ret = poll(fds, user_counter + 1, -1); //最开始只有一个listenfd，且user_counter的初值时0

        if (ret < 0)
        {
            printf("poll failure\n");
            break;
        }

        for (int i = 0; i < user_counter + 1; i++)
        {
            if ((fds[i].fd == listenfd) && (fds[i].revents & POLLIN)) //监听listenfd，如果有数据写，就连接
            {
                struct sockaddr_in client_address;
                socklen_t client_addresslength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addresslength);

                if (connfd < 0)
                {
                    printf("error is %d\n", errno);
                    continue;
                }

                /*如果请求太多，关闭新到的连接*/
                if (user_counter > USER_LIMIT)
                {
                    const char *info = "to many user";
                    printf("%s", info);
                    send(connfd, info, strlen(info), 0);
                    close(connfd);
                    continue;
                }

                /*对于新的连接，可以同时修改fds和users数组。前文已经提到，users[connfd]对应与新连接文件描述符connfd的客户端数据*/
                user_counter++;
                users[connfd].address = client_address;
                setnonblocking(connfd); //设置连接socket为非阻塞
                fds[user_counter].fd = connfd;
                fds[user_counter].events = POLLIN | POLLRDHUP | POLLERR;
                fds[user_counter].revents = 0;
                printf("come a new user,now have %d user\n", user_counter); //user_counter小于5
            }
            else if (fds[i].revents & POLLERR)
            {
                printf("get an error from %d\n", fds[i].fd);
                char errors[100];
                memset(errors, '\0', 100);
                socklen_t len = 100;

                if (getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, errors, &len) < 0)
                    printf("get socket option failure\n");
                continue;
            }

            else if (fds[i].revents & POLLRDHUP) //秒啊 把user_counter处的users和fds填充到要关闭的这个连接，然后边界减1
            {
                //如果客户端关闭，服务器也关闭相应的连接，并将用户数减1
                users[fds[i].fd] = users[fds[user_counter].fd]; //user_counter是已有socket连接的数量
                close(fds[i].fd);
                fds[i] = fds[user_counter];
                i--;
                user_counter--;
                printf("a client left\n");
            }

            else if (fds[i].revents & POLLIN) //数据可读
            {
                int connfd = fds[i].fd;
                memset(users[connfd].buf, '\0', BUFFER_SIZE);
                ret = recv(connfd, users[connfd].buf, BUFFER_SIZE - 1, 0);
                printf("get %d bytes of client data %s from %d", ret, users[connfd].buf, connfd);

                if (ret < 0)
                {
                    //如果读操作出错，关闭连接
                    close(connfd);
                    users[fds[i].fd] = users[fds[user_counter].fd];
                    fds[i] = fds[user_counter];
                    i--;
                    user_counter--;
                }
                else if (ret == 0)
                {
                }
                else
                {
                    for (int j = 1; j < user_counter; j++) //1~user_counter全是连接socket
                    {
                        if (fds[j].fd == connfd) //数据发送者不用写数据到客户端.（如何给其他socket触发写事件的）
                        {
                            continue;
                        }
                        fds[i].events |= ~POLLIN;
                        fds[i].events |= POLLOUT;
                        users[fds[j].fd].write_buf = users[connfd].buf;
                    }
                }
            }
            else if (fds[i].revents & POLLOUT) //数据可写
            {
                int connfd = fds[i].fd;
                if (!users[connfd].write_buf)
                {
                    continue;
                }
                ret = send(connfd, users[fds[i].fd].write_buf, strlen(users[connfd].write_buf), 0);
                users[connfd].write_buf = NULL;
                //写完数据后要重新注册fds[i]上的可读事件
            }
        }
    }
    delete[] users;
    close(listenfd);
    return 0;
}