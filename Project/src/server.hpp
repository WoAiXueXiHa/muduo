#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/epoll.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>

// ================================== 日志宏 ==================================
#define INF 0
#define DBG 1
#define ERR 2
#define LOG_LEVEL DBG

/* 日志格式：[线程ID 时间 文件 行号] */
#define LOG(level, format, ...) do { \
    if (level < LOG_LEVEL) break; \
    time_t t = time(NULL); \
    struct tm lt; \
    /* 使用线程安全版本 localtime_r */ \
    localtime_r(&t, &lt); \
    char timeStr[64]; \
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &lt); \
    fprintf(stdout, "[%p %s %s:%d] " format "\n", \
            (void*)pthread_self(), timeStr, __FILE__, __LINE__, ##__VA_ARGS__); \
} while(0)

#define INF_LOG(format, ...) LOG(INF, format, ##__VA_ARGS__)
#define DBG_LOG(format, ...) LOG(DBG, format, ##__VA_ARGS__)
#define ERR_LOG(format, ...) LOG(ERR, format, ##__VA_ARGS__)
// ===============================================================================


// ================================== Buffer ==================================
#define BUFFER_SIZE (1024*1024) // 1M
class Buffer{
private:
    std::vector<char> _buffer;
    // 相对于_buffer的读写索引偏移量
    uint64_t _readIndex = 0;
    uint64_t _writeIndex = 0;
public:
    Buffer() : _buffer(BUFFER_SIZE), _readIndex(0), _writeIndex(0) {}
    char* Begin() { return &*_buffer.begin(); }     // 获取缓冲区首地址，先解引用得到索引0，再取地址
    // 获取当前读写位置
    char* getWriteIndex() { return Begin() + _writeIndex; }
    char* getReadIndex() { return Begin() + _readIndex; }
   
    // 获取头尾空闲空间大小
    uint64_t getHeadSize() { return _readIndex; }   // 读索引之前
    uint64_t getTailSize() { return _buffer.size() - _writeIndex; }    // 总空间-写索引->剩下就是尾部空间

    // 获取可读数据大小 写偏移-读偏移
    uint64_t getReadableSize() { return _writeIndex - _readIndex; }
    
    // 读写指针移动
    void moveReadOffset(uint64_t len) {
        if(0 == len) return;
        // 向后移动的大小，必须雄安与可读数据大小
        assert(len <= getReadableSize());
        _readIndex += len;
    }
    void moveWriteOffset(uint64_t len) {
        if(0 == len) return;
        // 向后移动的大小，必须小于当前后边的空闲空间大小
        assert(len <= getTailSize());
        _writeIndex += len;
    }

    // 确保可写空间足够（整体空间够了就移动数据，否则扩容）
    void ensureWriteable(uint64_t len) {
        // 最优情况：缓冲区末尾空闲空间足够，直接返回
        if(len <= getTailSize()) return;

        // 走到这里，说明尾部空间不够
        // 无论是后续复用头部空间还是扩容
        // 必须先把有效数据移动到头部空间！
        uint64_t readableSize = getReadableSize();
        // 整理内存碎片
        std::copy(getReadIndex(), getReadIndex() + readableSize, Begin());
        _readIndex = 0;
        _writeIndex = readableSize;

        // 整理完碎片后，再次检查尾部空间是否足够
        if(getTailSize() < len) {
            // 此时_buffer里0到_writeIndex全是有效数据
            LOG(DBG, "buffer expand to %lu", _buffer.size() + len);
             _buffer.resize(_writeIndex + len);
        }
    }
    
    // 写数据，不移动写指针
    void Write(const char* data, uint64_t len) {
        if(0 == len) return;
        ensureWriteable(len);
        const char* d = (const char*)data;
        std::copy(d, d + len, getWriteIndex());
    }
    // 写数据并移动指针
    void WriteAndMove(const char* data, uint64_t len) {
        Write(data, len);
        moveWriteOffset(len);
    }

    // 提供各种类型的写入
    void writeString(const std::string& data) { Write(data.c_str(), data.size()); }
    void writeStringAndMove(const std::string& date) { 
        writeString(date); 
        moveWriteOffset(date.size()); 
    }

    void writeBuffer(Buffer& data) { Write(data.getReadIndex(), data.getReadableSize()); }
    void writeBufferAndMove(Buffer& data) { 
        writeBuffer(data); 
        moveWriteOffset(data.getReadableSize()); 
    }

    // 读数据，不移动读指针
    void Read(void* data, uint64_t len) {
        if(0 == len) return;
        assert(len <= getReadableSize());
        std::copy(getReadIndex(), getReadIndex() + len, (char*)data);
    }
    void readAndMove(void* data, uint64_t len) {
        Read(data, len);
        moveReadOffset(len);
    }

    // 提供各种类型的读取
    std::string readString(uint64_t len) {
        if(0 == len) return "";
        assert(len <= getReadableSize());
        // 这里双重拷贝了！！！！！很严重的缺陷！！！！！
        // std::string str(getReadIndex(), len);
        // Read(&str[0], len);
        
        // 直接调用 string 的 (const char* s, size_t n) 构造函数
        // 底层会一次性分配好内存，并直接把 getReadIndex() 开始的 len 个字节 copy 进去
        return std::string(getReadIndex(), len);
    }
    std::string readStringAndMove(uint64_t len) {
        assert(len <= getReadableSize());
        std::string str = readString(len);
        moveReadOffset(len);
        return str;
    }

    char* findCRLF() {
        char* p = (char*)memchr(getReadIndex(), '\n', getReadableSize());
        return p;
    }

    // 获取一行数据
    std::string readLine() {
        char* p = findCRLF();
        if(p == NULL) {
            // 未找到，返回空字符串
            return "";
        }
        // +1把换行符也取出来
        return readString(p - getReadIndex() + 1);
    }
    std::string readLineAndMove() {
        std::string str = readLine();
        moveReadOffset(str.size());
        return str;
    }

    void Clear() {
        _readIndex = 0; 
        _writeIndex = 0;
    }
};
// ===============================================================================

// ================================ Socket =======================================
#define MAX_LISTEN 1024
class Socket{
private:
    int _sockfd;
public:
    Socket(int sockfd) : _sockfd(sockfd) {}
    Socket() : _sockfd(-1) {}
    ~Socket() { if(_sockfd != -1) ::close(_sockfd); }
    int getFd() const { return _sockfd; }

    // 1. 创建套接字
    bool Create() {
        // int socket(int domain, int type, int protocol);
        _sockfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(_sockfd < 0) {
            ERR_LOG("create socket failed: %s", strerror(errno));
            return false;
        }
        return true;
    }

    // 2. 绑定地址
    bool Bind(const std::string& ip, uint16_t port) {
        // int bind(int sockfd, const struct sockaddr *addr,
        //        socklen_t addrlen);
        // struct sockaddr_in { 
        //     sa_family_t    sin_family; /* Address family */ 
        //     in_port_t      sin_port;   /* Port number */ 
        //     struct in_addr sin_addr;   /* Internet address */ 
        //     unsigned char  sin_zero[8]; /* Zero this if you want to */ 
        // }; 
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);  
        addr.sin_addr.s_addr = ::inet_addr(ip.c_str());  
        // 填充后八位，防止脏数据
        memset(addr.sin_zero, 0, sizeof(addr.sin_zero));  

        int ret = ::bind(_sockfd, (struct sockaddr*)&addr, sizeof(addr));
        if(ret < 0) {
            ERR_LOG("bind socket failed: %s", strerror(errno));
            return false;
        }
        return true;
    }
    
    // 3. 监听连接
    bool Listen(int backlog = MAX_LISTEN) {
        // int listen(int sockfd, int backlog);
        int ret = ::listen(_sockfd, backlog);
        if(ret < 0) {    
            ERR_LOG("listen socket failed: %s", strerror(errno));
            return false;
        }    
        return true;
    }

    // 向服务器发送连接请求
    bool Connect(const std::string& ip, uint16_t port) {
        // int connect(int sockfd, const struct sockaddr *addr,
        //             socklen_t addrlen);
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);  
        addr.sin_addr.s_addr = ::inet_addr(ip.c_str());  
        // 填充后八位，防止脏数据
        memset(addr.sin_zero, 0, sizeof(addr.sin_zero));  
        int ret = ::connect(_sockfd, (struct sockaddr*)&addr, sizeof(addr));
        if(ret < 0) {
            ERR_LOG("connect socket failed: %s", strerror(errno));
            return false;
        }
        return true;
    }

    // 获取新连接，返回值为新连接的套接字
    int Accept() {
        // int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
        int connfd = ::accept(_sockfd, NULL, NULL);
        if(connfd < 0) {
            ERR_LOG("accept socket failed: %s", strerror(errno));
            return -1;
        }
        return connfd;
    }


    // 接收数据
    ssize_t Recv(void* data, size_t len) {
        // ssize_t recv(int sockfd, void *buf, size_t len, int flags);
        ssize_t n = ::recv(_sockfd, data, len, 0);
        if(n < 0) {
            // 读到0字节，表示对端关闭连接，-1表示出错
            if(errno == EAGAIN || errno == EINTR) {
                // EAGAIN表示还没收到数据，EINTR表示被信号打断，可以重试
                // 这两种情况不是错误，可以继续读
                return 0;
            }
            ERR_LOG("recv socket failed: %s", strerror(errno));
            return -1;
        }
        return n;   // 返回实际读到的字节数
    }
        
    // 设置非阻塞接收数据
    ssize_t RecvNonblock(void* data, size_t len) {
        // MSG_DONTWAIT 非阻塞模式，如果没有数据立即返回，而不是等待
        return ::recv(_sockfd, data, len, MSG_DONTWAIT);
    }

    // 发送数据
    ssize_t Send(const void* data, size_t len, int flags = 0) {
        // ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        ssize_t n = ::send(_sockfd, data, len, flags);
        if(n < 0) {
            if(errno == EAGAIN || errno == EINTR) {
                // EAGAIN 11表示缓冲区满，EINTR 4表示被信号打断，可以重试
                // 这两种情况不是错误，可以继续写
                return 0;
            }
            ERR_LOG("send socket failed: %s", strerror(errno));
            return -1;
        }
        return n;   // 返回实际写出的字节数
    }
        
    // 设置非阻塞发送数据
    ssize_t SendNonblock(const void* data, size_t len) {
        if(0 == len) return 0;
        // MSG_DONTWAIT 非阻塞模式，如果缓冲区满立即返回，而不是等待
        return ::send(_sockfd, data, len, MSG_DONTWAIT);
    }

    // 关闭连接
    void Close() {
        if(_sockfd != -1) ::close(_sockfd);
        _sockfd = -1;
    }

    // 设置端口复用
    void SetReuseAddr() {
        // int setsockopt(int sockfd, int level, int optname,
        //                const void *optval, socklen_t optlen);
        int opt = 1;
        ::setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    // 设置套接字非阻塞属性
    void SetNonblock() {
        // int fcntl(int fd, int cmd, ... /* arg */ );
        // F_GETFL 获取文件状态标志
        // F_SETFL 设置文件状态标志
        // O_NONBLOCK 设置非阻塞标志
        int flags = ::fcntl(_sockfd, F_GETFL, 0);
        flags |= O_NONBLOCK;
        ::fcntl(_sockfd, F_SETFL, flags);
    }

    // 创建一个服务端连接
    bool createServer(const std::string& ip, uint16_t port, bool block_flag = false) {
    // 创建套接字-》设置端口复用-》绑定地址-》设置非阻塞属性-》监听连接
        if(!Create()) return false;
        SetReuseAddr();
        if(!Bind(ip, port)) return false;
        if(!block_flag) SetNonblock();
        if(!Listen()) return false;
        return true;
    }

    // 创建一个客户端连接
    bool createClient(const std::string& ip, uint16_t port, bool block_flag = false) {
    // 创建套接字-》设置非阻塞属性-》连接服务器
        if(!Create()) return false;
        if(!block_flag) SetNonblock();
        if(!Connect(ip, port)) return false;
        return true;
    }
};

// ===============================================================================

// ================================ Channel ======================================
// 这里只是简单测试，后续要换成EventLoop
class Poller;
class EventLoop;
class Channel {
private:
    int _fd;
    // 这里只是简单测试，后续要换成EventLoop
    Poller* _poller;
    EventLoop* _loop;
    uint32_t _events;       // 当前要监控的事件
    uint32_t _revents;      // 当前连接触发的事件
    using EventCallback = std::function<void()>;

    // 五个回调函数
    EventCallback _readCallback;        // 可读事件触发的回调
    EventCallback _writeCallback;       // 可写事件触发的回调
    EventCallback _closeCallback;       // 连接断开事件触发的回调
    EventCallback _errorCallback;       // 错误事件触发的回调
    EventCallback _eventCallback;       // 任意事件触发的回调

public:
    // 这里只是简单测试，后续要换成EventLoop
    Channel(int fd, Poller* poller) : _fd(fd), _poller(poller), _events(0), _revents(0) {}
    int getFd() const { return _fd; }
    uint32_t getEvents() const { return _events; }              // 获取想要监控的事件
    void setRevents(uint32_t revents) { _revents = revents; }       // 设置实际就绪的事件

    // 设置回调函数
    void setReadCallback(const EventCallback& cb) { _readCallback = cb; }
    void setWriteCallback(const EventCallback& cb) { _writeCallback = cb; }
    void setCloseCallback(const EventCallback& cb) { _closeCallback = cb; }
    void setErrorCallback(const EventCallback& cb) { _errorCallback = cb; }
    void setEventCallback(const EventCallback& cb) { _eventCallback = cb; }

    // 是否监控了可读事件
    bool isReadEnabled() const { return _events & EPOLLIN; }
    // 是否监控了可写事件
    bool isWriteEnabled() const { return _events & EPOLLOUT; }
    // 启动读事件监控
    void enableRead() { _events |= EPOLLIN; Update(); }
    // 停止读事件监控
    void disableRead() { _events &= ~EPOLLIN; Update(); }
    // 启动写事件监控
    void enableWrite() { _events |= EPOLLOUT; Update(); }
    // 停止写事件监控
    void disableWrite() { _events &= ~EPOLLOUT; Update(); }
    // 停止所有事件监控
    void disableAll() { _events = 0; Update(); }

    // 移除事件监控
    void remove();
    void Update();

    // 事件处理，一旦连接触发了事件，就调用这个函数，自己触发了什么事件，如何处理，由回调函数决定
    void handleEvent() {
        if((_revents & EPOLLIN) || (_revents & EPOLLHUP) || (_revents & EPOLLPRI)) {
            // 不管任何事件，只要有数据到来，就调用可读回调
            if(_readCallback) _readCallback();
        } 

        if(_revents & EPOLLOUT) {
            // 写事件触发，调用可写回调
            if(_writeCallback) _writeCallback();
        } 
        
        if(_revents & EPOLLERR) {
            // 错误事件触发，调用错误回调
            if(_errorCallback) _errorCallback();
        } 
        
        if(_revents & EPOLLHUP) {
            // 连接断开事件触发，调用关闭回调
            if(_closeCallback) _closeCallback();
        }
        // 任意事件触发，调用任意回调
        if(_eventCallback) _eventCallback();
    }
};

// ===============================================================================

// ================================ Poller =======================================
#define MAX_EPOLLEVENTS 1024
class Poller {
private:
    int _epfd;
    struct epoll_event _events[MAX_EPOLLEVENTS];        // 事件数组
    std::unordered_map<int, Channel*> _channels;        // 所有监控的连接

private:
    // 对epoll直接操作
    void updateChannel(Channel* channel, int op) {
        // int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
        int fd = channel->getFd();
        // struct epoll_event {
        //     uint32_t events;    /* Epoll events */
        //     epoll_data_t data;  /* User data variable */
        // };
        struct epoll_event event;
        event.data.fd = fd;
        event.events = channel->getEvents();
        int ret = ::epoll_ctl(_epfd, op, fd, &event);
        if(ret < 0) {
            ERR_LOG("epoll_ctl failed: %s", strerror(errno));
        }
    }
    
    // 判断一个Channel是否已经添加了事件监控
    bool isChannelAdded(Channel* channel) {
        auto it = _channels.find(channel->getFd());
        return it != _channels.end();
    }
public:
    Poller() {
        _epfd = ::epoll_create(MAX_EPOLLEVENTS);
        if(_epfd < 0) {
            ERR_LOG("epoll_create failed: %s", strerror(errno));
            abort();
        }
    }

    // 添加或修改监控事件
    void updateChannel(Channel* channel) {
        bool ret = isChannelAdded(channel);
        if(ret == false) {
            // 之前没有添加过，添加监控
            _channels.insert(std::make_pair(channel->getFd(), channel));
            updateChannel(channel, EPOLL_CTL_ADD);
        } else updateChannel(channel, EPOLL_CTL_MOD);
    }

    // 移除监控事件
    void removeChannel(Channel* channel) {
        auto it = _channels.find(channel->getFd());
        if(it != _channels.end()) {
            _channels.erase(it);
        }            
        updateChannel(channel, EPOLL_CTL_DEL);
    }
    
    // 开始监控，返回活跃连接
    // 输出型参数
    void Poll(std::vector<Channel*>* active) {
        // int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
        // -1表示一直等待，0表示立即返回，其他表示等待超时时间
        int nfds = ::epoll_wait(_epfd, _events, MAX_EPOLLEVENTS, -1);
        if(nfds < 0) {
            if(errno == EINTR) return;
            ERR_LOG("epoll_wait failed: %s", strerror(errno));
            abort();
        }
        // 处理活跃连接
        for(int i = 0; i < nfds; ++i) {
            // _events[i].data.fd 就是活跃连接的fd
            auto it = _channels.find(_events[i].data.fd);
            assert(it != _channels.end());
            it->second->setRevents(_events[i].events);   // 设置实际就绪的事件
            active->push_back(it->second);
        }
    }

};


// ===============================================================================
// 注意：这段代码必须放在 Poller 类的大括号结尾之后，main 函数之前！
// 因为此时编译器已经完全认识了 Poller 长什么样，知道它有 updateChannel 这个函数了。
// ===============================================================================

inline void Channel::Update() {
    if (_poller) {
        _poller->updateChannel(this);
    }
}

inline void Channel::remove() {
    if (_poller) {
        _poller->removeChannel(this);
    }
}
// ===============================================================================