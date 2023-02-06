#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include "func.h"
#include "config_c.h"
#include "threadpoll.h"
#include "httpconn.h"
#include "locker.h"
#include "global.h"
#include <iostream>
using namespace std;

#define MAX_EVENT_NUM 1000


void masterProcess()
{   
    sigset_t set;
    sigemptyset(&set);

    //下列这些信号在执行本函数期间不希望收到,fork()子进程时防止信号的干扰；
    sigaddset(&set, SIGCHLD);     //子进程状态改变
    sigaddset(&set, SIGALRM);     //定时器超时
    sigaddset(&set, SIGIO);       //异步I/O
    sigaddset(&set, SIGINT);      //终端中断符
    sigaddset(&set, SIGHUP);      //连接断开
    sigaddset(&set, SIGUSR1);     //用户定义信号
    sigaddset(&set, SIGUSR2);     //用户定义信号
    sigaddset(&set, SIGWINCH);    //终端窗口大小改变
    sigaddset(&set, SIGTERM);     //终止
    sigaddset(&set, SIGQUIT);     //终端退出符
    //屏蔽这些信号，收到也不进行处理
    if(sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        logWrite(3, errno, "masterProcess函数中sigprocmask调用失败");
    }
    process_type = MASTER;
    //读取配置文件
    ConfigReader* p_config = ConfigReader::getInstance();
    int workprocess = p_config->getInt("workprocess", 1);
    //创建工作进程
    for(int i = 0; i < workprocess; ++i) {
        pid_t pid = fork();
        switch (pid)
        {
        case -1:
            logWrite(2, errno, "创建工作进程num = %d失败", i);
            break;
        case 0:
            workProcess();
            break;
        default:
            break;
        }
    }

    sigemptyset(&set);  //信号屏蔽字为空，不再屏蔽信号
    //master进程处理逻辑
    while(1) {
        //设置信号处理函数，处理僵尸进程
        pid_t pid = getpid();
        logWrite(7, 0, "主进程pid = %d开始工作", pid);

        //等待接收信号进行处理
        sigsuspend(&set);  
        sleep(5);

        //以后扩充
    }
    sourceFree();
    return;
}

void workProcess()
{   
    //获取工作进程pid
    pid_t pid = getpid();  
    //设置进程类型
    process_type = WORKER;
    //取消信号屏蔽
    sigset_t set;
    sigemptyset(&set);
    if(sigprocmask(SIG_SETMASK, &set, NULL) == -1) {
        logWrite(3, errno, "worker进程pid = %d取消信号屏蔽失败", pid);
    }
    else {
        logWrite(7, 0, "worker进程pid = %d取消信号屏蔽成功", pid);
    }
    logWrite(7, 0, "工作进程pid = %d创建成功,等待工作", pid);
    //进行一些初始化工作
    ConfigReader* p_config = ConfigReader::getInstance();
    int threadnum = p_config->getInt("threadnum", 5);
    int max_requests = p_config->getInt("max_requests", 100);
    int max_users = p_config->getInt("max_users", 100);

    //1.初始化线程池
    threadpoll<http_conn> *poll = NULL;
    poll = new threadpoll<http_conn>(threadnum, max_requests);
    
    //预先为可能的客户分配一个httpconn对象
    http_conn* users = new http_conn[max_users];
    int user_count = 0;
    Conn_node* m_free_node = new Conn_node[max_users];
    Conn_node* free_node = m_free_node;
    for(int i = 0; i < max_users-1; ++i) {
        free_node[i].p_user = &(users[i]);
        free_node[i].next = &(free_node[i+1]);
    }
    free_node[max_users-1].p_user = &(users[max_users-1]);
    free_node[max_users-1].next = nullptr;

    logWrite(7, 0, "工作进程pid = %d初始化工作完成", pid);
    epoll_event events[MAX_EVENT_NUM];
    int epollfd = epoll_create(5);  //该参数没有任何含义，只需>0
    assert(epollfd != -1);
    //对监听socket不设置oneshot事件，可多次触发，无需循环一次性读完
    addfd(epollfd, listenfd, false);  
    http_conn::m_epollfd = epollfd;  

    while(true) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUM, -1);
        //errno != EINTR设置有什么作用？
        if((num < 0) && (errno != EINTR)) {
            perror("epoll");
            break;
        }
        Conn_node *temp;
        for(int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;
            Conn_node *temp = (Conn_node *)(events[i].data.ptr);
            if(sockfd == listenfd) {
                struct sockaddr_in client_addr;
                socklen_t client_addrlen = sizeof(client_addr);
                int connfd = accept(listenfd, (struct sockaddr*)&client_addr, &client_addrlen);
                if(connfd < 0) {
                    if(errno != EAGAIN) {
                        perror("accept");
                    }
                    continue;
                }
                if(http_conn::m_user_count >= max_users) {
                    const char *info = "Internal busy!";
                    printf("%s\n", info);
                    send(connfd, info, strlen(info), 0);
                    close(connfd);
                    continue;
                }
                //初始化客户连接
                (free_node->p_user)->init(connfd, client_addr, free_node);
                free_node = free_node->next;
                //users[connfd].init(connfd, client_addr);
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //如果有异常，直接关闭客户连接
                /*
                分析每一种异常发生的情况：
                1.EPOLLRDHUP:
                对端正常关闭产生该事件，相当于调用read返回值为0时的效果，说明另一端关闭连接
                2.EPOLLHUP:
                本端出错，自动检测，不用调用epoll_ctl向感兴趣socket添加该事件；
                本端读写均关闭，shutdown()时产生该事件
                3.EPOLLERR:
                本端出错，自动检测，不用调用epoll_ctl向感兴趣socket添加该事件；
                管道读端关闭，往写端写入数据，产生此错误事件；或者向一个已经断开
                连接的socketfd写入数据时，也会产生此错误事件

                */
            
               
                (*temp).p_user->close_conn();
                (*temp).next = free_node;
                free_node = temp;
                //users[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLIN) {
                //根据读的结果，决定是将任务添加到线程池请求队列中，还是关闭连接
                if((temp->p_user)->read()) {
                    //poll->append(users + sockfd);
                    poll->append(temp->p_user);
                }
                else {
                    (*temp).p_user->close_conn();
                    (*temp).next = free_node;
                    free_node = temp;
                }
            }
            else if(events[i].events & EPOLLOUT) {
                //根据写的结果，决定是否关闭连接
                if(!(*temp).p_user->write()) {
                    (*temp).p_user->close_conn();
                    (*temp).next = free_node;
                    free_node = temp;
                }
            }
            else {}
        }
    }
    close(epollfd);
    sourceFree();
    delete [] users;
    delete [] m_free_node;
    delete poll;
    return;
}