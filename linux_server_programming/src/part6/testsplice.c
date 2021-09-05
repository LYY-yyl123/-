#define _GNU_SOURCE 1

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>


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
        int pipefd[2];
        ret = pipe(pipefd);
        assert(ret != -1); //创建管道
        //connfd上流入的客户数据定向到管道中
        ret = splice(connfd, NULL, pipefd[1],  NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        assert(ret != -1);

        //管道中的数据重定向到connfd中
        ret = splice(pipefd[0], NULL, connfd,  NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        assert(ret != -1);
        close(connfd);
    }

    close(sock);
    return 0;
}
