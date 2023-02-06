#ifndef HTTPCONN_H
#define HTTPCONN_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <sys/uio.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <stdlib.h>
#include "global.h"

void addfd(int epollfd, int fd, bool one_shot);
int setnonblocking(int fd);

class http_conn;
struct Conn_node {
    http_conn *p_user;
    Conn_node *next;
};
class http_conn {
public:
    //文件名的最大长度
    static const int FILENAME_LEN = 200;
    //读缓冲区大小
    static const int READ_BUFFER_SIZE = 2048;
    //写缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;

    //http请求方法,仅支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};
    //服务器处理http请求的可能结果
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDDEN_REQUEST,
                    FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    //解析客户请求时，主状态机所处的状态
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0,CHECK_STATE_HEAD, CHECK_STATE_CONTENT};
    //解析客户请求时，从状态机所处的状态,行的读取状态
    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};

public:
    http_conn() {}
    ~http_conn() {}
public:
    void init(int sockfd, const sockaddr_in& addr, Conn_node *p_user_addr);  //第二个参数作用？
    void close_conn(bool real_close = true);  //关闭客户连接
    bool read();  //非阻塞读，判断是否将请求加入请求队列
    bool write();  //非阻塞写，判断是否正确写入
    void process();  //处理客户请求
public:
    static int m_epollfd;    //所有socket上事件注册到同一个epollfd
    static int m_user_count;  //统计用户数量

private:
    void init();  //初始化连接
    HTTP_CODE process_read();  //解析http请求
    bool process_write(HTTP_CODE ret);  //填充http应答

    /*被process_read()调用解析http请求*/
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_request_head(char *text);
    HTTP_CODE parse_request_content(char *text);
    HTTP_CODE do_request();
    char *get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line();

    /* 被process_write()调用填充http应答*/
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_conten_length(int content_length);
    bool add_linger();
    bool add_blank_line();

private:
    Conn_node* m_puser_addr;
    /* 该http连接的socket和对方的socket地址 */
    int m_socket;
    sockaddr_in m_address;
    /* 读缓冲区 */
    char m_read_buf[READ_BUFFER_SIZE];
    /* 表示读缓冲区已经读入客户数据的最后一个字节的下一个位置 */
    int m_read_idx;
    /* 当前正在分析的字符在读缓冲区中的位置 */
    int m_checked_idx;
    /* 当前正在解析的行的位置 */
    int m_start_line;
    /* 写缓冲区 */
    char m_write_buf[WRITE_BUFFER_SIZE];
    /* 写缓冲区待发送的字节数 */
    int m_write_idx;

    /* 主状态机所处的状态 */
    CHECK_STATE m_check_state;
    /* 请求方法 */
    METHOD m_method;

    /* 客户请求的目标文件的完整路径，其内容等于doc_root+m_url,网站根目录+文件名 */
    char m_real_file[FILENAME_LEN];
    /* 客户请求的目标文件名 */
    char* m_url;
    /* http协议版本号 */
    char* m_version;
    /* 主机名 */
    char* m_host;
    /* http请求的消息体长度 */
    int m_content_length;
    /* http请求是否保持连接 */
    bool m_linger;

    /* 客户请求的目标文件被mmap到内存中的起始位置 */
    char* m_file_address;
    /* 目标文件的状态，判断文件是否存在，是否为目录，是否可读，并获取文件大小信息 */
    struct stat m_file_stat;
    /* 用writev函数执行写操作， 定义下面两个成员  */
    struct iovec m_iv[2];
    int m_iv_count;  //写内存块的数量

};




#endif 