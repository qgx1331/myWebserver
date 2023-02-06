/*线程同步机制封装类*/
#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <semaphore.h>
#include <exception>
#include "func.h"
using namespace std;


//封装信号量的类
class sem {
public:
    /*
    构造函数，创建信号量，
    第一个0表示信号量类型为当前进程的局部变量；
    第二个0表示信号量的初始值为0；
    */
    sem() {
        if(sem_init(&m_sem, 0, 0) != 0) {
            logWrite(3, 0, "信号量初始化失败");
            throw exception();
        }
    }
    //析构函数，销毁信号量
    ~sem() {
        sem_destroy(&m_sem);
    }
    //等待信号量，以原子操作将信号量的值减1，值为0时，sem_wait阻塞
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    //添加信号量，以原子操作将信号量的值加1，大于0时，其他等待信号量的线程将被唤醒
    bool post() {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

//封装互斥锁的类
class locker {
public:
    locker() {
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            logWrite(3, 0, "互斥锁初始化失败");
            throw exception();
        }
    }
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

private:
    pthread_mutex_t m_mutex;
};

//封装条件变量的类
class cond {
public:
    cond() {
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            logWrite(3, 0, "互斥锁初始化失败");
            throw exception();
        }
        if(pthread_cond_init(&m_cond, NULL) != 0) {
            pthread_mutex_destroy(&m_mutex);
            logWrite(3, 0, "条件变量初始化失败");
            throw exception();
        }
    }
    ~cond() {
        pthread_cond_destroy(&m_cond);
        pthread_mutex_destroy(&m_mutex);
    }
    //等待条件变量
    bool wait() {
        pthread_mutex_lock(&m_mutex);
        int ret = pthread_cond_wait(&m_cond, &m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    //唤醒等待条件变量的线程
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
    pthread_mutex_t m_mutex;
};

#endif 