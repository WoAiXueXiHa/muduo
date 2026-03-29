#pragma once

#include <functional>
#include <cstdint>

#include <sys/epoll.h>

// 解决循环依赖问题，不要包含头文件，前置声明即可
class EventLoop;
class Channel {
    using eventCallBack = std::function<void()>;
private:
    int _fd;                    // 关注的事件描述符
    // 改成这样
    uint32_t _events;           // 上层期望内核监控的事件（位掩码）
    uint32_t _revents;          // 内核告诉上层实际就绪的事件（位掩码）
    EventLoop* _loop;           // 所属的事件循环

    // 五个回调
    eventCallBack _readCallback;
    eventCallBack _writeCallback;
    eventCallBack _errorCallback;
    eventCallBack _closeCallback;
    eventCallBack _anythingCallback;

public:
    Channel(int fd, EventLoop* loop) :_fd(fd), _events(0), _revents(0), _loop(loop){}
    int getFd() { return _fd; }
    uint32_t getEvents() { return _events; }
    uint32_t getRevents() { return _revents; }
    void setRevents(uint32_t revents) { _revents = revents; }

    void setReadCallBack(eventCallBack cb) { _readCallback = cb; }
    void setWriteCallBack(eventCallBack cb) { _writeCallback = cb; }
    void setErrorCallBack(eventCallBack cb) { _errorCallback = cb; }
    void setCloseCallBack(eventCallBack cb) { _closeCallback = cb; }
    void setAnyThingCallBack(eventCallBack cb) { _anythingCallback = cb; }

    /*
        位运算细节：
        1. 我想监控可读 |= EPOLLIN
        0000 0000 | 0000 0001 = 0000 0001
    
        2. 我想关闭某个事件 &= ~宏
        0000 0001 & 1111 1110 = 0000 0000

        3. 判断事件是否就绪 & 
        0000 0010 & 0000 0001 = 0000 0000 读事件没有就绪
    
    */
    // 是否监控了可读事件？
    bool isReadEvent() { return _events & EPOLLIN; }
    // 是否监控了可写事件？
    bool isWriteEvent() { return _events & EPOLLOUT; }

    // 为什么要Update？把_events的变化同步给内核
    // _events只是Channel内部的变量，内核并不知道它是否变化了
    // 必须调用epoll_ctl(EPOLL_CTL_MOD, fd, &event)才能告诉内核：这个fd现在监控EPOLLIN
    void enableRead() { _events |= EPOLLIN; Update(); }
    void enableWrite() { _events |= EPOLLOUT; Update(); }
    void disableRead() { _events &= ~EPOLLIN; Update(); }
    void disableWrite() { _events &= ~EPOLLOUT;Update(); }
    void disableAll() { _events = 0; Update(); }

    // 为什么只声明？
    // 函数实现要调用EventLoop的方法，而现在EventLoop.hpp还没有包含
    // 如果在Channel.hpp里实现，编译器就不知道EvetLoop内部有什么方法了
    // 实现放到EventLoop.hpp的末尾就好
    void Update();
    void Remove();

    // 事件处理，一旦连接触发了事件，就调用这个函数，这个函数根据触发事件类型，进行不同的回调
    void handleEvent() {
        // EPOLLIN 正常可读     EPOLLHUP 对端关闭连接，需要检查缓冲区的残留数据     EPOLLPRI 带外数据
        if((_revents & EPOLLIN) || (_revents & EPOLLHUP) || (_revents & EPOLLPRI)) {
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
        // 任意事件触发，调用任意回调，目的是刷新连接活跃度
        if(_anythingCallback) _anythingCallback();
    }
};