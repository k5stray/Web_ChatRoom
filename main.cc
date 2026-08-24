#include <signal.h>
#include "web/chatroom.h"

TcpServer *g_server = NULL;
void handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
		ChatRoom::get_instance().stop_all();
    }
}

int main()
{
    struct sigaction sa{};
    sa.sa_handler = handler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    TcpServer server(4);
    g_server = &server;

	ChatRoom::get_instance().bind_to_tcpserver(server, 8080, 8888);
	ChatRoom::get_instance().start(server);
}