#ifndef THREADPOLL_H
#define THREADPOLL_H

#include <pthread.h>
#include <cstdio>
#include <list>
#include "locker.h"
#include "httpconn.h"
#include "func.h"
using namespace std;


template <typename T> 
class threadpoll {
public:
    threadpoll(int thread_number = 8, int max_requests = 1000);
    ~threadpoll();

public:
    bool append(T *request);  //向请求队列中添加任务
    static void *worker(void *arg);   //工作线程运行的函数
    void run();

private:
    int m_thread_number;  //线程池中的线程数
    int m_max_requests;   //请求队列中最多请求数
    pthread_t *m_threads;  //线程数组
    list<T *> m_workqueue;   //请求队列
    locker m_queuelocker;  //保护请求队列的锁
    sem m_queuestat;   //请求队列是否有任务需要处理
    bool m_stop;  //是否结束线程 
    

};

//构造函数
template <typename T>
threadpoll<T> :: threadpoll(int thread_number, int max_requests) : 
m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),
m_stop(false) {
    if((thread_number <= 0) || (max_requests <= 0)) {
        throw exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) {
        throw exception();
    }

    //创建thread_number个线程，并设置为线程分离状态
    for(int i = 0; i < m_thread_number; ++i) {
        printf("create the %d thread\n", i);
        if(pthread_create(m_threads + i, NULL, worker,this) != 0) {
            delete [] m_threads;
            throw exception();
        }
        if(pthread_detach(m_threads[i]) != 0) {
            delete [] m_threads;
            throw exception();
        }
    }
}

//析构函数
template <typename T>
threadpoll<T> :: ~threadpoll() {
    delete [] m_threads;
    m_stop = true;
}

//添加任务
template <typename T>
bool threadpoll<T> :: append(T *request) {
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//工作线程运行的函数
template <typename T>
void* threadpoll<T> :: worker(void *arg) {
    threadpoll * poll = (threadpoll *)arg;
    poll->run();
    return poll;
}

template<typename T>
void threadpoll<T> :: run() {
    while(!m_stop) {
        m_queuestat.wait();  //等待请求队列中有任务
        m_queuelocker.lock();  //操作请求队列，上锁保护
        if(m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request) {
            continue;
        }
        request->process();
    }
}


#endif