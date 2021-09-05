#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>

#define BUFFER_SIZE 512
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

    int sendbuf = atoi(argv[3]);
    int len = sizeof(sendbuf);
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(sendbuf));
    getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sendbuf, (socklen_t*)&len);
    printf("the tcp send buffer after setting is %d\n",sendbuf);


    if (connect(sockfd, (struct sockaddr_in*)&server_address, sizeof(server_address))  != -1)
    {
        char buf[BUFFER_SIZE];
        memset(buf, 'a', BUFFER_SIZE);
        send(sockfd, buf, BUFFER_SIZE, 0);
    }
    

    close(sockfd);
    return 0;
}