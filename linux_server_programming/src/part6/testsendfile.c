#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>     //atoi rand,malloc,cmalloc等
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h> //ip地址结构
#include <stdio.h>
#include <unistd.h>   //close,read,write
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <libgen.h> //basename,dirname
#include <assert.h>

int main(int argc, char *argv[])
{
    
    if (argc <= 3)
    {
        printf("usage:%s ip_address, port_number\n", basename(argv[0])); // char *basename(char *path); 成功： 返回截取path中的去目录部分的最后的文件或路径名指针。  失败： 返回 NULL

        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    const char* file_name = argv[3];

    int filefd = open(file_name, O_RDONLY);
    assert(filefd > 0);

    struct stat stat_buf;
    fstat(filefd, &stat_buf);

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_port = htons(port);
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    int ret = bind(sock, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(sock, 5);
    assert(ret != -1);

    struct sockaddr_in client;
    socklen_t client_addrlength;
    int connfd = accept(sock, (struct sockaddr*)&client, &client_addrlength);

    if (connfd < 0)
    {
        printf("errno is %d\n", errno);
    }
    else
    {
        sendfile(connfd, filefd, NULL, stat_buf.st_size);
        close(connfd);
    }

    close(sock);
    return 0;
}