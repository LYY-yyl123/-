#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

union semun
{
    
    int val; //用于GETVAL操作，获取信号量的semval值
    struct semid_ds *buf;  //用于IPC_STAT和IPC_SET命令
    unsigned short int *array; //用于GETALL和SETALL
    struct seminfo* _buf;  //用于IPC_INFO命令
};
/*op为-1执行P操作，为1执行V操作*/
void pv(int sem_id, int op)
{
    struct sembuf sem_b;
    sem_b.sem_num = 0;
    sem_b.sem_op = op;
    sem_b.sem_flg = SEM_UNDO;
    semop(sem_id, &sem_b, 1);
}

int main(int argc, char *argv[])
{
    int sem_id = semget(IPC_PRIVATE, 1, 0666);

    union semun sem_un;
    sem_un.val = 1;
    semctl(sem_id, 0, SETVAL, sem_un);

    pid_t id = fork();
    if (id < 0)
    {
        return 1;
    }
    else if (id == 0)
    {
        printf("child try to get binary sem\n");

        /*在父，子进程间共享IPC_PRIVATE信号量的关键就在于二者都可以操作该信号量的标识符sem_id*/
        pv(sem_id, -1);
        printf("child get the sem and would release if after 5 seconds\n");
        sleep(5);
        pv(sem_id , 1);
        exit(0);
    }
    else
    {
        printf("parent try to get binary sem\n");
        pv(sem_id, -1);
        printf("parent get the sem and would release if after 5 seconds\n");
        sleep(5);
        pv(sem_id, 1);
    }
    waitpid(id, NULL, 0);
    semctl(sem_id, 0, IPC_RMID, sem_un); /*删除信号量*/
    return 0;
}