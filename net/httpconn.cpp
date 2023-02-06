#include "httpconn.h"

/* 定义http响应的状态信息 */
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The request file not found in this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

/* 网站的根目录 */
const char* doc_root = "/home/gaoxiang/myWebserver/resources";

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;



//设置文件描述符为非阻塞状态
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//向内核事件表中注册fd事件
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    //EPOLLRDHUP事件作用？
    /*
    1.对端正常关闭（程序close（），shell下kill或ctrl+c),触发EPOLLIN和EPOLLRDHUP;
    2.事件设置为ET模式，触发次数少，需要将内容全部读取，其中fd必须设置
    为非阻塞，通过非阻塞情况下读取内容的返回值，来判断数据是否全部读出；
    非阻塞读取空白内容，返回值为-1，并设置errno为EAGAIN或EWOULDBLOCK
    */
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void addfd(int epollfd, int fd, bool one_shot, Conn_node* p_user_addr) {
    epoll_event event;
    event.data.fd = fd;
    event.data.ptr = (void *)p_user_addr;
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}
//向内核事件表中删除fd事件
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
//修改内核事件表中fd的事件
void modfd(int epollfd, int fd, int ev, Conn_node* p_user_addr) {
    epoll_event event;
    event.data.fd = fd;
    event.data.ptr = (void *)p_user_addr;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
//初始化客户连接
void http_conn::init(int sockfd, const sockaddr_in& addr, Conn_node *p_user_addr) {
    m_socket = sockfd;
    m_address = addr;
    m_puser_addr = p_user_addr;
    addfd(m_epollfd, sockfd, true, p_user_addr);
    ++m_user_count;
    init();  //初始化一些用于分析客户请求的变量
}
//初始化一些用于分析客户请求的变量
void http_conn::init() {
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_start_line = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_linger = false;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}
//关闭客户连接
void http_conn::close_conn(bool real_close) {
    if(real_close && m_socket != -1) {
        removefd(m_epollfd, m_socket);
        m_socket = -1;
        --m_user_count;
    }
}
//循环读取数据，直到无数据可读或关闭连接，根据读取的http请求状态，是否将该请求放入请求队列中
bool http_conn::read() {
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int read_bytes = 0;
    while(true) {
        read_bytes = recv(m_socket, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(read_bytes == -1) {
            //非阻塞读取空内容时错误
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }
        //断开连接时
        if(read_bytes == 0) {
            return false;
        }
        m_read_idx += read_bytes;

    }
    return true;
}

//线程工作函数，处理请求队列上的任务
void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST) {
        /* 
        注册了EPOLLONESHOT事件的文件描述符，操作系统最多只能触发一次读或写或异常事件，
        其他线程不能操作该socket。
        防止一个线程在处理一个socketfd读写事件时，还没处理完，该socketfd又来了新事件，
        造成多个线程同时操作一个socket.
        另外，注册了EPOLLONESHOT事件的socket在被一个线程处理完后，需要重置，确保新事
        件来临时能触发
        */
        modfd(m_epollfd, m_socket, EPOLLIN, m_puser_addr);  
        return;
    }
    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_socket, EPOLLOUT, m_puser_addr);

}

//从状态机，解析出一行内容
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for(; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r') {
            if((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n') {
            if((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//解析请求行，获得请求方法、目标URL、HTTP协议版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    //如果请求行中没有空格和'\t',请求有问题
    m_url = strpbrk(text, " \t");  //在text中寻找第一个含有第二个参数中字符的位置
    if(!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char* method = text;
    //只接受get请求，其他请求错误，比较两个字符串大小，遇到空字符结束
    if(strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }
    else {
        return BAD_REQUEST;
    }
    m_url += strspn(m_url, " \t");  //不太明白此操作含义？
    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");  //什么作用？
    //只支持HTTP1.1版本
    if(strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    //获取请求路径
    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;  //此时，指针指向域名地址
        m_url = strchr(m_url, '/');   // /后面就是请求路径
    }
    if(!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEAD;
    return NO_REQUEST;
    
}


//分析请求头
http_conn::HTTP_CODE http_conn::parse_request_head(char* text) {
    //遇到空行表示请求头解析完毕
    if(text[0] == '\0') {
        //如果有消息体，则转移状态，否则得到完整的请求
        if(m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    //处理Connection头部字段
    else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");  //作用是什么？跳过空格？
        if(strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }
    //处理Content-Length头部字段
    else if(strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");  
        m_content_length = atol(text); //将字符串转为长整型，遇到空格结束
    }
    //处理Host头部字段
    else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        printf("oop! unknow header %s\n", text);
    }
    return NO_REQUEST;

}

//并没有真正解析HTTP请求的消息体，只是判断是否完整读完
http_conn::HTTP_CODE http_conn::parse_request_content(char* text) {
    if(m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/* 
当得到一个完整、正确的HTTP请求时，可以分析目标文件的属性。如果目标文件存在、对用户可读、
且不是目录，则使用mmap将其映射到内存地址m_file_address处，并告知调用者获取文件成功
*/
http_conn::HTTP_CODE http_conn::do_request() {
    //获取请求文件完整路径
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    //将请求文件的属性输出给结构体stat，失败表示无此资源
    if(stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    //此判断什么意思，S_IROTH?表示其他用户具有可读权限
    /* 判断此文件对其他用户是否具有可读权限 */
    if(! (m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDDEN_REQUEST;
    }
    //如果请求文件是目录，则请求失败
    if(S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);
    /*
    由mmap函数申请一段内存空间，将目标文件直接映射到其中，返回指向目标内存区域的指针
    */
    m_file_address = (char*)mmap(NULL, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}
//释放申请的内存映射区地址空间
void http_conn::unmap() {
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

//主状态机，读取请求队列上的任务并分析
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    //循环条件不太清楚?do_request()作用？
    /*
    1.循环条件：第一个条件是当状态为解析消息体时，不再一行一行解析，因此设置第一个条件；
    2.当解析完HTTP请求后，若该请求合理有效,，即返回GET_REQUEST,则获取请求的目标文件，
    执行do_request()将目标文件内容映射到内存空间中；
    */
    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) ||
    ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();  //获取一行数据
        m_start_line = m_checked_idx;  //更新下一行起始位置
        printf("got 1 http line: %s\n", text);

        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEAD: {
                ret = parse_request_head(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_request_content(text);
                if(ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}
/* 往写缓冲区中写入待发送的数据 */
bool http_conn::add_response(const char* format, ...) {
    //写缓冲区写满数据，无法写入，返回false
    if(m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);   //对arg_list进行初始化，使其指向可变参数列表里面的第一个参数。
    /* 
    vsnprintf()将格式化数据从可变参数列表写入缓冲区中，返回值为format的长度，但写入时截断字符
    */
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);  //释放指针
    return true;
}
/* 添加状态行 */
bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
/* 添加响应头部 */
bool http_conn::add_headers(int content_len) {
    add_conten_length(content_len);
    add_linger();
    add_blank_line();
}
/* 添加响应头--正文长度字段 */
bool http_conn::add_conten_length(int content_length) {
    return add_response("Content_Length: %d\r\n", content_length);
}
/* 添加响应头--连接方式字段 */
bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
/* 添加空行 */
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}
/* 添加响应体*/
bool http_conn::add_content(const char* content) {
    return add_response("s", content);
}
/* 根据服务器处理http请求的结果，决定返回给客户端的内容 */
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        //500服务器内部错误
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!(add_content(error_500_form))) {
                return false;
            }
            break;
        }
        //400客户端请求有语法错误，不能被服务器识别
        case BAD_REQUEST: {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!(add_content(error_400_form))) {
                return false;
            }
            break;
        }
        //404请求资源不存在
        case NO_RESOURCE: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!(add_content(error_404_form))) {
                return false;
            }
            break;
        }
        //403没有权限访问，拒绝提供服务
        case FORBIDDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        //200请求成功，获取资源
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            if(m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)) {
                    return false;
                }
            }
            break;
        }
        default: {
            return false;
        }
        
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}
//写HTTP响应,不太明白？
bool http_conn::write() {
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if(bytes_to_send == 0) {
        modfd(m_epollfd, m_socket, EPOLLIN, m_puser_addr);
        init();
        return true;
    }

    while(1) {
        temp = writev(m_socket, m_iv, m_iv_count);
        if(temp <= -1) {
            /* 
            如果TCP写缓冲区没有空间，则等待下一次EPOLLOUT事件,在此期间服务器无法接受
            到同一客户的下一个请求，但可以保证连接的完整性
            */
            if(errno == EAGAIN) {
                modfd(m_epollfd, m_socket, EPOLLOUT, m_puser_addr);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        if(bytes_to_send <= bytes_have_send) {
            unmap();
            if(m_linger) {
                init();
                modfd(m_epollfd, m_socket, EPOLLIN, m_puser_addr);
                return true;
            }
            else {
                modfd(m_epollfd, m_socket, EPOLLIN, m_puser_addr);
                return false;
            }
        }
    }
}