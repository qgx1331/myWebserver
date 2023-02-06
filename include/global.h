//存放一些数据结构体和全局变量
#ifndef GLOBAL_H
#define GLOBAL_H

#include <signal.h>
#define MASTER 0
#define WORKER 1
//配置文件配置项
typedef struct _ConfItem {
    char item_key[50];
    char item_value[500];
}ConfItem, *PConfItem;

//日志文件相关
typedef struct {
    int fd;
    int level;
}Log_t;

//信号相关
typedef struct _Signal {
    int signo;
    const char *signame;
    void (*handler)(int signo, siginfo_t *siginfo, void *ucontext);
}Signal_t;

extern Log_t log;
extern int listenfd;
extern int process_type; 
#endif