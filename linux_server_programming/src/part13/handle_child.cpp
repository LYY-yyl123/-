#include <sys/wait.h>
//非阻塞调用要当事件发生时执行才能提高效率
static void handle_child(int sig)
{
    pid_t pid;
    int stat;
    if (waitpid(-1, &stat,WNOHANG) > 0)
    {
        //子进程结束善后处理
    }
}