#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>  //定义了ip的地址结构
#include <errno.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <stdio.h>
#include "stdbool.h"
#include <sys/stat.h>
#include <fcntl.h> 
#include <sys/uio.h>   //writev,readv
#include <libgen.h>    //basename,dirname函数

#define BUFFER_SIZE 1024
static char* status_line[2] = {"200 ok", "500 internal server error"};

int main(int argc, char* argv[])
{
    if (argc <= 3)
    {
        printf("uasge:%s use ip_address, port_number\n",basename(argv[0]));
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    const char* file_name = argv[3];  //将目标文件作为第三个参数传入

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
        //用来保存HTTP应答的状态行，头部字段和一个空行的缓冲区
        char header_buf[BUFFER_SIZE];
        memset(header_buf, '\0', BUFFER_SIZE);
        //用于存放目标文件的缓存
        char* file_buf;

        //获取目标文件的属性
        struct stat file_stat;

        //记录目标文件是否是有效文件
        bool valib = true;
        //缓存区heade_buf已经用了多少字节的空间
        int len = 0;

        if (stat(file_name, &file_stat) < 0) //目标文件不存在 
        {
            valib = false;
        }
        else
        {
            if (S_ISDIR(file_stat.st_mode))  //目标文件是一个目录
                valib = false;
            else if (file_stat.st_mode & S_IROTH) //当前用户有读取目标文件的权限
            {
                int fd = open(file_name, O_RDONLY);
                //动态分配file_buf，并指定其为目标文件大小file_stat.st_size +1,然后将目标文件读入缓存区file_buf
                //file_buf = new char[file_stat.st_size + 1];
                file_buf = (char*)malloc(file_stat.st_size + 1);
                memset(file_buf, '\0', file_stat.st_size + 1);

                if (read(fd ,file_buf, file_stat.st_size + 1) < 0)
                {
                    valib = false;
                }
            }
            else
            {
                valib = false;
            }
        }

        //如果目标问价有效，则发送正常的http应答
        if(valib)
        {
            //下面这部分把http应答的状态行，content-length头部字段和一个空行加入到head_buf中
            int ret = snprintf(header_buf, BUFFER_SIZE -1, "%s %s\r\n", "HTTP/1.1", status_line[0]);//BUFFER_SIZE为写入字符串的最大数目
            len += ret;

            ret = snprintf(header_buf + len, BUFFER_SIZE - 1 - len, "content-length: %ld\r\n", file_stat.st_size);

            len += ret;
            ret = snprintf(header_buf + len, BUFFER_SIZE - 1 - len, "%s", "\r\n");
            //利用writev将header_buf和file_buf内容一并输出
            struct iovec iv[2];
            iv[0].iov_base = header_buf;
            iv[0].iov_len = sizeof(header_buf);
            iv[1].iov_base = file_buf;
            iv[1].iov_len = file_stat.st_size;
            ret = writev(connfd, iv, 2);
        }
        else
        {
            //如果目标文件无效，则通知客户端发生错误
            ret = snprintf(header_buf, BUFFER_SIZE - 1, "%s %s\r\n", "http/1.1", status_line[1]);
            len += ret;
            ret = snprintf(header_buf, BUFFER_SIZE - 1 - len, "%s", "\r\n");
            send(connfd, header_buf, strlen(header_buf), 0);
        }

        close(connfd);
        //delete [] file_buf;
        free(file_buf);

    }
    
    close(sock);
    return 0;
}