#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>

#include "config_c.h"
#include "func.h"
#include "global.h"


using namespace std;

int listenfd;
int process_type;
int main() {
    //读取配置文件
    ConfigReader* p_config = ConfigReader::getInstance();
    if(p_config->load("../webserver.conf") == false) {
        cout << "读取配置文件失败" <<endl;
        return -1;
    }
    const char* ip = p_config->getString("ip");
    int port = p_config->getInt("port", 80);
    int daemon_flag = p_config->getInt("daemon", 0);

    //创建守护进程
    if(daemon_flag == 1) {
        int daemon_stat = setDaemon();
        switch(daemon_stat) {
            case -1:
                cout << "创建守护进程失败，程序退出" <<endl;
                return -1;
            case 0:
                cerr <<"守护进程创建成功" <<endl;
                break;
            default:
                return 0;
        }
    }

    //初始化日志系统
    logInit();

    int ret = 0;
    //初始化信号
    ret = signalInit();
    if(ret == -1) {
        logWrite(1, errno, "初始化信号失败");
    }
    //master进程开始工作
    listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    //设置linger属性
    struct linger tmp = {1, 0};
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    struct sockaddr_in address;
    bzero(&address, sizeof(address));  //将该结构体地址数据清零
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr.s_addr);  //和课本略有不同
    address.sin_port = htons(port);
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);

    ret = listen(listenfd, 5);
    assert(ret >= 0);
    
    masterProcess();
    return 0;

}

void sourceFree() {
    if(log.fd != STDERR_FILENO) {
        close(log.fd);
    }
    close(listenfd);
}