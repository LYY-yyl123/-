#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include <stdio.h>
#include <netinet/in.h>
#define BUFFER_SIZE 64
class util_timer;

/*用户数据结构：客户端socket地址，socket文件描述符，读缓存和定时器*/
struct client_data
{
    struct sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    util_timer *timer;
};

/*定时器类*/
class util_timer
{
public:
    util_timer(): prev(NULL), next(NULL){}
public:
    time_t expire;                  /*任务的超时时间，这里使用绝对时间*/
    void (*cb_func)(client_data*);  /*任务回调函数*/
    /*回调函数处理的客户数据，由定时器的执行者传递给回调函数*/
    client_data *user_data;
    util_timer *prev;            //指向上一个定时器
    util_timer *next;            //指向下一个定时器
};

/*定时器链表，是一个升序，双向链表，且带有头节点和尾节点*/
class sort_time_lst
{
public:
    sort_time_lst():head(NULL), tail(NULL){}
    /*链表被销毁时，删除其中所有定时器*/
    ~sort_time_lst()
    {
        util_timer *tmp = head;
        while(tmp)
        {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
    /*将目标定时器timer添加到链表中*/
    void add_timer(util_timer *timer)
    {
        if (!timer)
        {
            return;
        }
        if (!head)
        {
            head = tail = timer;
            return;
        }
        /*如果timer的超时时间小于head，就把timer插入到链表头部，否则调用add_timer()把定时器插入到合适的位置*/
        if (timer->expire < head->expire)
        {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head);
    }

    /*当某个定时任务发生变化时，调整对应定时器在链表中的位置。这个函数只考虑被调整定时器时间延长的情况，即向尾部移动*/
    void adjust_timer(util_timer *timer)
    {
        if (!timer)
            return;
        
        util_timer *tmp = timer->next;
        /*如果该定时器在尾部，或者定时器的新超时值还是小于下一个定时器的值，则无需更改*/
        if (!tmp || (timer->expire < tmp->expire))
            return;
        
        /*如果目标定时器是链表头节点，则将该定时器取出重新插入*/
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }
        /*若不在头部，则取出该定时器，插入到原来位置之后的链表中*/
        else
        {
            timer->next->prev = timer->prev;
            timer->prev->next = timer->next;
            add_timer(timer, timer->next);
        }
    }

    /*将目标定时器从链表中删除*/
    void delete_timer(util_timer *timer)
    {
        if (!timer)
            return ;
        
        /*下面这个条件成立表名链表中只有一个定时器，即目标定时器*/
        if (timer == head && timer == tail)
        {
            delete timer;
            head = NULL;
            tail = NULL;
        }

        /*如果目标链表有两个以上定时器，且timer是头节点*/
        if (timer == head)
        {
            head = timer->next;
            head->prev = NULL;
            delete timer;
            return;
        }

        
        /*如果目标链表有两个以上定时器，且timer是尾节点*/
        if (timer == tail)
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }

        /*目标定时器位于中间*/
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    /*SIGALRM信号每次触发就在其信号处理函数(如果用统一事件源，就在main)中处理一次tick函数，以处理链表上到期的任务*/
    void tick()
    {
        if (!head)
            return ;
        
        printf("time trick\n");
        time_t curtime = time(NULL); /*获取系统当前事件*/

        util_timer *tmp = head;
        /*从头节点开始处理每个定时器，自到遇到一个尚未到期的定时器，这就是定时器的核心逻辑*/
        while (tmp)
        {
            //因为每个定时器都用绝对时间作为超时值，所以可以把超时值和系统当前时间比较以判断定时器是否到期
            if (curtime < tmp->expire)
            {
                break;
            }
            /*调用定时器的回调函数，以执行定时任务*/
            tmp->cb_func(tmp->user_data);
            //执行完定时器的任务后，把定时器从链表中删除，重置链表头节点
            head = tmp->next;
            if (head)
            {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    /*一个重载的辅助函数，他被add_timer和adjust_timer调用,他表示timer节点添加到lst_head之后的链表中*/
    void add_timer(util_timer *timer, util_timer *lst_head )
    {
        util_timer *prev = lst_head;
        util_timer *tmp = prev->next;
        /*遍历lst_head之后的连接，直到找到一个超时时间大于目标定时器的时间的定时器，将目标定时器插入到该节点之前*/
        while (tmp)
        {
            if (timer->expire < tmp->expire)
            {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        /*如果遍历完lst_head，没找到超时时间大于目标定时器的节点，就将目标定时器插入到链表尾部，并设置成新的尾节点*/
        if (!tmp)
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

private:
    util_timer *head;
    util_timer *tail;
};

#endif