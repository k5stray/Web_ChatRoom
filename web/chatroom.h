#ifndef CHATROOM_H
#define CHATROOM_H

#include "../net/tcp_server.h"

class ChatRoom {
public:
    static ChatRoom& get_instance() {
        static ChatRoom instance;
        return instance;
    }
    bool bind_to_tcpserver(TcpServer &server, int http_port, int websocket_port);
    void start(TcpServer &server);
    void stop(TcpServer &server);
    void stop_all();
private:
    ChatRoom();
    ~ChatRoom() = default;
    ChatRoom(const ChatRoom&) = delete;
    ChatRoom& operator=(const ChatRoom&) = delete;

    std::vector<TcpServer*> server_list_;
};

#endif