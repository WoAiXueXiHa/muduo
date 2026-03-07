#pragma once
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include "Log.hpp"

// 封装Socket类，提供一些基本的套接字操作接口
#define MAX_LISTEN 1024
class Socket {
private:
    int _sockfd;
public: 
    Socket() : _sockfd(-1) {}
    Socket(int sockfd) : _sockfd(sockfd) {}
    ~Socket() {
        if(_sockfd != -1) {
            ::close(_sockfd);
        } 
    }  
    int GetFd() const { return _sockfd; }

    // 创建套接字
    bool Create() {
        // int socket(int domain, int type, int protocol);
        // 第三个参数设置为0，表示根据前两个参数自动选择协议
        _sockfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(_sockfd < 0) {
            LOG_ERR("socket error: %s", strerror(errno));
            return false;
        }
        return true;
    }

    // 绑定地址信息
    // int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    // struct sockaddr_in {
    //     sa_family_t    sin_family; // 地址族，必须是AF_INET
    //     in_port_t      sin_port;   // 16位端口号，使用网络字节序
    //     struct in_addr sin_addr;   // 32位IP地址，使用网络字节序
    //     char           sin_zero[8]; // 填充0，保持与struct sockaddr大小一致
    // };
    bool Bind(const std::string& ip, uint16_t port) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);  // htons将主机字节序转换为网络字节序
        addr.sin_addr.s_addr = ::inet_addr(ip.c_str());  // inet_addr将点分十进制IP地址转换为网络字节序的二进制形式
        memset(addr.sin_zero, 0, sizeof(addr.sin_zero));  // 填充0,防止脏数据
        int ret = ::bind(_sockfd, (struct sockaddr*)&addr, sizeof(addr));
        if(ret < 0) {
            LOG_ERR("bind error: %s", strerror(errno));
            return false;
        }
        return true;
    }

    // 开始监听
    bool Listen(int backlog = MAX_LISTEN) {
        // int listen(int sockfd, int backlog);
        int ret = ::listen(_sockfd, backlog);
        if(ret < 0) {
            LOG_ERR("listen error: %s", strerror(errno));
            return false;
        }
        return true;
    }

    // 向服务器发送连接请求
    bool Connect(const std::string& ip, uint16_t port) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ::inet_addr(ip.c_str());
        addr.sin_port = ::htons(port);
        memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
        // int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
        int ret = ::connect(_sockfd, (struct sockaddr*)&addr, sizeof(addr));
        if(ret < 0) {
            LOG_ERR("connect error: %s\n", strerror(errno));
            return false; 
        }
        return true;
    }   
    
    // 获取新连接，返回值是新连接的套接字文件描述符
    int Accept() {
        // int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
        // 第二个参数可以获取客户端的地址信息，这里暂时不需要，所以传入nullptr
        int connfd = ::accept(_sockfd, nullptr, nullptr);
        if(connfd < 0) {
            LOG_ERR("accept error: %s\n", strerror(errno));
            return -1;
        }
        return connfd;
    }

    // 接收数据
    ssize_t Recv(void* buf, size_t len) {
        // ssize_t recv(int sockfd, void *buf, size_t len, int flags);
        ssize_t n = ::recv(_sockfd, buf, len, 0);
        if(n <= 0) {
            // recv返回0表示对方关闭了连接，返回-1表示发生了错误
            if(errno == EAGAIN || errno == EINTR) {
                // EAGAIN表示非阻塞套接字没有数据可读了
                // EINTR表示系统调用被信号中断了
                // 这两种情况都不是错误，可以继续尝试读取
                return 0;
            }
            LOG_ERR("recv error: %s", strerror(errno));
            return -1;
        }
        return n;   // 返回实际接收的数据长度
    }

    // 设置非阻塞接收数据
    ssize_t NonBlockRecv(void* buf, size_t len) {
        // MSG_DONTWAIT表示非阻塞接收，如果没有数据可读就立即返回，而不是阻塞等待
        return ::recv(_sockfd, buf, len, MSG_DONTWAIT);
    }

    // 发送数据
    ssize_t Send(const void* buf, size_t len, int flag = 0) {
        // ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        ssize_t n = ::send(_sockfd, buf, len, flag);
        if(n < 0) {
            if(errno == EAGAIN || errno == EINTR) {
                // EAGAIN表示非阻塞套接字发送缓冲区满了，无法发送数据了
                // EINTR表示系统调用被信号中断了
                // 这两种情况都不是错误，可以继续尝试发送
                return 0;
            }
            LOG_ERR("send error: %s\n", strerror(errno));
            return -1;
        }
        return n;   // 返回实际发送的数据长度
    }

    // 设置非阻塞发送
    ssize_t NonBlockSend(const void* buf, size_t len) {
        if(0 == len) return 0;
        // MSG_DONTWAIT表示非阻塞发送，如果发送缓冲区满了就立即返回，而不是阻塞等待
        return ::send(_sockfd, buf, len, MSG_DONTWAIT);
    }

    // 关闭套接字
    void Close() {
        if(_sockfd != -1) {
            ::close(_sockfd);
            _sockfd = -1;
        }
    }

    // 创建一个服务端连接
    // 缺省参数只能从右往左半缺省，不能跳着来！
    // bool CreateServer(const std::string& ip = "0.0.0.0", uint16_t port, bool block_flag = false) {
    bool CreateServer(const std::string& ip, uint16_t port, bool block_flag = false) {
        // 1. 创建套接字 2. 绑定地址 3. 开始监听 4. 设置非阻塞 5. 启动地址复用
        if(!Create()) return false;
        if(block_flag) NonBlock();
        if(!Bind(ip, port)) return false;
        if(!Listen()) return false;
        return true;
    }

    // 创建一个服务端连接
    bool CreateClient(const std::string& ip, uint16_t port, bool block_flag = false) {
        // 1. 创建套接字 2. 设置非阻塞 3. 向服务器发送连接请求
        if(!Create()) return false;
        if(block_flag) NonBlock();
        if(!Connect(ip, port)) return false;
        return true;
    }

    // 设置地址端口复用
    void ReuseAddr() {
        // int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
        // SOL_SOCKET表示操作套接字级别的选项，SO_REUSEADDR表示允许地址复用
        int opt = 1;
        ::setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    // 设置套接字非阻塞属性
    void NonBlock() {
        // int fcntl(int fd, int cmd, ... /* arg */ );
        // 位图操作
        // F_GETFL获取文件状态标志，F_SETFL设置文件状态标志，O_NONBLOCK表示非阻塞模式
        int flags = ::fcntl(_sockfd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        ::fcntl(_sockfd, F_SETFL, flags);
    } 
};