//存放所有函数声明
#ifndef FUNC_H
#define FUNC_H

//和进程相关的函数
int setDaemon();   //创建守护进程
void masterProcess();  //master进程工作
void workProcess();  //work进程工作

//和日志、打印输出相关的函数
void logInit();  //初始化日志文件
void logWrite(int level, int err, const char *fmt, ...);  //向日志文件写错误
int logClose();  //关闭日志文件

//和信号相关的函数
int signalInit();  //初始化信号，注册信号

void sourceFree();  //释放资源

#endif