#pragma once
#include <cassert>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#include <memory>
#include "Log.hpp"
#include "Channel.hpp"
#include "Poller.hpp"


class channel;
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

    Poller _poller;                                 // 对所有描述符的事件监控

private:
    static int CreateEventfd();
    void ReadWakeupFd();  // 读取_wakeup_fd事件通知次数
    void RunAllTasks();   // 执行任务池中的所有任务
public:
    EventLoop() :_thread_id(std::this_thread::get_id())
                ,_wakeup_fd(CreateEventfd()) 
                ,_wakeup_channel(new Channel(_wakeup_fd, this)) 
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


    ~EventLoop();
};


// 移除监控，把红黑树上这个节点删除掉
void Channel::Remove() { _loop->RemoveEvent(this); }
// 更新监控
void Channel::Update() { _loop->UpdateEvent(this); }