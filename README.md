# Web_ChatRoom
基于http与websocket协议的C++网页多人实时聊天室服务
## 1.简介
本项目是 Linux 下 C++ 实现的网页端多人实时聊天室服务。
### （1）网络层
采用主从 Reactor 多线程模型，MainReactor 只负责监听接受新连接，已建立连接分发给 SubReactor 线程处理读写事件，充分利用多核 CPU，提升并发连接承载能力。每个 TCP 连接绑定独立 ring‑buffer，用于接收数据缓存、协议拆包，缓解内核缓冲区压力，处理 TCP 分片、粘包问题，解耦 IO 读取与业务解析逻辑。

### （2）工具层
提供实现服务所需的工具。异步日志功能，采用双缓冲区设计，独立线程写磁盘，有效减少日志输出带来的性能消耗及IO磁盘访问频率；双链表功能，基于linux内核双链表改装，实现简易队列功能，保证极小资源占用和极快的插入删除操作；打开文件功能，提供全局唯一的静态文件打开服务，仅启动时加载，后续请求直接通过接口获取从而避免频繁的磁盘访问；内存池，用于频繁变化的业务数据，可减少系统内存碎片的产生。

### （3）web应用层
实现请求解析响应与转发功能，基于http与websocket协议实现，采用多端口设计(http与websocket独立端口)，主要负责内容如下：
HTTP     ：提供网页静态页面，处理浏览器普通 HTTP 请求；
WebSocket：完成 HTTP 升级握手，建立长连接，实现浏览器与服务端全双工通信，服务端可主动向客户端推送聊天消息，实现群聊广播、在线状态通知；定时检测非活动连接，超时关闭。

## 2.效果显示
<img width="3391" height="1960" alt="image" src="https://github.com/user-attachments/assets/37cf26c5-0024-427e-abb1-a4cf65709f5a" />
支持1000+连接稳定低延时广播，如图
<img width="482" height="260" alt="image" src="https://github.com/user-attachments/assets/e6ce470b-df90-4ff0-a5a8-9399881785bd" />

## 3.编译环境搭建
```
# 1.安装依赖
sudo apt update
sudo apt install libssl-dev

# 2.编译
make

# 3.运行
./server
```

## 4.架构分析
该项目基于B/S架构组织，由浏览器作为应用客户端，服务器向浏览器提供数据交互服务。服务器对外提供8080和8888两个端口，8080端口提供html页面服务，8888端口负责http协议与websocket协议的转换，实现客户端与服务器的双工通信。服务器功能的实现依赖多个组件，主要包含NET、Web App、Util，交互示意图如下。
<img width="1031" height="659" alt="image" src="https://github.com/user-attachments/assets/020535ad-cc0a-47c6-b54b-61c94a916a51" />

### （1）环形缓冲区RingString
    TCP是面向“流”的传输协议，无消息边界，提供给应用的数据可能是不完整的帧，而应用期望每次拿到的数据是完整的帧。为解决这个问题，给每个TCP链接设置网络缓冲区，接收到完整一帧数据再交由应用处理。
本项目采用环形缓冲区RingString作为TCP连接缓冲区，RingString灵感来自std::string，std::string在*append=>erase*过程中触发数据前移，若使用std::string作为TCP缓冲区，频繁的数据传输会不断产生前移的动作。为省掉前移带来的开销，使用环形的“string”替代std::string。RingString实现了std::string的部分接口，支持自动扩容，其结构如下
<img width="1211" height="372" alt="image" src="https://github.com/user-attachments/assets/44fe8025-0b72-4be6-9628-56f48adcaeb1" />
r_ptr与w_ptr在active buffer中移动，代表了存储数据的位置，backup buffer与active buffer大小一致，发生卷绕时，将ative中数据拷贝到backup，向应用提供线性的数据缓冲区。

### （2）网络I/O Reactor
Reactor 是一种用于处理高并发 I/O 的事件驱动设计模式。它的核心思想是将 I/O 操作的事件分离出来，通过非阻塞 I/O 和事件循环机制，实现少量线程高效处理海量并发连接。本项目实现多Reactor，示意图如下。
<img width="1553" height="1214" alt="image" src="https://github.com/user-attachments/assets/ab8aefd2-26cf-4731-8cc7-c438a3cb2d3c" />
如上图，每个epoll对应一个线程，主线程仅负责监听并建立新连接，子线程则负责连接的网络IO及业务数据处理，该设计可达到网络高并发的目的。经实测，该模型在单线程时(连接建立与网络IO均在主线程)，压测QPS 3.2w+，4个I/O线程时，压测 QPS 10.1W+。

### （3）异步双缓冲区日志
实现了异步双缓冲区日志系统，提供格式化的日志输出功能，支持输出到屏幕与磁盘日志文件中。采用多线程双缓冲区设计，业务线程向缓冲区0输出日志数据，缓冲区0满时，与缓冲区1对换，并唤醒写磁盘线程将日志数据写入磁盘日志文件中。写磁盘线程同时设置定时唤醒，检查缓冲区0是否为空，非空则主动对换写入磁盘中。
<img width="1156" height="493" alt="image" src="https://github.com/user-attachments/assets/1166d8f9-9e56-4952-861a-f73aaf8f8603" />
独立写磁盘线程可让业务线程避免执行缓慢的写磁盘操作，而缓冲区可减少磁盘访问次数。

### （4）内存池
聊天室转发的业务数据，可大可小，每次都需要从堆中申请，使用完释放，频繁的数据交互导致系统产生较多内存碎片。使用内存池来存储这部分数据，可减少系统内存碎片的产生，结构示意图如下。
<img width="1467" height="844" alt="image" src="https://github.com/user-attachments/assets/02f1af7d-a012-4367-ba40-fdf5e1ac4d68" />
采用分离式内存池设计，由多个固定大小的内存池组成，支持自动扩容，更符合实际场景需求。
