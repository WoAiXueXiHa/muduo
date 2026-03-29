#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>
#include <sys/eventfd.h>
#include "./Poller.hpp"
#include "./Logging.hpp"
#include "Channel.hpp"


class EventLoop {
    using Functor = std::function<void()>;
private:
    int _eventfd;                               // 唤醒epoll_wait，必须在_eventChannel前
    std::thread::id _threadid;                  // 线程id，判断线程是否在当前EventLoop中
    Poller _poller;                             
    std::vector<Functor> _tasks;                // 任务池
    std::unique_ptr<Channel> _eventChannel;     // 专门给eventfd用的，初始化依赖eventfd
    std::mutex _mutex;                          // 锁

private:
    // 创建eventfd
    // 为什么static？ 构造函数初始化列表需要在this完全构造前调用它
    static int createEventfd() {
        // 非阻塞，关闭文件
        int ret = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (ret < 0) {
            LOG_ERROR("eventfd error: %s", strerror(errno));
            abort();
        }
        return ret;
    }

    // 内核维护了一个uint64_t 的计数器
    // 计数器的值表示有多少个事件发生
    // 读取eventfd的计数器
    void readEventfd() {
        uint64_t buf;
        int ret = ::read(_eventfd, &buf, sizeof(buf));
        if(ret < 0) {
            if(errno == EAGAIN || errno == EINTR) return;
            LOG_ERROR("read error: %s", strerror(errno));
            abort();
        }
    }

    // 唤醒eventfd，同时更新计数器
    void wakeupEventfd() {
        uint64_t val = 1;
        int ret = ::write(_eventfd, &val, sizeof(val));
        if(ret < 0) {
            if(errno == EINTR) return;
            LOG_ERROR("write error: %s", strerror(errno));
            abort();
        }
    }

    void runAllTasks() {
        std::vector<Functor> functors; 
        {
            // 加锁，保护任务池资源
            std::unique_lock<std::mutex> lock(_mutex);
            // 把任务换出来
            _tasks.swap(functors);
        }   // 解锁，其它线程可以向任务池中添加任务
        // 执行任务
        for(auto& f : functors) {
            f();
        }
    }

public:
  EventLoop(): _eventfd(createEventfd())
             , _threadid(std::this_thread::get_id())
             , _eventChannel(new Channel(_eventfd, this)) {
        // 给_eventChannel设置回调函数
        _eventChannel->setReadCallBack([this](){
            // 回调的目的是读走计数器，清空 eventfd
            readEventfd();
        });
        // 启动读事件监控
        _eventChannel->enableRead();
   }

  ~EventLoop() {
    ::close(_eventfd);
  }

  // 等待事件 -> 处理事件 -> 处理任务
  void loop() {
    while(true) {
        // 等待事件
        std::vector<Channel*> activeChannels;
        _poller.Poll(&activeChannels);

        // 处理事件
        for(auto& channel : activeChannels) {
            channel->handleEvent();
        }

        // 执行任务
        runAllTasks();
    }
  }

  bool isInLoop() const {
    return std::this_thread::get_id() == _threadid;
  }

  void runInLoop(Functor cb) { 
    bool ret = isInLoop();
    if(ret) cb();
    else queueInLoop(cb);
  }

  void queueInLoop(Functor cb) {
    {
        // 加锁，保护任务池资源
        std::unique_lock<std::mutex> lock(_mutex);
        _tasks.push_back(std::move(cb));   
    }
    wakeupEventfd();    // 唤醒epoll_wait，让EventLoop尽快跑任务
  }

  // 委托给Poller
  void updateChannel(Channel* channel) {
    _poller.updateChannel(channel);
  }
  void removeChannel(Channel* channel) {
    _poller.removeChannel(channel);
  }
};

// 类外定义
inline void Channel::Update() { _loop->updateChannel(this); }
inline void Channel::Remove() { _loop->removeChannel(this); }