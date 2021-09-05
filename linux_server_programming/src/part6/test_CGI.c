#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h> //基本IO头文件
#include <errno.h>
#include <assert.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h> //malloc cmalloc free,absort,atoi等函数

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

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    int ret = bind(sock, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(sock, 5);
    assert(ret != -1);

    struct sockaddr_in client;
    socklen_t client_addrlength = sizeof(client);

    int connfd = accept(sock, (struct sockaddr*)&client, &client_addrlength);
    if (connfd < 0)
    {
        printf("errno is :%d\n", errno);
    }
    else 
    {
        close(STDOUT_FILENO);
        dup(connfd);          //生成一个最小的文件描述符，指向connfd这个sock连接文件
        printf("abcd\n");
        close(connfd);
    }
    
    close(sock);
    return 0;

}