#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#define BUFFER_SIZE 4096 /*读缓冲区大小*/
/*主状态机的两种可能状态，分别表示：当前正在分析请求行，当前正在分析头部字段*/

enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER};
/*从状态机的三种可能状态，即行的读取状态，分别表示，读取完整的行，行出错和行数据不完整*/
enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};

/*服务器处理http的结果:NO_REQUEST表示客户请求不完整，需要继续读取客户数据；GET_REQUEST表示获取了一个完整的数据请求；BAD_REQUEST表示请求有语法错误；FORBIDDEN_REQUEST表示客户对资源没有
足够的访问权限；INTERNAL_ERROR表示服务器内部错误；CLOSE_CONNECTION表示客户端已经关闭连接*/

enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDDEN_REQUEST, INTERNAL_ERROR, CLOSE_CONNECTION};
//为了简化问题，没有发送一个完整的http响应报文,而是根据服务器的处理结果发送成功或者是失败信息
static const char* szret [] = {"i get a correct result\n", "something wrong\n"};

//用状态机，用于解析一行内容
LINE_STATUS parse_line(char* buffer, int& checked_index, int& read_index)
{
    char temp;
    /* checked_index指向buffer(应用程序的读缓冲区)中当前正在分析的字节，read_index指向buffer中客户数据的尾部的下一字节。buffer中的0~checked_index已经分析完毕，
    滴checked_index~read_index-1字节有下面的程序解析 */
    for (; checked_index < read_index; ++ checked_index)
    {
        //获得当前要分析的字节
        temp = buffer[checked_index];
        //如果当前读到的字符是回车符，则可能读取到一个完整的行
        if (temp == '\r')
        {
            //如果\r恰好是最后一个被读入的数据，那么这次没有读取到一个完整的行，返回LINE_OPEN表示还要继续读取客户数据才能进一步分析
        
            if (checked_index + 1 == read_index)
            {
                return LINE_OPEN;
            }
            //如果下一字符是\n说明我们成功读取到一个完整的行
            else if (buffer[checked_index + 1] == '\n')
            {
                buffer[checked_index++] = '\0';
                buffer[checked_index++] = '\0';
                return LINE_OK;
            }

            //否则说明http请求出现语法问题
            return LINE_BAD;
        }
        //如果当前行是\n，则也有可能读取到一个完整的行
        else if (temp =='\n')
        {
            if (checked_index > 1 && buffer[checked_index - 1] == '\r')
            {
                buffer[checked_index - 1] = '\0';
                buffer[checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        //分析完所有内容都没有发现\r,证明还需要读取更多的客户数据进行分析
    }
    return LINE_OPEN;
}

/*分析请求行*/
HTTP_CODE parse_requestline(char* temp, CHECK_STATE& checkstate)
{
    char* url = strpbrk(temp, " \t");//strpbrk是在源字符串（s1）中找出最先含有搜索字符串（s2）中任一字符的位置并返回，若找不到则返回空指针
    if (!url)
        return BAD_REQUEST;
    
    *url++ = '\0';//*(url++)

    char* method = temp;

    if (strcasecmp(temp, "GET") == 0)//仅支持GET(忽略大小写比较)
    {
        printf("the request method is GET\n");
    }
    else 
        return BAD_REQUEST;
    
    url += strspn(url, " \t");
    /*strspn() 函数用来计算字符串 str 中连续有几个字符都属于字符串 accept，其原型为：
size_t strspn(const char *str, const char * accept);
【函数说明】strspn() 从参数 str 字符串的开头计算连续的字符，而这些字符都完全是 accept 所指字符串中的字符。简单的说，若 strspn() 返回的数值为n，则代表字符串 str 开头连续有 n 个字符都是属于字符串 accept 内的字符。
【返回值】返回字符串 str 开头连续包含字符串 accept 内的字符数目。所以，如果 str 所包含的字符都属于 accept，那么返回 str 的长度；如果 str 的第一个字符不属于 accept，那么返回 0。
注意：检索的字符是区分大小写的。
提示：提示：函数 strcspn() 的含义与 strspn() 相反，可以对比学习。
*/
    char* version = strpbrk(url, " \t");
    if (!version)
        return BAD_REQUEST;
    
    *version++ = '\0';
    version += strspn(version, " \t");
    if (!version)
    {
        return BAD_REQUEST;
    }
    *version++ = '\0';
    version += strspn(version, " \t");
    if (strcasecmp(version, "HTTP/1.1") != 0)
        return BAD_REQUEST;

    //检查url是否合法
    if (strncasecmp(url, "http://", 7) == 0)
    {
        url += 7;
        url = strchr(url, '/');
    }
    if (!url || url[0]!= '/')
        return BAD_REQUEST;
    
    printf("the request url is %s\n", url);
    return NO_REQUEST;

}

//分析头部字段
HTTP_CODE parse_headers(char* temp)
{
    //遇到一个空行，则说明是一个正确的http请求
    if (temp[0] = '\0')
        return GET_REQUEST;
    else if (strncasecmp(temp, "Host:", 5) == 0)//处理一个host头部字段
    {
        temp += 5;
        temp += strspn(temp, " \t");
        printf("the request host is: %s\n", temp);
    }
    else//其他头部字段不处理
    {
        printf("i can't handle other header");
    }   
}

//分析http请求的入口函数
HTTP_CODE parse_content(char* buffer, int& checked_index, CHECK_STATE& checkstate, int& read_index, int& start_line)
{
    LINE_STATUS linestatus = LINE_OK;//记录当前行的读取状态
    HTTP_CODE retcode = NO_REQUEST;//记录HTTp请求的处理结果

    //主状态机，用于从buffer中去取出所有完整的行
    while ((linestatus = parse_line(buffer, checked_index, read_index)) == LINE_OK)
    {
        char* temp = buffer + start_line;//start_line是行在buffer中的起始位置
        start_line = checked_index;//记录下一行的起始位置

        //checkstate记录主状态机当前的状态
        switch(checkstate)
        {
            case CHECK_STATE_REQUESTLINE: //第一个状态，分析请求行
            {
                retcode = parse_requestline(temp, checkstate);
                if (retcode = BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER: //第二个状态，分析头部字段
            {
                retcode = parse_headers(temp);
                if (retcode == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if (retcode == GET_REQUEST)
                {
                    return GET_REQUEST;
                }
                break;
            }

            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    //若没有读取到完整的行，则需要进一步读取客户数据
    if (linestatus == LINE_OPEN)
    {
        return NO_REQUEST;
    }
    else
        return BAD_REQUEST;
}

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

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    printf("ret = %d\n", ret);
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    struct sockaddr_in client;
    socklen_t client_length = sizeof(client);
    int fd = accept(listenfd, (struct sockaddr *)&client, &client_length);

    if (fd < 0)
        printf("errno number is %d\n", errno);
    else
    {
        //读缓冲区、
        char buffer[BUFFER_SIZE];
        int data_read = 0;
        int read_index = 0; //当前已经读取了多少字节的客户数据
        int checked_index = 0; //当前已经分析完了多少客户数据
        int start_line = 0;    //行在buffer中的起始位置

        CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;
        while (1)
        {
            data_read = recv(fd, buffer + read_index, BUFFER_SIZE - read_index, 0);
            if (data_read = -1)
            {
                printf("reading fault\n");
                break;
            }
            else if (data_read == 0)
            {
                printf("remote client has closed connection\n");
                break;
            }
            read_index += data_read;
            //分析目前获得的所有客户数据
            HTTP_CODE result = parse_content(buffer, checked_index, checkstate, read_index, start_line);
            if (result == NO_REQUEST)
            {
                continue; //没有得到一个完整的请求
            }
            else if (result == GET_REQUEST)
            {
                send(fd, szret[0], strlen(szret[0]), 0);//得到一个完整的请求
                break;
            }
            else
            {
                send(fd, szret[1], strlen(szret[1]), 0); //其他情况表示发生错误
                break;
            }
        }
            close(fd);
        
    }
        close(listenfd);
        return 0;
}
