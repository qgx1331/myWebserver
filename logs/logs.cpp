#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include "func.h"
#include <iostream>
#include "config_c.h"
#include "global.h"

using namespace std;

static char err_levels[][20]  = 
{
    {"stderr"},    //0：控制台错误
    {"emerg"},     //1：紧急
    {"alert"},     //2：警戒
    {"crit"},      //3：严重
    {"error"},     //4：错误
    {"warn"},      //5：警告
    {"notice"},    //6：注意
    {"info"},      //7：信息
    {"debug"}      //8：调试
};
Log_t log;

void logInit() 
{
    ConfigReader* p_config = ConfigReader::getInstance();
    const char* p_log_path = p_config->getString("log_path");
    if(p_log_path == NULL) {
        p_log_path = "error.log";
    }
    log.level = p_config->getInt("log_level", 3);
    log.fd = open(p_log_path, O_CREAT|O_APPEND|O_WRONLY, 0644);
    if(log.fd == -1) {
        cerr << "打开日志文件失败" <<endl;
        log.fd = STDERR_FILENO;
    }
    
}

void logWrite(int level, int err, const char *fmt, ...)
{
    char *last;
    char errstr[1024];
    memset(errstr, 0, sizeof(errstr));
    last = errstr + 1023;

    struct timeval tv;
    struct tm tm;
    time_t sec;
    char *p;
    va_list args;

    memset(&tv, 0, sizeof(struct timeval));
    memset(&tm, 0, sizeof(struct tm));

    gettimeofday(&tv, NULL);

    sec = tv.tv_sec;
    localtime_r(&sec, &tm);
    tm.tm_mon++;
    tm.tm_year += 1900;

    char curtimestr[40] = {0};
    snprintf(curtimestr, 39, "%4d/%02d/%02d %02d:%02d:%02d", 
                tm.tm_year, tm.tm_mon,
                tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec);

    p = errstr;
    memcpy(errstr, curtimestr, strlen(curtimestr));
    p += strlen(curtimestr);
    *p = ' ';
    ++p;
    
    sprintf(p, "[%s]", err_levels[level]);
    p += strlen(err_levels[level]);
    p += 2;
    *p = ' ';
    ++p;

    va_start(args, fmt);
    vsnprintf(p, last-p, fmt, args);
    va_end(args);
    p += strlen(p);
    *p = ' ';
    ++p;

    if(err) {
        char *errinfo = strerror(err);
        sprintf(p, "%s", errinfo);
        p += strlen(errinfo);
    }
    *p++ = '\n';

    if(level <= log.level) {
        int n = write(log.fd, errstr, p-errstr);
        if(n == -1) {
            if(log.fd != STDERR_FILENO) {
                n = write(STDERR_FILENO, errstr, p-errstr);
            }
        }
    }
}

int logClose()
{
    if(close(log.fd) == -1) {
        logWrite(3, errno, "关闭日志文件失败");
        return -1;
    }
    return 0;
}