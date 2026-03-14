#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/epoll.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
 #include <sys/timerfd.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/eventfd.h>

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
    bool createServer(uint16_t port, const std::string& ip = "0.0.0.0", bool block_flag = false) {
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
    // Channel(int fd, Poller* poller) : _fd(fd), _poller(poller), _events(0), _revents(0) {}
    Channel(int fd, EventLoop* loop) : _fd(fd), _loop(loop), _events(0), _revents(0) {}
    
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
    void Remove();
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
    void updateChannelPl(Channel* channel, int op) {
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
    void updateChannelPl(Channel* channel) {
        bool ret = isChannelAdded(channel);
        if(ret == false) {
            // 之前没有添加过，添加监控
            _channels.insert(std::make_pair(channel->getFd(), channel));
            updateChannelPl(channel, EPOLL_CTL_ADD);
        } else updateChannelPl(channel, EPOLL_CTL_MOD);
    }

    // 移除监控事件
    void removeChannelPl(Channel* channel) {
        auto it = _channels.find(channel->getFd());
        if(it != _channels.end()) {
            _channels.erase(it);
        }            
        updateChannelPl(channel, EPOLL_CTL_DEL);
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

// ================================ Timer =======================================
class EventLoop;
using taskFunc = std::function<void()>;
using realseFunc = std::function<void()>;

class TimerTask {
private:
    uint64_t _id;            // 定时器任务对象ID
    uint32_t _timeout;       // 超时时间
    bool _canceled;          // 是否取消
    taskFunc _taskCb;        // 任务回调
    realseFunc _releaseCb;   // 释放资源回调
public:
    TimerTask(uint64_t id, uint32_t timeout, const taskFunc& cb)
        : _id(id), _timeout(timeout), _canceled(false), _taskCb(cb) {}

    ~TimerTask() {
        if(_canceled == false) _taskCb();
        _releaseCb();
    }

    void Cancel() { _canceled = true; }
    void setReleaseCallback(const realseFunc& cb) { _releaseCb = cb; }
    uint32_t getTimeout() const { return _timeout; }
    
};

class TimerWheel {
private:
    using weakTask = std::weak_ptr<TimerTask>;
    using ptrTask = std::shared_ptr<TimerTask>;
    int _tick;                                      // 秒针，走到哪里执行哪里的任务
    int _capacity;                                  // 最大延迟时间
    std::unordered_map<uint64_t, weakTask> _timers;
    std::vector<std::vector<ptrTask>> _wheel;

    EventLoop* _loop;
    int _timerFd;               // 定时器fd，可读事件回调时，从这里获取到超时事件
    std::unique_ptr<Channel> _timerChannel;

private:
    void removeTimer(uint64_t id) {
        auto it = _timers.find(id);
        if(it != _timers.end()) _timers.erase(it);
    }

    static int createTimerFd() {
        // 创建定时器fd
        // int timerfd_create(int clockid, int flags);
        // CLOCK_MONOTONIC 系统时钟，不受系统时钟变化影响
        int timerfd = ::timerfd_create(CLOCK_MONOTONIC, 0);
        if(timerfd < 0) {
            ERR_LOG("timerfd_create failed: %s", strerror(errno));
            abort();
        }
        // 设置定时器fd
        // int timerfd_settime(int fd, int flags, const struct itimerspec *new_value, struct itimerspec *old_value);
        // struct itimerspec {
        //     struct timespec it_interval;  /* 间隔时间 */
        //     struct timespec it_value;     /* 第一次到期时间 */
        // };
        // it_value表示第一次到期时间，it_interval表示间隔时间
        struct itimerspec value;
        value.it_value.tv_sec = 1;                            
        value.it_value.tv_nsec = 0;                         
        value.it_interval.tv_sec = 1;                       
        value.it_interval.tv_nsec = 0;
        ::timerfd_settime(timerfd, 0, &value, NULL);
        return timerfd;
    }

    int readTimerFd() {
        uint64_t times;
        int ret = ::read(_timerFd, &times, sizeof(times));
        if(ret < 0) {
            ERR_LOG("read timerfd failed: %s", strerror(errno));
            abort();
        }
        return times;
    }
        
    void runTimerTask() {
        _tick = (_tick + 1) % _capacity;
        _wheel[_tick].clear();
    }

    void onTimer() {
        int times = readTimerFd();
        for(int i = 0; i < times; ++i) {
            runTimerTask();
        }
    }

    void timerAddInWheel(uint64_t id, uint32_t timeout, const taskFunc& cb) {
        ptrTask task(new TimerTask(id, timeout, cb));
        task->setReleaseCallback(std::bind(&TimerWheel::removeTimer, this, id));
        int pos = (_tick + timeout) % _capacity;
        _wheel[pos].push_back(task);
        _timers[id] = weakTask(task);
    }

    void timerRefreshInWheel(uint64_t id) {
        auto it = _timers.find(id);
        if(it == _timers.end()) return;

        ptrTask task = it->second.lock();
        int delay = task->getTimeout();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(task);
    }

    void timerCancelInWheel(uint64_t id) {
        auto it = _timers.find(id);
        if(it == _timers.end()) return;

        ptrTask task = it->second.lock();
        if(task) task->Cancel();
    }
public:
    TimerWheel(EventLoop* loop) :_tick(0), _capacity(60), _wheel(60), _loop(loop)
        , _timerFd(createTimerFd()), _timerChannel(new Channel(_timerFd, _loop)) {
        // 给定时器fd添加可读事件回调，读取定时器fd事件通知次数
        _timerChannel->setReadCallback(std::bind(&TimerWheel::onTimer, this));
        // 启动定时器fd的可读事件监控，托管给Poller
        _timerChannel->enableRead();
    }

    void timerAdd(uint64_t id, uint32_t timeout, const taskFunc& cb);
    void timerRefresh(uint64_t id);
    void timerCancel(uint64_t id);
    bool isInTimer(uint64_t id) { return _timers.find(id) != _timers.end(); }
};
// ===============================================================================


// ================================ EventLoop ================================================
class EventLoop {
private:
    using Functor = std::function<void()>;
    std::thread::id _threadId;                // 记录当前EventLoop所在的线程ID，用于判断是否跨线程
    int _eventFd;                             // 用于跨线程唤醒epoll_wait的eventfd
    std::unique_ptr<Channel> _eventChannel;   // 包装eventFd的通道，让它也能像网络连接一样被epoll监听
    Poller _poller;                           // 负责底层epoll的循环
    std::vector<Functor> _tasks;              // 任务池
    std::mutex _mutex;                        // 保证_tasks这个跨线程共享资源不被竞争破坏
    TimerWheel _timer_wheel;                  //定时器模块

public:
    // 创建内核级的事件通知描述符
    static int createEventfd() {
        // int eventfd(unsigned int initval, int flags);
        // EFD_NONBLOCK 非阻塞模式
        // EFD_CLOEXEC  关闭文件描述符
        int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if(efd < 0) {
            ERR_LOG("eventfd failed: %s", strerror(errno));
            abort();
        }
        return efd;
    }

    // 执行任务池中的所有跨线程任务
    void runAllTasks() {
        // 先清空任务池
        std::vector<Functor> functors;
        {
            // 加锁，保证任务池操作线程安全
            std::unique_lock<std::mutex> lock(_mutex);
            // 交换任务池和functors
            _tasks.swap(functors);
        }// 锁自动释放，其它线程可以往任务池中添加任务
        // 执行任务池中的所有任务
        for(auto& functor : functors) {
            functor();
        }
    }

    // eventFd的可读事件回调：有人调用wakeup往里写东西时，epoll会唤醒并执行这个回调
    void readEventFd() {
        uint64_t res = 0;
        // 必须把写进来的东西读出来，否则下次epoll_wait就不会再唤醒了
        int ret = ::read(_eventFd, &res, sizeof(res));
        if(ret < 0) {
            if(errno == EAGAIN || errno == EINTR) return;
            ERR_LOG("read eventfd failed: %s", strerror(errno));
            abort();
        }
    }

    // 唤醒epoll_wait
    void wakeupEventFd() {
        uint64_t val = 1;
        int ret = ::write(_eventFd, &val, sizeof(val));
        if(ret < 0) {
            if(errno == EINTR) return; 
            ERR_LOG("write eventfd failed: %s", strerror(errno));
            abort();
        }
    }


public:
    EventLoop() : _threadId(std::this_thread::get_id())
                , _eventFd(createEventfd())
                , _eventChannel(new Channel(_eventFd, this)) 
                , _timer_wheel(this) 
    {
        // 给eventFd添加可读事件回调，读取eventFd事件通知次数
        _eventChannel->setReadCallback(std::bind(&EventLoop::readEventFd, this));
        // 启动eventFd的可读事件监控，托管给Poller
        _eventChannel->enableRead();
                
    }

    // 事件监控-》就绪事件处理-》执行任务
    void Loop() {
        while(1) {
            // 事件监控
            std::vector<Channel*> actives;
            _poller.Poll(&actives);
            // 就绪事件处理
            for(auto& channel : actives) {
                channel->handleEvent();
            }            
            // 执行任务池中的任务
            runAllTasks();
        }
    }

    // 判断当前线程是都是EventLoop对应的线程
    bool isInLoop() { return (_threadId == std::this_thread::get_id()); }
    void assertInLoop() { assert(isInLoop()); }

    // 让代码一定在EventLoop所在的线程中执行
    void runInLoop(const Functor& cb) {
        if(isInLoop()) cb();
        else queueInLoop(cb);
    }

    // 压入任务到任务池
    void queueInLoop(const Functor& cb) {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _tasks.push_back(cb);
        }
        // 通知EventLoop有任务需要执行
        wakeupEventFd();
    }

    // 添加/修改描述符的事件健监控
    void updateChannelEvlp(Channel* channel) { _poller.updateChannelPl(channel); }
    // 移除描述符的事件监控
    void removeChannelEvlp(Channel* channel) { _poller.removeChannelPl(channel); }       
    
    void timerAdd(uint64_t id, uint32_t timeout, const taskFunc& cb) { _timer_wheel.timerAdd(id, timeout, cb); }
    void timerRefresh(uint64_t id) { _timer_wheel.timerRefresh(id); }
    void timerCancel(uint64_t id) { _timer_wheel.timerCancel(id); }
    bool isInTimer(uint64_t id) { return _timer_wheel.isInTimer(id); }
    
};
// ===========================================================================================================

// ================================ LoopThread LoopThreadPool ================================================
class LoopThread{
private:
    // 实现_loop获取的同步关系，避免线程创建了，但是_loop还没有实例化的情况
    std::mutex _mutex;
    std::condition_variable _cond;
    // 必须先有线程，再有EventLoop对象
    // EventLoop对象必须在线程中创建，否则会出现跨线程访问EventLoop对象
    std::thread _thread;    // EventLoop所在的线程
    EventLoop* _loop;       // EventLoop对象

private:
    // 实例化EventLoop对象，唤醒_cond上可能阻塞的线程，并开始运行EventLoop的功能
    // 线程入口函数
    void threadEntry() {
        EventLoop loop;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _loop = &loop;      // _loop 是指针
            _cond.notify_one();
        }
        loop.Loop();
    }
public:
    // 构造函数，创建线程，并等待EventLoop对象实例化
    LoopThread() : _loop(nullptr), _thread(std::bind(&LoopThread::threadEntry, this)) {}
    // 返回当前线程关联的EventLoop对象指针
    EventLoop* getLoop() {
        EventLoop* loop = nullptr;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cond.wait(lock,[&]() { return _loop != nullptr; });    // loop为空就一直阻塞
            loop = _loop;
        }
        return loop;
    }
};

class LoopThreadPool{
private:
    // 管理从属线程的创建和销毁，分配连接到线程上
    int _threadCnt;                     // 从属线程数量
    int _nextIdx;                       // 下一个线程的索引
    EventLoop* _baseLoop;               // 主线程的EventLoop对象
    std::vector<LoopThread*> _threads;  // 保存所有LoopThread对象
    std::vector<EventLoop*> _loops;     // 从属线程数量大于0，从loops中分配
public:
    LoopThreadPool(EventLoop* baseloop) :_threadCnt(0), _nextIdx(0), _baseLoop(baseloop) {}
    void setThreadCnt(int cnt) { _threadCnt = cnt;}
    void createThreads() {
        if(_threadCnt > 0) {
            _threads.resize(_threadCnt);
            _loops.resize(_threadCnt);
            for(int i = 0; i < _threadCnt; ++i) {
                _threads[i] = new LoopThread();
                _loops[i] = _threads[i]->getLoop();
            }
        }
    }

    EventLoop* getNextLoop() {
        if(0 == _threadCnt) return _baseLoop;
        _nextIdx = (_nextIdx + 1) % _threadCnt;
        return _loops[_nextIdx];
    }
};

// ===========================================================================================================

// ========================================== Any ================================================
class Any {
private:
    class holder {
    public:
        virtual ~holder() {}
        virtual const std::type_info& type() const = 0;
        virtual holder* clone() = 0;
    };
    template <class T>
    class placeHolder : public holder {
    public:
        T _val;
        placeHolder(const T& val) : _val(val) {}
        virtual const std::type_info& type() const { return typeid(T); }
        virtual holder* clone() { return new placeHolder(_val); }
    };

    holder* _content;
public:
    Any() :_content(nullptr) {}
    template <class T>
    Any(const T& val) :_content(new placeHolder<T>(val)) {}
    Any(const Any& other) :_content(other._content? other._content->clone() : nullptr) {}
    ~Any() { delete _content; }

    Any& swap(Any& other) {
        std::swap(_content, other._content);
        return *this;
    }

    // 返回子类对象保存的数据的指针
    template <class T>
    T* getPtr() {
        // 类型检查
        assert(typeid(T) == _content->type());
        // 先强转成placeHolder<T>指针，再取值，最后取地址
        return &((placeHolder<T>*)_content)->_val;
    }

    template <class T>
    Any& operator=(const T& val) {
        //为val构造一个临时的通用容器，然后与当前容器自身进行指针交换，临时对象释放的时候，原先保存的数据也就被释放
        Any(val).swap(*this);
        return *this;
    }

    Any& operator=(const Any& other) {
        Any(other).swap(*this);
        return *this;
    }
};

// ===========================================================================================================


// ================================ Connection ===============================================================
typedef enum {
    DISCONNECTED,       // 连接关闭状态
    CONNECTING,         // 正在连接状态，待处理连接事件
    CONNECTED,          // 连接成功状态，可以通信
    DISCONNECTING       // 正在断开连接状态
}ConnectionStatus;
class Connection;
using ptrConnection = std::shared_ptr<Connection>;
class Connection : public std::enable_shared_from_this<Connection> {
private:
    uint64_t _connId;                 // 连接的唯一ID，便于连接的管理和查找，也作为唯一的定时器ID
    int _sockFd;                      // 连接关联的socket描述符
    bool _enableInactiveRelease;      // 是否允许连接处于非活动状态时自动释放
    EventLoop* _loop;                 // 连接所在的EventLoop对象    
    ConnectionStatus _status;         // 连接状态
    Socket _socket;                   // 连接关联的Socket对象
    Channel _channel;                 // 连接关联的Channel对象，管理连接的事件
    Buffer _inBuffer;                 // 输入缓冲区，存放从socket中读到的数据
    Buffer _outBuffer;                // 输出缓冲区，存放待发送的数据
    // 请求只接受到了一半，要保存上一次处理到了哪里
    Any _context;                     // 保存上下文信息，用于请求的处理

    // 以下的回调函数，是服务器模块设置的，用于处理连接的各种事件
    // 在某个阶段事件发生时才会被调用

    // 连接建立阶段
    using connectedCallBack = std::function<void(const ptrConnection&)>;
    // 业务处理阶段
    using messageCallBack = std::function<void(const ptrConnection&, Buffer*)>;
    // 连接关闭状态
    using closedCallBack = std::function<void(const ptrConnection&)>;
    // 任意事件触发阶段
    using anyEventCallBack = std::function<void(const ptrConnection&)>;

    connectedCallBack _connectedCallBack;
    messageCallBack _messageCallBack;
    // 用户设置的业务关闭回调
    closedCallBack _closedCallBack;
    // 组件内部的连接关闭回调：组件内设置，服务器组件会把所有的连接管理起来，一旦某个连接要关闭
    // 就调用这个回调，从管理的地方移除自己的信息，通知组件内的其他模块，连接已经关闭，可以进行清理工作
    closedCallBack _serverCloseCallBack;
    anyEventCallBack _anyEventCallBack;

private:
    // 5个Channel的事件回调
    // 描述符可读事件触发后的回调，接收socket数据放到缓冲区中，然后调用事件处理回调
    void handleRead() {
        // 1. 接收socket的数据，放到缓冲区
        char buf[65536];
        ssize_t ret = _socket.RecvNonblock(buf, sizeof(buf) - 1);
        if(ret < 0) {
            // 出错了不能关闭！
            // 看一下还有没有待处理的数据
            shutdownInLoop();
        }
        // 0表示没读到数据，-1表示连接断开
        // 将数据放入输入缓冲区，写入之后要移动指针！
        _inBuffer.WriteAndMove(buf, ret);

        // 2. 调用messageCallBack处理输入缓冲区中的数据
        if(_messageCallBack) {
            // shared_from_this：从当前对象自身获取自身的shaerd_ptr管理对象
            _messageCallBack(shared_from_this(), &_inBuffer);
        }
    }

    // 描述符可写触发后的回调，将发送缓冲区的数据进行发送
    void handleWrite() {
       ssize_t ret = _socket.SendNonblock(_outBuffer.getReadIndex(), _outBuffer.getReadableSize()); 
       if(ret < 0) {
            if(_inBuffer.getReadableSize() > 0) {
                _messageCallBack(shared_from_this(), &_inBuffer);
            }
            Release();
       }
       _outBuffer.moveReadOffset(ret);
       if(0 == _outBuffer.getReadableSize()) {
            _channel.disableWrite();    // 没有数据待发送了，关闭写事件监控
            // 如果当前连接待关闭，有数据，发完数据再释放，没有数据直接释放
            if(_status == DISCONNECTING) {
                Release();
            }
       }
    }

    // 描述符挂断触发的回调
    void handleClose() {
        // 一旦连接挂断，套接字啥都不干了，有数据就处理一下，处理完就关闭连接
        if(_inBuffer.getReadableSize() > 0) {
            _messageCallBack(shared_from_this(), &_inBuffer);
        }
        Release();
    }

    // 描述符错误触发的回调
    void handleError() { handleClose(); }

    // 描述符触发任意事件的回调: 1. 刷新连接活跃度--延迟定时销毁任务 2.调用组件使用者的任意事件回调
    void handleAnyEvent() {
        if(_enableInactiveRelease == true) { _loop->timerRefresh(_connId); }
        if(_anyEventCallBack) { _anyEventCallBack(shared_from_this()); }
    }

    // 连接获取后，所处的状态下要进行各种设置
    void eastablishedInLoop() {
        // 1. 修改连接状态      2. 启动读事件监控       3. 调用回调
        assert(_status == CONNECTING);  // 必须要是待处理状态，半连接
        // 一旦启动读事件监控就有可能会理解触发读事件，而如果这时候启动了非活跃连接销毁
        // 会刷新活跃度，延迟销毁
        // 所以启动读事件不能放到构造里
        _channel.enableRead();
        if(_connectedCallBack) _connectedCallBack(shared_from_this());
    }

    
    // 启动非活跃连接超时释放规则
    void enableInactiveReleaseInLoop(int sec) {
        // 1. _enableInactiveRelease为true，表示允许非活跃连接自动释放
        _enableInactiveRelease = true;
        // 2. 如果当前定时销毁任务已存在，再刷新延迟一次即可
        if(_loop->isInTimer(_connId)) {
            _loop->timerRefresh(_connId);
        }
        // 3. 如果不存在定时销毁任务，新增
        _loop->timerAdd(_connId, sec, std::bind(&Connection::Release, this));
    }

    // 取消非活跃连接超时释放规则
    void cancelInactiveReleaseInLoop() {
        _enableInactiveRelease = false;
        if(_loop->isInTimer(_connId)) {
            _loop->timerCancel(_connId);
        }
    }

    // 更新协议
    void upgradeInLoop(const Any& context,
                const connectedCallBack& conn,
                const messageCallBack& msg,
                const closedCallBack& close,
                const anyEventCallBack& any) {
        _context = context;
        _connectedCallBack = conn;
        _messageCallBack = msg;
        _closedCallBack = close;
        _anyEventCallBack = any;
    }

    // 实际释放接口
    void RealseInLoop() {
        // 1. 修改连接状态
        _status = DISCONNECTED;
        // 2. 关闭读写事件监控
        _channel.Remove();
        // 3. 关闭描述符
        _socket.Close();
        // 4. 如果当前定时器队列还有定时销毁任务，取消任务
        if(_loop->isInTimer(_connId)) cancelInactiveReleaseInLoop();
        // 5. 调用用户关闭回调，避免先移除服务器管理的连接信息导致Connection被释放，再去处理会出错的情况
        if(_closedCallBack) _closedCallBack(shared_from_this());
        // 6. 调用服务器组件的关闭回调，通知组件内的其他模块，连接已经关闭，可以进行清理工作
        if(_serverCloseCallBack) _serverCloseCallBack(shared_from_this());
    }

    // 把数据放到发送缓冲区，启动可写事件监控
    void sentInLoop(Buffer& buf) {
        if(_status == DISCONNECTED) return;
        _outBuffer.writeBufferAndMove(buf);
        if(_channel.isWriteEnabled() == false) {
            _channel.enableWrite();
        }
    }

    // 设置状态为DISCONNECTING，启动关闭事件监控
    // 需要判断有无数据待处理
    void shutdownInLoop() {
        _status = DISCONNECTING;
        if(_inBuffer.getReadableSize() > 0) {
            if(_messageCallBack) _messageCallBack(shared_from_this(), &_inBuffer);
        }
        // 要么是写入数据时出错关闭，要么是没有待发送的数据，直接关闭
        if(_outBuffer.getReadableSize() > 0) {
            if(_channel.isWriteEnabled() == false) {
                _channel.enableWrite();
            }
        }
        if(0 == _outBuffer.getReadableSize()) {
            Release();
        }
    }


public:
    // 向外暴露的接口
    Connection(EventLoop* loop, uint64_t connId, int sockFd) :_connId(connId), _sockFd(sockFd) 
        , _enableInactiveRelease(false), _loop(loop), _status(CONNECTING), _socket(sockFd) 
        , _channel(sockFd, loop) 
    {
        // 设置回调
        _channel.setReadCallback(std::bind(&Connection::handleRead, this));
        _channel.setCloseCallback(std::bind(&Connection::handleClose, this));
        _channel.setErrorCallback(std::bind(&Connection::handleError, this));
        _channel.setWriteCallback(std::bind(&Connection::handleWrite, this));
        _channel.setEventCallback(std::bind(&Connection::handleAnyEvent, this));
    }
    ~Connection() { DBG_LOG("realease connection %p", this); }

    // 获取管理的文件描述符
    int getFd() const { return _sockFd; }
    // 获取连接的唯一ID
    uint64_t getId() const { return _connId; }
    // 是否处于CONNECTED状态
    bool isConnected() const { return _status == CONNECTED; }
    // 设置上下文：连接建立完成时调用
    void setContext(const Any& context) { _context = context; }
    // 获取上下文
    Any* getContext() { return &_context; }

    // 设置回调
    void setConnectedCallBack(const connectedCallBack& cb) { _connectedCallBack = cb; }
    void setMessageCallBack(const messageCallBack& cb) { _messageCallBack = cb; }
    void setClosedCallBack(const closedCallBack& cb) { _closedCallBack = cb; }
    void setServerCloseCallBack(const closedCallBack& cb) { _serverCloseCallBack = cb; }
    void setAnyEventCallBack(const anyEventCallBack& cb) { _anyEventCallBack = cb; }

    // 连接建立就绪后，设置channel回调，启动读监控，调用_connectedCallBack
    void established() {
        _loop->runInLoop(std::bind(&Connection::eastablishedInLoop, this));
    }

    // 发送数据
    void Send(const char* data, size_t len) {
        // 外界传入的data可能是个临时空间，我们只把发送操作压入任务池
        Buffer buf;
        buf.WriteAndMove(data, len);
        _loop->runInLoop(std::bind(&Connection::sentInLoop, this, std::move(buf)));
    }

    // 提供给组件使用者的关闭接口，不是实际关闭，要判断是否有数据待处理
    void Shutdown() {
        _loop->runInLoop(std::bind(&Connection::shutdownInLoop, this));
    }
    void Release() {
        _loop->queueInLoop(std::bind(&Connection::RealseInLoop, this));
    }

    // 启动非活跃销毁，定义多长时间无通信就是非活跃，添加定时任务
    void enableInactiveRelease(int sec) {
        _loop->runInLoop(std::bind(&Connection::enableInactiveReleaseInLoop, this, sec));
    }
    // 取消非活跃销毁
    void cancelInactiveRelease() {
        _loop->runInLoop(std::bind(&Connection::cancelInactiveReleaseInLoop, this));
    }

    // 切换协议：重置上下文及阶段性回调处理函数
    // 这个接口必须在EvenetLoop线程中立即执行
    // 必须在线程内调用
    void Upgrade(const Any& context, const connectedCallBack& conn, const messageCallBack& msg, 
                const closedCallBack& close, const anyEventCallBack& any) {
        _loop->assertInLoop();
        _loop->runInLoop(std::bind(&Connection::upgradeInLoop, this, context, conn, msg, close, any));
    }

};

// ===========================================================================================================

// ================================ Acceptor =================================================================
class Acceptor {
private:
    Socket _socket;
    EventLoop* _loop;
    Channel _channel;

    using AcceptCallBack = std::function<void(int)>;
    AcceptCallBack _acceptCallBack;
private:
    void handleRead() {
        int sockFd = _socket.Accept();
        if(sockFd < 0) return;
        if(_acceptCallBack) _acceptCallBack(sockFd);
    }

    int createServer(int port) {
        bool ret = _socket.createServer(port);
        assert(ret == true);
        return _socket.getFd();
    }

public:
    Acceptor(EventLoop* loop, int port) : _loop(loop), _socket(createServer(port)),
            _channel(_socket.getFd(), loop) {
        _channel.setReadCallback(std::bind(&Acceptor::handleRead, this));
    }

    void setAcceptCallBack(const AcceptCallBack& cb) { _acceptCallBack = cb; }
    void Listen() { _channel.enableRead(); }
};

// ===========================================================================================================

// ================================ TcpServer ==============================================================
class TcpServer {
private:
    uint64_t _nextId;       // 自动增长的连接ID
    int _port;              // 监听端口
    int _timeout;           // 非活跃连接的统计时间--多长时间五通信就是非活跃连接
    bool _enableInactiveRelease;  // 是否允许非活跃连接自动释放
    EventLoop* _baseLoop;        // 主线程的EventLoop对象，负责监听事件的处理
    Acceptor _acceptor;     // 监听套接字
    LoopThreadPool _pool;   // 从属EventLoop线程池
    std::unordered_map<uint64_t, ptrConnection> _connMap;  // 连接管理表

    using ConnectedCallBack = std::function<void(const ptrConnection&)>;
    using MessageCallBack = std::function<void(const ptrConnection&, Buffer*)>;
    using ClosedCallBack = std::function<void(const ptrConnection&)>;   
    using AnyEventCallBack = std::function<void(const ptrConnection&)>;
    using Functor = std::function<void()>;

    ConnectedCallBack _connectedCallBack;
    MessageCallBack _messageCallBack;
    ClosedCallBack _closedCallBack;
    AnyEventCallBack _anyEventCallBack;

private:
    void runAfterInLoop(const Functor& task, int delay) {
        _nextId++;
        _baseLoop->timerAdd(_nextId, delay, task);
    }
    void removeConnection(const ptrConnection& conn) {
        int id = conn->getId();
        auto it = _connMap.find(id);
        if(it != _connMap.end()) 
            _connMap.erase(it);
    }

    // 为新连接构造一个Connection进行管理
    void newConnection(int sockFd) {
        _nextId++;
        ptrConnection conn(new Connection(_pool.getNextLoop(), _nextId, sockFd));
        conn->setConnectedCallBack(_connectedCallBack);
        conn->setMessageCallBack(_messageCallBack);
        conn->setAnyEventCallBack(_anyEventCallBack);
        conn->setServerCloseCallBack(std::bind(&TcpServer::removeConnection, this, conn));
        if(_enableInactiveRelease) conn->enableInactiveRelease(_timeout);
        conn->established();
        _connMap.insert(std::make_pair(_nextId, conn));
    }
public:
    TcpServer(int port) : _port(port), _nextId(0) 
        ,_enableInactiveRelease(false), 
        _acceptor(_baseLoop, port)
        ,_pool(_baseLoop)
    {
        _acceptor.setAcceptCallBack(std::bind(&TcpServer::newConnection, this, std::placeholders::_1));
        _acceptor.Listen();
    }

    void setThreadCnt(int cnt) { _pool.setThreadCnt(cnt); }
    void setConnectedCallBack(const ConnectedCallBack& cb) { _connectedCallBack = cb; }
    void setMessageCallBack(const MessageCallBack& cb) { _messageCallBack = cb; }
    void setClosedCallBack(const ClosedCallBack& cb) { _closedCallBack = cb; }
    void setAnyEventCallBack(const AnyEventCallBack& cb) { _anyEventCallBack = cb; }
    void setEnableInactiveRelease(int sec) { _timeout = sec; _enableInactiveRelease = true; }

    // 添加一个定时任务
    void runAfter(const Functor& task, int delay) {
        _baseLoop->runInLoop(std::bind(&TcpServer::runAfterInLoop, this, task, delay));
    }
    void Loop() { _pool.createThreads(); _baseLoop->Loop(); }
};


// ===========================================================================================================


void Channel::Update() {
    if (_loop) {
        _loop->updateChannelEvlp(this);
    }
}
void Channel::Remove() {
    if (_loop) {
        _loop->removeChannelEvlp(this);
    }
}

void TimerWheel::timerAdd(uint64_t id, uint32_t timeout, const taskFunc& cb) {
    _loop->runInLoop(std::bind(&TimerWheel::timerAddInWheel, this, id, timeout, cb));
}

void TimerWheel::timerRefresh(uint64_t id) {
    _loop->runInLoop(std::bind(&TimerWheel::timerRefreshInWheel, this, id));
}

void TimerWheel::timerCancel(uint64_t id) {
    _loop->runInLoop(std::bind(&TimerWheel::timerCancelInWheel, this, id));
}

class NetWork {
public:
    NetWork() {
        DBG_LOG("SIGPIE INIT");
        signal(SIGPIPE, SIG_IGN);
    }
};
static NetWork nw;