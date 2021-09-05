#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h> //ip地址等数据结构
#include <errno.h>
#include <string.h>
#include <stdlib.h> //atoi()
#include <unistd.h> //(close, read ,write等)
#include <assert.h>
#include <sys/socket.h>

#define BUF_SIZE 1024

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

    //暂停20s等待客户端连接以及相关操作
    sleep(20);

    struct sockaddr_in client;
    socklen_t client_length = sizeof(client);
    int connfd = accept(sock, (struct sockaddr *)&client, client_length);

    if (connfd < 0)
        printf("errno is %d\n",errno);
    else
    {
        char buffer[BUF_SIZE];
        memset(buffer, '\0', BUF_SIZE);
        ret = recv(connfd, buffer, BUF_SIZE - 1, 0);
        printf("got %d bytes of normal data '%s'\n", ret, buffer);

        memset(buffer, '\0', BUF_SIZE);
        ret = recv(connfd, buffer, BUF_SIZE - 1, MSG_OOB);
        printf("got %d bytes of oob data '%s'\n",ret, buffer);

        memset(buffer, '\0', BUF_SIZE);
        ret = recv(connfd, buffer, BUF_SIZE - 1, 0);
        printf("got %d bytes of normal data '%s'\n", ret, buffer);
        
        close(connfd);
    }

    close(sock);
    return 0;
}