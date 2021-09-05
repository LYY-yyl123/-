#ifndef TIME_WHEEL_TIMER
#define TIME_WHEEL_TIMER

#include <time.h>
#include <stdio.h>
#include <netinet/in.h>

#define BUFFER_SIZE 64

class tw_timer;
/*绑定socket和定时器*/
struct client_data
{
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    tw_timer *timer;
};

/*定时器类*/
class tw_timer
{
public:
    tw_timer(int rot, int ts): next(NULL), prev(NULL), rotation(rot), time_slot(ts){}

public:
    int rotation;  /*记录定时器在时间轮多少圈之后生效*/
    int time_slot; /*记录定时器属于时间轮上的那个槽(对应的链表，下同)*/
    void (*cb_func)(client_data*); /*定时器回调函数*/
    client_data *user_data; /*客户数据*/
    tw_timer *next;
    tw_timer *prev;
};

/*时间轮*/
class time_wheel
{
public:
    time_wheel():cur_slot(0)
    {
        for (int i = 0; i < N; ++i)
        {
            slots[i] = NULL; /*初始化每个槽的头节点*/
        }
    }

    ~time_wheel()
    {
        /*遍历每个槽并销毁其中的定时器*/
        for (int i = 0; i < N; ++i)
        {
            tw_timer *tmp = slots[i];
            while (tmp)
            {
                slots[i] = tmp->next;
                delete tmp;
                tmp = slots[i];
            }
        }
    }

    /*根据定时器的值timeout创建一个定时器，并把它插入到合适的槽中*/
    tw_timer *add_timer(int timeout)
    {
        if (!timeout)
        {
            return NULL;
        }
        int ticks = 0;
        /*下面根据待插入定时器超时值计算他将在时间轮转动多少个滴答后被触发，并将该滴答数存在ticks中。如果待插入定时器超时值小于SI,则将ticks向上整合为1，否则就向下整合timeout/SI*/
        if (timeout < SI)
        {
            ticks = 1;
        }
        else
        {
            ticks = timeout/SI;
        }
        /*计算待插入的定时器在时间轮转动多少圈后被触发*/
        int rotation = ticks / N;
        /*计算定时器被插入到那个槽中*/
        int ts = (cur_slot + (ticks / N)) % N;
        /*创建新的定时器*/
        tw_timer *timer = new tw_timer(rotation, ts);

        /*如果第ts个槽中没有定时器，则插入到头节点*/
        if (!slots[ts])
        {
            printf("add timer, rotation is %d, ts is %d, cur_slot is %d\n",rotation, ts, cur_slot);
            slots[ts] = timer;
        }
        /*否则将定时器插入到第ts个槽中*/
        else
        {
            timer->next = slots[ts];
            slots[ts]->prev = timer;
            slots[ts] = timer;
        }
        return timer;
    }

    /*删除目标定时器*/
    void del_timer(tw_timer *timer)
    {
        if (!timer)
        {
            return;
        }
        int ts = timer->time_slot;
        /*若目标定时器就是头节点，则重置头节点*/
        if (timer == slots[ts])
        {
            slots[ts] = slots[ts]->next;
            if (slots[ts])
            {
                slots[ts]->prev = NULL;
            }
            delete timer;
        }
        else
        {
            timer->prev->next = timer->next;
            if (timer->next)
            {
                timer->next->prev = timer->prev;
            }
            delete timer;
        }
    }

    /*SI时间到后，调用该函数，时间轮向前滚动一个槽的间隔*/
    void tick()
    {
        tw_timer *tmp = slots[cur_slot];//取得时间轮上当前槽的头节点
        printf("current slot is %d\n", cur_slot);
        while (tmp)
        {
            printf("tick time once\n");
            /*如果定时器的rotation的值大于0，则在这一轮不起作用*/
            if (tmp->rotation > 0)
            {
                tmp->rotation--;
                tmp = tmp->next;
            }
            /*否则说明定时器到期，执行定时任务，然后删除该定时器*/
            else
            {
                tmp->cb_func(tmp->user_data);
                if (tmp = slots[cur_slot])
                {
                    printf("delete head in cur_slot\n");
                    slots[cur_slot] = tmp->next;
                    delete tmp;
                    if (slots[cur_slot])
                    {
                        slots[cur_slot]->prev = NULL;
                    }
                    tmp = slots[cur_slot];
                }
                else
                {
                    tmp->prev->next = tmp->next;
                    if (tmp->next)
                    {
                        tmp->next->prev = tmp->prev;
                    }
                    tw_timer *tmp2 = tmp->next;
                    delete tmp;
                    tmp = tmp2;
                }
            }
        }
        cur_slot = ++cur_slot % N;/*更新当前时间轮的时间槽，以反映时间轮的转动*/
    }
private:
    /*时间轮上槽的数目*/
    static const int N = 60;
    /*每1s转动一次，槽间隔为1s*/
    static const int SI = 1;
    /*时间轮的当前槽*/
    int cur_slot;
    /*时间轮的槽，每个槽指向一个定时器链表，链表无序*/
    tw_timer *slots[N];
};

#endif