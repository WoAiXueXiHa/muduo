# 核心事件派发层架构

## 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        TcpServer                                 │
│  (连接管理、业务回调设置)                                         │
└────────────────┬────────────────────────────────────────────────┘
                 │
        ┌────────┴────────┐
        │                 │
   ┌────▼─────┐    ┌─────▼──────────┐
   │ Acceptor  │    │ LoopThreadPool │
   │(监听连接) │    │  (工作线程池)   │
   └────┬─────┘    └─────┬──────────┘
        │                │
        │          ┌─────┴──────────────┐
        │          │                    │
   ┌────▼──────────▼──┐    ┌───────────▼────┐
   │   EventLoop       │    │  LoopThread    │
   │  (主线程事件循环) │    │ (工作线程)     │
   └────┬─────────────┘    └───────┬────────┘
        │                          │
        │                    ┌─────▼──────────┐
        │                    │  EventLoop     │
        │                    │ (工作线程循环) │
        │                    └────────────────┘
        │
   ┌────▼──────────────────────────────────┐
   │         Poller (epoll 包装)            │
   │  - epfd (红黑树根)                     │
   │  - events[] (活跃事件数组)             │
   │  - _channels map (fd -> Channel)      │
   └────┬──────────────────────────────────┘
        │
   ┌────▼──────────────────────────────────┐
   │    Channel (文件描述符代理)             │
   │  - fd                                  │
   │  - events (期望监控的事件)             │
   │  - revents (实际就绪的事件)            │
   │  - 5个回调函数                         │
   └────────────────────────────────────────┘
```

---

## 核心类详解

### 1. **Channel** - 文件描述符的代理

**职责**：

- 管理单个文件描述符的事件监控
- 存储该 fd 的事件回调函数
- 当 epoll 返回该 fd 就绪时，触发对应回调

**核心成员**：

```cpp
int _fd;                          // 文件描述符
Poller* _poller;                  // 所属的 Poller（已改为 EventLoop*）
uint32_t _events;                 // 用户期望监控的事件（EPOLLIN | EPOLLOUT）
uint32_t _revents;                // epoll 返回的实际就绪事件

// 5个回调函数
EventCallback _readCallback;      // 可读事件
EventCallback _writeCallback;     // 可写事件
EventCallback _closeCallback;     // 连接关闭
EventCallback _errorCallback;     // 错误事件
EventCallback _eventCallback;     // 任意事件
```

**关键方法**：

- `enableRead() / disableRead()`：启动/停止读事件监控
- `enableWrite() / disableWrite()`：启动/停止写事件监控
- `handleEvent()`：根据 `_revents` 触发对应回调

**数据流**：

```
用户设置回调 → Channel 存储 → Poller 监控 → epoll 返回 → 
Channel::handleEvent() 触发回调
```

---

### 2. **Poller** - epoll 的包装

**职责**：

- 封装 Linux epoll 系统调用
- 管理所有 Channel 的注册/修改/删除
- 调用 epoll_wait 获取活跃事件

**核心成员**：

```cpp
int _epfd;                                    // epoll 文件描述符
struct epoll_event _events[MAX_EPOLLEVENTS]; // 活跃事件数组（内核吐出）
std::unordered_map<int, Channel*> _channels; // fd -> Channel 映射表
```

**关键方法**：

- `updateChannelPl(Channel*)`：添加或修改 Channel 的事件监控
- `removeChannelPl(Channel*)`：移除 Channel 的事件监控
- `Poll(vector<Channel*>* active)`：调用 epoll_wait，返回活跃 Channel 列表

**数据流**：

```
Channel 请求监控 → Poller::updateChannelPl() → epoll_ctl() 注册到内核
                                                    ↓
                                            epoll_wait() 阻塞等待
                                                    ↓
                                            内核返回活跃事件
                                                    ↓
                                    Poller 填充 _events[]
                                                    ↓
                                    返回活跃 Channel 列表
```

---

### 3. **EventLoop** - 事件循环核心

**职责**：

- 运行事件循环（Poll → 处理事件 → 执行任务）
- 管理 Poller 和所有 Channel
- 处理跨线程任务队列
- 管理定时器

**核心成员**：

```cpp
std::thread::id _threadId;              // 当前 EventLoop 所在线程 ID
int _eventFd;                           // eventfd（用于跨线程唤醒）
std::unique_ptr<Channel> _eventChannel; // eventfd 的 Channel
Poller _poller;                         // epoll 包装
std::vector<Functor> _tasks;            // 跨线程任务队列
std::mutex _mutex;                      // 保护 _tasks
TimerWheel _timer_wheel;                // 定时器
```

**关键方法**：

- `Loop()`：主事件循环
- `runInLoop(Functor)`：在本线程执行任务（如果已在本线程则立即执行，否则入队）
- `queueInLoop(Functor)`：任务入队并唤醒 epoll_wait
- `updateChannelEvlp(Channel*)`：委托给 Poller 更新 Channel
- `removeChannelEvlp(Channel*)`：委托给 Poller 移除 Channel

**事件循环流程**：

```
Loop() {
    while(true) {
        // 1. 事件监控：阻塞等待活跃事件
        Poller::Poll(&actives);
        
        // 2. 事件处理：触发每个活跃 Channel 的回调
        for(Channel* ch : actives) {
            ch->handleEvent();
        }
        
        // 3. 执行任务：处理跨线程任务队列
        runAllTasks();
    }
}
```

---

### 4. **TimerWheel** - 定时器

**职责**：

- 管理定时任务
- 使用时间轮算法（60 个槽位，每秒转一格）
- 通过 timerfd 实现秒级精度

**核心成员**：

```cpp
int _tick;                                    // 秒针位置（0-59）
int _capacity;                                // 容量（60）
std::unordered_map<uint64_t, weakTask> _timers; // 任务 ID -> 任务
std::vector<std::vector<ptrTask>> _wheel;    // 时间轮数组

int _timerFd;                                 // timerfd 文件描述符
std::unique_ptr<Channel> _timerChannel;      // timerfd 的 Channel
EventLoop* _loop;                            // 所属 EventLoop
```

**关键方法**：

- `timerAdd(id, timeout, callback)`：添加定时任务
- `timerRefresh(id)`：刷新任务（延迟执行）
- `timerCancel(id)`：取消任务

**时间轮工作原理**：

```
添加任务：
  pos = (_tick + timeout) % 60
  _wheel[pos].push_back(task)

每秒触发一次（timerfd 可读）：
  _tick = (_tick + 1) % 60
  执行 _wheel[_tick] 中的所有任务
  清空 _wheel[_tick]
```

---

### 5. **LoopThread** - 线程 + EventLoop 绑定

**职责**：

- 在独立线程中创建并运行 EventLoop
- 提供线程安全的 EventLoop 获取接口

**核心成员**：

```cpp
std::mutex _mutex;
std::condition_variable _cond;
std::thread _thread;        // 工作线程
EventLoop* _loop;           // EventLoop 对象指针
```

**关键方法**：

- `getLoop()`：获取 EventLoop 指针（阻塞等待直到 EventLoop 创建完成）

**创建流程**：

```
LoopThread 构造
    ↓
创建线程，执行 threadEntry()
    ↓
threadEntry() 中创建 EventLoop 对象
    ↓
通知 _cond，唤醒等待的 getLoop()
    ↓
开始运行 loop.Loop()
```

---

### 6. **LoopThreadPool** - 线程池

**职责**：

- 管理多个 LoopThread
- 负载均衡分配连接到不同线程

**核心成员**：

```cpp
int _threadCnt;                 // 工作线程数量
int _nextIdx;                   // 下一个分配的线程索引
EventLoop* _baseLoop;           // 主线程 EventLoop
std::vector<LoopThread*> _threads;  // 所有 LoopThread 对象
std::vector<EventLoop*> _loops;     // 所有 EventLoop 指针
```

**关键方法**：

- `setThreadCnt(int cnt)`：设置线程数量
- `createThreads()`：创建所有线程
- `getNextLoop()`：轮询返回下一个 EventLoop

**负载均衡**：

```
getNextLoop() {
    if(threadCnt == 0) return baseLoop;  // 没有工作线程，用主线程
    _nextIdx = (_nextIdx + 1) % _threadCnt;
    return _loops[_nextIdx];  // 轮询分配
}
```

---

## 数据流转示例

### 场景：新连接到达

```
1. Acceptor 监听到新连接
   ↓
2. Acceptor::handleRead() 调用 accept()
   ↓
3. TcpServer::newConnection() 创建 Connection
   ↓
4. Connection 创建 Channel，设置读/写/关闭回调
   ↓
5. Connection::established() 调用 channel.enableRead()
   ↓
6. Channel::enableRead() 调用 EventLoop::updateChannelEvlp()
   ↓
7. EventLoop 委托给 Poller::updateChannelPl()
   ↓
8. Poller 调用 epoll_ctl(EPOLL_CTL_ADD) 注册到内核
   ↓
9. 下一次 epoll_wait() 返回该 fd 可读
   ↓
10. Poller::Poll() 返回活跃 Channel 列表
    ↓
11. EventLoop::Loop() 调用 channel->handleEvent()
    ↓
12. Channel::handleEvent() 触发 _readCallback
    ↓
13. Connection::handleRead() 读取数据到 _inBuffer
    ↓
14. 调用用户设置的 messageCallback 处理业务
```

---

## 关键设计点

### 1. **跨线程安全**

- EventLoop 记录自己的线程 ID
- `runInLoop()` 检查是否同线程
- 跨线程任务通过 eventfd 唤醒 epoll_wait

### 2. **事件驱动**

- 所有 I/O 都是非阻塞的
- 通过 epoll 高效监控多个 fd
- 事件就绪时立即触发回调

### 3. **定时器精度**

- 使用 timerfd 实现秒级精度
- 时间轮算法 O(1) 添加/删除/刷新
- 避免了堆排序的开销

### 4. **线程池负载均衡**

- 主线程处理监听事件
- 工作线程处理连接事件
- 轮询分配新连接到工作线程

---

## 总结


| 类                  | 职责       | 核心成员                              |
| ------------------ | -------- | --------------------------------- |
| **Channel**        | fd 事件代理  | fd, events, revents, 5个回调         |
| **Poller**         | epoll 包装 | epfd, events[], _channels map     |
| **EventLoop**      | 事件循环核心   | Poller, eventfd, 任务队列, TimerWheel |
| **TimerWheel**     | 定时器      | 时间轮数组, timerfd, _tick             |
| **LoopThread**     | 线程绑定     | thread, EventLoop*                |
| **LoopThreadPool** | 线程池      | vector<LoopThread*>, _nextIdx     |


下一步：实现这些类，从 Channel 开始。