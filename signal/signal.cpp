#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#include "global.h"
#include "func.h"


//声明信号处理函数
static void signalHandler(int signo, siginfo_t *siginfo, void *ucontext);

//需要处理的信号
Signal_t signals[] = {
    {SIGCHLD, "SIGCHLD", signalHandler},
    {0, NULL, NULL}
};
int signalInit() 
{
    Signal_t *sig;
    struct sigaction sa;
    for(sig = signals; sig->signo != 0; ++sig) {
        memset(&sa, 0, sizeof(struct sigaction));
        if(sig->handler) {
            sa.sa_sigaction = sig->handler;
            sa.sa_flags = SA_SIGINFO|SA_RESTART;
        }
        else {
            sa.sa_handler = SIG_IGN;
        }
        sigemptyset(&sa.sa_mask);
        if(sigaction(sig->signo, &sa, NULL)) {
            logWrite(1, errno, "sigaction(%s)失败", sig->signame);
            return -1;
        }
        else {
            logWrite(7, 0, "sigaction(%s)成功", sig->signame);
        }
    }
    return 0;
}

static void signalHandler(int signo, siginfo_t *siginfo, void *ucontext)
{
    int status;
    pid_t pid;
    if(process_type == MASTER) {
        switch (signo)
        {
        case SIGCHLD:
            logWrite(7, 0, "master进程接收到SIGCHLD信号");
            pid = waitpid(-1, &status, WNOHANG);
            if(pid == -1) {
                logWrite(3, errno, "master进程调用waitpid失败");
                return;
            }
            logWrite(7, 0, "master进程成功回收了worker子进程pid = %d的系统资源", pid);
            break;

        //以后添加其他信号处理逻辑
        default:
            break;
        }
    }
    else {
        //worker进程的信号处理函数，以后增加
    }
}