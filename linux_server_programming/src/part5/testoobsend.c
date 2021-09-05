#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    
    if (argc <= 2)
    {
        printf("usage:%s ip_address, port_number\n", basename(argv[0])); // char *basename(char *path); 成功： 返回截取path中的去目录部分的最后的文件或路径名指针。  失败： 返回 NULL

        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in server_address;
    bzero(&server_address, sizeof(server_address));
    server_address.sin_port = htons(port);
    server_address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &server_address.sin_addr);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sockfd >= 0);

    if (connect(sockfd, (struct sockaddr_in*)&server_address, sizeof(server_address)) < 0)
    {
        printf("connect false");
    }
    else
    {
        const char oob_data = "abc";
        const char normal_data = "123";
        send(sockfd, normal_data, strlen(normal_data), 0);
        send(sockfd, oob_data, strlen(oob_data), 0);
        send(sockfd, normal_data, strlen(normal_data), 0);
    }

    close(sockfd);
    return 0;
}