#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <iostream>
using namespace std;
#include "func.h"

int setDaemon()
{
    switch(fork()) {
        case -1:
            cout << "创建子进程失败" << endl;
            return -1;
        case 0:
            break;
        default:
            return 1;  //父进程退出
    }

    //设置子进程为新的会话进行
    if(setsid() == -1) {
        cout << "创建新的会话失败" <<endl;
        return -1;
    }

    umask(0);  //设置掩码为0，防止限制新创建的文件权限
    int fd = open("/dev/null", O_RDWR);
    if(fd == -1) {
        cout << "打开空洞设备失败" <<endl;
        return -1;
    }
    if(dup2(fd, STDIN_FILENO) == -1) {
        cout << "daemon中dup2失败" <<endl;
        return -1;
    }
    if(dup2(fd, STDOUT_FILENO) == -1) {
        cout << "daemon中dup2失败" << endl;
        return -1;
    }
    if(fd > STDERR_FILENO) {
        if(close(fd) == -1) {
            cout << "daemon中close失败" <<endl;
            return -1;
        }
    }
    return 0;
}