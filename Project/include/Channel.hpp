#pragma once
#include <cstdint>
#include <functional>
#include <sys/epoll.h>
#include "Poller.hpp"

class Poller;
class Channel {
    // 事件回调函数类型定义
    using EventCallback = std::function<void()>;
private:
    int _fd;
    Poller* _poller;
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
    Channel(int fd, Poller* poller) : _fd(fd), _poller(poller),  _events(0), _revents(0) {}
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