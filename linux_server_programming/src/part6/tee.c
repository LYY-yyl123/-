#define _GNU_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

int main(int argc, char* argv[])
{
    if (argc <= 2)
    {
        printf("usage: %s <file>\n", argv[0]);
        return 1;
    }
    
    int filefd = open(argv[1], O_CREAT | O_WRONLY | O_TRUNC, 0666);
    assert(filefd > 0);

    int pipefd_stdout[2];
    int ret = pipe(pipefd_stdout);
    assert(ret != -1);

    int pipe_file[2];
    ret = pipe(pipe_file);
    assert(ret != -1);

    //将标准输出内容输出到管道pipefd_stdout
    ret = splice(STDIN_FILENO, NULL, pipefd_stdout[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
    assert(ret != -1);

    //将管道pipefd_stdout输出定向到pipe_file的输入
    ret = tee(pipefd_stdout[0], pipe_file[1], 32768, SPLICE_F_NONBLOCK);
    assert(ret != -1);

    //将pipe_file输出重定向到文件描述符filefd,从而将标准输入内容输入到标准文件
    ret = splice(pipe_file[0], NULL, filefd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
    asset(ret != -1);

    ret = splice(pipe_file[0], NULL, STDOUT_FILENO, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
    asset(ret != -1);

    close(filefd);
    close(pipefd_stdout[0]);
    close(pipefd_stdout[1]);
    close(pipe_file[0]);
    close(pipe_file[1]);
    return 0;
}