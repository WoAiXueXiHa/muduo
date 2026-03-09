#pragma once
#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include <ctime>
#include <functional>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <typeinfo>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

// ===================================== 日志 ========================================================
#define INFO  0
#define DEBUG 1
#define ERR   2
#define LOG_LEVEL DEBUG

#define LOG(level, format, ...) do { \
    if(level < LOG_LEVEL) break; \
        time_t t = time(nullptr); \
        struct tm tm_info; \
        localtime_r(&t, &tm_info); \
        char time_buffer[32] = { 0 }; \
        strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", &tm_info); \
        fprintf(stdout, "[%p %s %s:%d] " format "\n", \
                (void*)pthread_self(), time_buffer, __FILE__, __LINE__, ##__VA_ARGS__); \
} while(0)

#define LOG_DEBUG(format, ...) LOG(DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  LOG(INFO, format, ##__VA_ARGS__)
#define LOG_ERR(format, ...)   LOG(ERR, format, ##__VA_ARGS__)

// ================================================================================================


// ==================================== 缓冲区 =====================================================
#define BUFFER_SIZE 1024        
class Buffer {
private:
    std::vector<char> _buf;
    uint64_t _readPos;     // 读位置偏移量
    uint64_t _writePos;    // 写位置偏移量
public:
    Buffer() :_buf(BUFFER_SIZE), _readPos(0), _writePos(0) {}
    // 获取vector底层数组首地址
    // 解引用访问第一个元素
    // 取地址获取首地址，数组名就是首地址
    char* Begin() { return &(*_buf.begin()); }
    // 获取当前写位置
    char* GetWritePos() { return Begin() + _writePos; }
    // 获取当前读位置
    char* GetReadPos() { return Begin() + _readPos; }    
    // 获取缓冲区末尾空闲空间大小：写偏移之后的空间，总体空间-写偏移
    uint64_t GetTailSize() { return _buf.size() - _writePos; }
    // 获取缓冲区起始空闲空间大小：读偏移之前的空间
    uint64_t GetHeadSize() { return _readPos; }
    // 获取可读数据大小
    uint64_t GetReadableSize() { return _writePos - _readPos; }

    // 读偏移向后移动
    void MoveReadPos(uint64_t len) { 
        if(0 == len) return;
        // 向后移动的大小必须小于可读数据大小
        assert(len <= GetReadableSize());
        _readPos += len; 
    }
    // 写偏移向后移动
    void MoveWritePos(uint64_t len) {
        // 向后移动的大小必须小于缓冲区末尾空闲空间大小
        assert(len <= GetTailSize());
        _writePos += len;
    }   

    // 确保可写空间足够，够了移动数据，不够扩容
    void EnsureWriteable(uint64_t len) {
        // 如果缓冲区末尾空闲空间足够，直接返回
        if(GetTailSize() >= len) return;

        // 末尾空间不够，判断加上起始位置的空闲空间大小是否足够，够了就把数据移动到起始位置
        if(len <= GetHeadSize() + GetTailSize()) {
            // 移动数据到起始位置
            uint64_t rsz = GetReadableSize();   // 保存当前数据大小
            // template <class InputIterator, class OutputIterator>
            //    OutputIterator copy (InputIterator first, InputIterator last, OutputIterator result);
            // 将[first,last)区间内的元素复制到以result为起始位置的区间中，返回一个指向result区间最后一个元素之后位置的迭代器
            std::copy(GetReadPos(), GetReadPos() + rsz, Begin());  
            // 非常重要！数据从起始位置开始了！
            // 读偏移归0，写偏移等于数据大小
            _readPos = 0;
            _writePos = rsz;
        } else {
            // 末尾空间和起始位置的空闲空间加起来都不够了，扩容
            LOG_DEBUG("Buffer扩容 %zu\n", _buf.size() + len);
            _buf.resize(_writePos + len);
        }
    }

    // 写入数据后移动指针
    void Write(const void* data, uint64_t len) {
        if(0 == len) return;
        EnsureWriteable(len);
        const char* d = (const char*)data;
        std::copy(d, d + len, GetWritePos());
        MoveWritePos(len);
    }
    void WriteString(const std::string& data) {
        Write(data.c_str(), data.size());
    }
    void WriteBuffer(Buffer& data) {
        uint64_t len = data.GetReadableSize();
        if(0 == len) return;
        EnsureWriteable(len);
        std::copy(data.GetReadPos(), data.GetReadPos() + len, GetWritePos());
        MoveWritePos(len);
        data.MoveReadPos(len);  // 移动源数据的读偏移，表示数据被消费了
    }

    // 读取数据后移动指针
    void Read(void* data, uint64_t len) {
        if(0 == len) return;
        assert(len <= GetReadableSize());
        char* d = (char*)data;  
        std::copy(GetReadPos(), GetReadPos() + len, d);
        MoveReadPos(len);
    }
    std::string ReadString(uint64_t len) {
        assert(len <= GetReadableSize());
        std::string s(len, '\0');  // 创建一个长度为len的字符串，内容初始化为'\0'
        Read(&s[0], len);  // 读取数据到字符串的缓冲区
        return s;
    }

    // 查找换行符位置，返回指针
    char* FindCRLF() {
        // memchr(const void *str, int c, size_t n) 
        // 在字符串的前n个字节中搜索第一次出现字符c的位置，返回指向该位置的指针
        char* res = (char*)memchr(GetReadPos(), '\n', GetReadableSize());
        return res;
    }

    // 获取一行数据
    std::string GetLine() {
        char* pos = FindCRLF();
        if(nullptr == pos) {
            return "";
        }
        // +1 是为了把换行符也包含在返回的字符串中，表示一行数据完整了
        return ReadString(pos - GetReadPos() + 1); 
    }

    // 清空，不需要释放内存
    void Clear() {
        _readPos = 0;
        _writePos = 0;
    }
};

// ==================================== 缓冲区 =====================================================

// ==================================== 套接字 =====================================================
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
// ===============================================================================================

// ==================================== Channel =====================================================

class EventLoop;
class Channel {
    // 事件回调函数类型定义
    using EventCallback = std::function<void()>;
private:
    int _fd;
    // Poller* _poller;   // 后续要改成EventLoop对象定义的，Poller只是临时测试
    EventLoop* _loop;  // 改成EventLoop对象定义的
    uint32_t _events;  // 关注的事件
    uint32_t _revents; // 实际发生的事件
    // 这些回调函数都是Connection实现的，Channel只负责调用它们
    EventCallback _read_callback;           // 可读事件被触发时的回调函数
    EventCallback _write_callback;          // 可写事件被触发时的回调函数
    EventCallback _error_callback;          // 错误事件被触发时的回调函数
    EventCallback _close_callback;          // 连接关闭事件被触发时的回调函数
    EventCallback _event_callback;          // 事件被触发时的通用回调函数，可以根据实际发生的事件类型来调用不同的回调函数
public:
    // Channel只负责监控事件的发生，不负责事件的处理
    // 所以它不需要知道事件发生时应该调用哪个具体的回调函数，这些回调函数由Connection来设置和实现
    Channel(int fd, EventLoop* loop) : _fd(fd), _loop(loop),  _events(0), _revents(0) {}
    int GetFd() const { return _fd; }
    uint32_t GetEvents() const { return _events; }          // 获取想要监控的事件
    void SetEvents(uint32_t events) { _events = events; }   // 设置实际就绪的事件

    // 事件回调函数的设置接口
    void SetReadCallback(const EventCallback& cb) { _read_callback = cb; }
    void SetWriteCallback(const EventCallback& cb) { _write_callback = cb; }            
    void SetErrorCallback(const EventCallback& cb) { _error_callback = cb; }
    void SetCloseCallback(const EventCallback& cb) { _close_callback = cb; }
    void SetEventCallback(const EventCallback& cb) { _event_callback = cb; }

    // 当前是否监控了可读事件
    bool IsReading() const { return (_events & EPOLLIN); }
    // 当前是否监控了可写事件
    bool IsWriting() const { return (_events & EPOLLOUT); }
    
    // 启动就是挂到红黑树上了，等事件发生了就会调用HandleEvent来处理事件
    // 启动读事件监控
    void EnableReading() { _events |= EPOLLIN; /*后边会添加到EventLoop的事件监控中*/}
    // 启动写事件监控
    void EnableWriting() { _events |= EPOLLOUT; }
    
    // 停止只是停止监控，节点还在红黑树上
    // 停止读事件监控
    void DisableReading() { _events &= ~EPOLLIN; /*后边会修改到EventLoop的事件监控中*/}
    // 停止写事件监控
    void DisableWriting() { _events &= ~EPOLLOUT; }
    // 停止所有事件监控
    void DisableAll() { _events = 0; }

    // 移除监控，把红黑树上这个节点删除掉
    void Remove();
    // 更新监控
    void Update();

    // 事件处理，一旦连接触发了事件，就调用这个函数
    // 自己触发了什么事件如何处理，由Connection来实现
    void HandleEvent() {
        // 就绪可读事件、对端关闭连接事件、紧急数据事件
        if((_revents & EPOLLIN) || (_revents & EPOLLRDHUP) || (_revents & EPOLLPRI)) {
            // 直接用通用回调函数处理
            if(_read_callback) _read_callback();
            if(_event_callback) _event_callback();
        }
        // 有可能会释放连接的操作事件, 一次只处理一个
        if(_revents & EPOLLOUT) {
            if(_write_callback) _write_callback();
            if(_event_callback) _event_callback();  // 放到事件处理之后，刷新活跃度
        } else if(_revents & EPOLLERR) {
            if(_event_callback) _event_callback();  // 放到关闭连接之前，刷新活跃度
            if(_error_callback) _error_callback(); // 一旦出错，就要释放连接
        } else if(_revents & EPOLLRDHUP) {
            if(_event_callback) _event_callback();
            if(_close_callback) _close_callback();
        }

        // 不管任何事件发生了，都调用通用回调函数来处理，具体怎么处理由Connection来实现
        if(_event_callback) _event_callback();
    }
};

// =================================================================================================

// ==================================== Poller =====================================================
#define MAX_EPOLL_EVENTS 1024
class Channel;
class Poller {
private:
    int _epfd;  // epoll实例的文件描述符
    // struct epoll_event { 
    //     uint32_t events;  // 关注的事件类型
    //     epoll_data_t data; // 用户数据
    // };
    struct epoll_event _events[MAX_EPOLL_EVENTS];  // 存储就绪事件的数组
    std::unordered_map<int, Channel*> _channels;   // fd:Channel的映射表，方便通过fd找到对应的Channel对象

private:
    // 对epoll的直接操作，把操作命令给我
    void Update(Channel* channel, int op) {
        // int epoll_ctl(int epfd, int op, int fd,
        //               struct epoll_event *_Nullable event);
        int fd = channel->GetFd();          // 要监控的事件的文件描述符
        struct epoll_event ev;
        ev.data.fd = fd;                    // 将Channel的fd作为用户数据存储在epoll_event中
        ev.events = channel->GetEvents();   // 获取Channel关注的事件类型

        int ret = ::epoll_ctl(_epfd, op, fd, &ev);
        if(ret < 0) {
            LOG_ERR("epoll_ctl error: %s\n", strerror(errno));
        }
    }

    // 判断一个Channel是否添加了事件监控
    bool HasChannel(Channel* channel) {
        auto it = _channels.find(channel->GetFd());
        return it != _channels.end();
    }

public:
    Poller() {
        _epfd = ::epoll_create(MAX_EPOLL_EVENTS);
        if(_epfd < 0) {
            LOG_ERR("epoll_create error: %s\n", strerror(errno)); 
        }
    }

    // 添加或修改事件监控，没有就添加，有就修改
    void UpdateEvent(Channel* channel) {
        bool ret = HasChannel(channel);
        if(!ret) {
            // 不存在则添加
            _channels.insert(std::make_pair(channel->GetFd(), channel));
            Update(channel, EPOLL_CTL_ADD);
        }
        Update(channel, EPOLL_CTL_MOD);
    }

    // 移除监控
    void RemoveEvent(Channel* channel) {
        auto it = _channels.find(channel->GetFd());
        if(it != _channels.end()) {
            _channels.erase(it);
        }
        Update(channel, EPOLL_CTL_DEL);
    }

    // 开始监控，返回活跃连接的列表
    void Poll(std::vector<Channel*>* active_arr) {
        // int epoll_wait(int epfd, struct epoll_event *events,
        //              int maxevents, int timeout);
        // epfd是要监控的描述符，events是保存就绪事件的数组
        // maxevents是events数组的大小，timeout是超时时间，-1表示阻塞等待，0表示立即返回
        // 返回值是就绪事件的数量，0表示超时，-1表示出错
        int nfds = ::epoll_wait(_epfd, _events, MAX_EPOLL_EVENTS, -1);
        if(nfds < 0) {
            if(errno == EINTR) {
                // EINTR表示系统调用被信号中断了，这种情况不是错误，可以继续等待
                return;
            }
            LOG_ERR("epoll_wait error: %s\n", strerror(errno));
            abort();    // 退出程序
        }

        // nfds表示有多少个事件就绪了，遍历这些就绪事件，找到对应的Channel对象，并将它添加到活跃连接的列表中
        for(int i = 0; i < nfds; ++i) {
            auto it = _channels.find(_events[i].data.fd);
            assert(it != _channels.end());  // 确保这个fd对应的Channel对象存在
            // it->second是Channel对象的指针，_events[i].events是这个fd就绪的事件类型
            it->second->SetEvents(_events[i].events); 
            active_arr->push_back(it->second);  // 将这个Channel对象添加到活跃连接的列表中
        }
    }
};

// ==================================== Timer =====================================================


using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;
// 定时器任务对象
// 作用：保存定时器任务的ID、超时时间、是否取消、定时任务回调函数、删除TimerWheel中保存的定时器对象信息
class TimerTask {
private:
    uint64_t _id;               // 定时器任务对象ID
    uint32_t _timeout;          // 定时器超时时间
    bool _canceled;             // 定时器是否取消
    TaskFunc _task_cb;          // 定时器对象要执行的定时任务
    ReleaseFunc _release_cb;    // 删除TimerWheel中保存的定时器对象信息
public:
    TimerTask(uint64_t id, uint32_t delay, const TaskFunc& task_cb)
        :_id(id), _timeout(delay), _canceled(false), _task_cb(task_cb) {}

    ~TimerTask() {
        if(_canceled == false) _task_cb();
        _release_cb();
    } 
    
    void Cancel() { _canceled = true; }
    void SetRealseFunc(const ReleaseFunc& release_cb) { _release_cb = release_cb; }
    uint32_t GetTimeout() const { return _timeout; }
};

// 时间轮
class TimerWheel {
public:
private:
    // WeakTask 保存的是TimerTask的弱引用，
    // 防止TimerTask被释放后，TimerWheel中保存的TimerTask也被释放
    using WeakTask = std::weak_ptr<TimerTask>;
    // PtrTask 保存的是TimerTask的共享指针，
    // 防止TimerTask被释放后，TimerWheel中保存的TimerTask也被释放
    using PtrTask = std::shared_ptr<TimerTask>;

    int _tick;                  // 时间轮的刻度，走到哪里，就处理哪里的任务
    int _capacity;              // 表盘最大容量，最大延迟时间
    // 为啥用二维数组？
    std::vector<std::vector<PtrTask>> _wheel;  // 时间轮的表盘，每个表盘是一个队列
    std::unordered_map<uint64_t, WeakTask> _timers; 

    EventLoop* _loop;           // 定时器对象所有操作都在EventLoop中进行，不需要加锁
    int _timerfd;               // 定时器fd，用于通知时间轮
    std::unique_ptr<Channel> _timer_channel;  // 定时器事件通知通道

private:
    void RemoveTimer(uint64_t id) {
        auto it = _timers.find(id);
        if(it != _timers.end())  _timers.erase(it);   
    }

    static int CreateTimerfd() {
        // 创建定时器fd
        // timerfd_create(clockid_t clock_id, int flags)
        // CLOCK_MONOTONIC 系统时钟，不受系统时钟变化影响
        // 0 非阻塞模式
        int timerfd = ::timerfd_create(CLOCK_MONOTONIC, 0);
        if(timerfd < 0) {
            LOG_ERR("Failed in timerfd_create");
            abort();
        }

        // 设置定时器fd的超时时间
        // int timerfd_settime(int fd, int flags,
        // const struct itimerspec *new_value, struct itimerspec *old_value);
        struct itimerspec itime;
        itime.it_value.tv_sec = 1;
        itime.it_value.tv_nsec = 0;
        itime.it_interval.tv_sec = 1;   // 第一次超时时间为1s后
        itime.it_interval.tv_nsec = 0;  // 第一次超时后，每隔1s超时一次
        timerfd_settime(timerfd, 0, &itime, nullptr);
        return timerfd;
    }

    int ReadTimerfd() {
        uint64_t times;
        // 有可能因为其它描述符的时间处理花费时间过长，然后在处理定时器描述符事件时，
        // 就超时很多次了
        // read读到的数据times就是从上一次read之后的超时次数
        int ret = ::read(_timerfd, &times, sizeof(times));
        if(ret < 0) {
            LOG_ERR("Failed in read");
            abort();
        }
        return times;
    }

    // 这个函数应该每秒被执行一次，相当于秒针走一格
    void RunTimerTasks() {
        _tick = (_tick + 1) % _capacity;
        _wheel[_tick].clear();   // 清空当前刻度的任务队列
    }

    void OnTime() {
        // 根据实际超时的次数，执行对应的超时任务
        int times = ReadTimerfd();
        for(int i = 0; i < times; ++i) {
            RunTimerTasks();
        }
    }



    void TimerAddInLoop(uint64_t id, uint32_t delay, const TaskFunc& task_cb) {
        PtrTask ptr_task(new TimerTask(id, delay, task_cb));
        ptr_task->SetRealseFunc(std::bind(&TimerWheel::RemoveTimer, this, id));
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(ptr_task);
        _timers[id] = ptr_task;
    }

    
    void TimerRefreshInLoop(uint64_t id) {
        // 通过保存的定时器对象的weak_ptr，构造一个shared_ptr，添加到时间轮中
        auto it = _timers.find(id);
        if(it == _timers.end()) return;     // 没找到定时器任务，没法刷新和延迟

        PtrTask ptr_task = it->second.lock();
        int delay = ptr_task->GetTimeout();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(ptr_task);
    }


    void TimerCancelInLoop(uint64_t id) {
        auto it = _timers.find(id);
        if(it == _timers.end()) return;     // 没找到定时器任务，没法取消

        PtrTask ptr_task = it->second.lock();
        ptr_task->Cancel();
    }

public:
    TimerWheel(EventLoop* loop) :_capacity(60), _tick(0), _wheel(_capacity),_loop(loop)
        ,_timerfd(CreateTimerfd()), _timer_channel(new Channel(_timerfd, loop)) {
        _timer_channel->SetReadCallback(std::bind(&TimerWheel::OnTime, this));
        _timer_channel->EnableReading();    // 开启定时器fd的读事件监听
    }

    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc& task_cb);
    void TimerRefresh(uint64_t id);
    void TimerCancel(uint64_t id);

    bool HasTimer(uint64_t id) const {
        auto it = _timers.find(id);
        return it != _timers.end();
    }
};


// =============================================================================================


// ==================================== EventLoop =====================================================
class EventLoop {
private:
    using Functor = std::function<void()>;

    // 对于一个任务，如果本身在线程里，直接执行
    // 如果不在线程里，放到任务池里，等线程空闲了再执行
    std::thread::id _thread_id;                     // 线程ID  

    // ===================== 易错注意！ =======================================================
    // 声明顺序不能乱，_wakeup_channel依赖于_wakeup_fd，所以必须先声明_wakeup_fd
    int _wakeup_fd;                                 // 用于唤醒线程的文件描述符
    std::unique_ptr<Channel> _wakeup_channel;       // 用于监听_wakeup_fd的Channel对象
    // ===================== 易错注意！ =======================================================

    // 为啥用vector而不是queue？
    // 因为vector的内存是连续的，访问效率更高
    // 而queue底层是list，内存不连续，访问效率较低
    // 并且vector可以通过swap来快速清空
    // 而queue需要逐个弹出元素来清空
    std::vector<Functor> _tasks_poll;              // 任务池
    std::mutex _mutex;                             // 保护任务队列的互斥锁 

    TimerWheel _timer_wheel;                        // 时间轮
    Poller _poller;                                 // 对所有描述符的事件监控

private:
    // 为啥用static？
    // 因为eventfd只能在一个进程内使用，所以只需要一个全局的eventfd就可以了
    static int CreateEventfd() {
        int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if(efd < 0) {
            LOG_ERR("eventfd error: %s\n", strerror(errno));
            abort();
        }
        return efd;
    }

    // 读取_wakeup_fd事件通知次数
    void ReadWakeupFd() {
        uint64_t res = 0;
        int ret = ::read(_wakeup_fd, &res, sizeof(res));
        if(ret < 0) {
            // EINTR被信号打断      EAGAIN表示没有数据可读
            if(errno == EINTR || errno == EAGAIN) return;
            LOG_ERR("read _wakeup_fd error: %s\n", strerror(errno));
            abort();
        }
    }
    // 写入_wakeup_fd事件通知次数
    void WeakUpEventfd() {
        uint64_t val = 1;
        int ret = ::write(_wakeup_fd, &val, sizeof(val));
        if(ret < 0) {
            if(errno == EINTR) return;
            LOG_ERR("write _wakeup_fd error: %s\n", strerror(errno));
            abort();
        }
    }

    // 执行任务池中的所有任务
    void RunAllTasks() {
        std::vector<Functor> tasks;
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            tasks.swap(_tasks_poll);
        }
        for(auto& task : tasks) { 
            task();
        }
    }  

public:
    EventLoop() :_thread_id(std::this_thread::get_id())
                ,_wakeup_fd(CreateEventfd()) 
                ,_wakeup_channel(new Channel(_wakeup_fd, this)) 
                , _timer_wheel(this)
    {
        // 给_wakeup_channel设置可读事件回调函数，当_wakeup_fd可读时，读取_wakeup_fd事件通知次数
        _wakeup_channel->SetReadCallback(std::bind(&EventLoop::ReadWakeupFd, this));
        // 启动_wakeup_channel的读事件监控
        _wakeup_channel->EnableReading();
    }

    // 事件监控-》就绪时间处理-》执行回调函数
    void StartLoop(){
        while(1) {
            // 1. 监控事件的发生，获取就绪事件列表
            std::vector<Channel*> actives_list;
            _poller.Poll(&actives_list);
            // 2. 遍历就绪事件列表，调用对应的Channel对象的HandleEvent方法来处理事件
            for(auto& Channel: actives_list) {
                Channel->HandleEvent();
            }
            // 3. 执行任务池中的任务
            RunAllTasks();
        }
    }

    // 判断当前线程是否是EventLoop所在的线程
    bool IsInLoopThread() const { return _thread_id == std::this_thread::get_id(); }
    // 断言当前线程必须是EventLoop所在的线程，否则输出错误日志并终止程序
    void AssertInLoopThread() { assert(IsInLoopThread()); }

    // 压入任务池
    void QueueInLoop(const Functor& cb) {
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks_poll.push_back(cb);
        }
        // 唤醒有可能因为没有事件就绪，而导致的epoll阻塞
        // 即给_weakup_fd写入一个数据，
    }
    // 线程安全的向任务池中添加一个任务
    void RunInLoop(const Functor& cb) {
        if(IsInLoopThread()) {
            cb();   // 如果当前线程就是EventLoop所在的线程，直接执行任务
        } 
        QueueInLoop(cb);
    }

    // 添加/修改描述符的事件监控
    void UpdateEvent(Channel* channel) { return _poller.UpdateEvent(channel); }
    // 移除描述符的事件监控
    void RemoveEvent(Channel* channel) { return _poller.RemoveEvent(channel); }

    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc& task_cb) {
        return _timer_wheel.TimerAdd(id, delay, task_cb);
    }
    void TimerRefresh(uint64_t id) {
        return _timer_wheel.TimerRefresh(id);
    }
    void TimerCancel(uint64_t id) {
        return _timer_wheel.TimerCancel(id);
    }
    bool HasTimer(uint64_t id) const {
        return _timer_wheel.HasTimer(id);
    }
    ~EventLoop(){}
};


// 移除监控，把红黑树上这个节点删除掉
void Channel::Remove() { _loop->RemoveEvent(this); }
// 更新监控
void Channel::Update() { _loop->UpdateEvent(this); }


