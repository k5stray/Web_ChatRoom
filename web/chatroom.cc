#include <iostream>
#include <string>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <iomanip>
#include <sstream>
#include <sys/timerfd.h>

#include "chatroom.h"
#include "../util/openfile.h"
#include "../util/log_util.h"

#define HTML_INDEX_SRC       "web/index.html"
#define HTML_INTERFACE_SRC   "web/chatrom.html"
#define HTML_NODOUND_SRC     "web/404.html"
#define HTTP_ICO_SRC         "web/favicon.ico"

static const char HTTP_RESPONSE_SATUS200[] = "HTTP/1.1 200 OK\r\n";
static const char HTTP_RESPONSE_SATUS404[] = "HTTP/1.1 404 Not Found\r\n";
static const char HTTP_RESPONSE_TYPE[] = "Content-Type: text/html; charset=UTF-8\r\n\r\n";

static const char SWITCH_RESPONE_HEAD[] = R"(HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: )";

static const char HTTP_ICO_HEAD[] = R"(HTTP/1.1 200 OK
Content-Type: image/x-icon\r\n)";

static std::string base64_encode(unsigned char* data, size_t len)
{
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, data, len);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);

    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return result;
}

static std::string calculateSecWebSocketAccept(const std::string& secWebSocketKey)
{
    std::string input = secWebSocketKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash);

    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

inline static uint16_t read_big_u16(const uint8_t* buf)
{
    return (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
}

inline static uint64_t read_big_u64(const uint8_t* buf)
{
    return (static_cast<uint64_t>(buf[0]) << 56)
         | (static_cast<uint64_t>(buf[1]) << 48)
         | (static_cast<uint64_t>(buf[2]) << 40)
         | (static_cast<uint64_t>(buf[3]) << 32)
         | (static_cast<uint64_t>(buf[4]) << 24)
         | (static_cast<uint64_t>(buf[5]) << 16)
         | (static_cast<uint64_t>(buf[6]) << 8)
         | buf[7];
}

inline static void write_big_u16(uint8_t* buf, uint16_t val)
{
    buf[0] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[1] = static_cast<uint8_t>(val & 0xFF);
}

inline static void write_big_u64(uint8_t* buf, uint64_t val)
{
    buf[0] = static_cast<uint8_t>((val >>56) & 0xFF);
    buf[1] = static_cast<uint8_t>((val >>48) & 0xFF);
    buf[2] = static_cast<uint8_t>((val >>40) & 0xFF);
    buf[3] = static_cast<uint8_t>((val >>32) & 0xFF);
    buf[4] = static_cast<uint8_t>((val >>24) & 0xFF);
    buf[5] = static_cast<uint8_t>((val >>16) & 0xFF);
    buf[6] = static_cast<uint8_t>((val >>8) & 0xFF);
    buf[7] = static_cast<uint8_t>(val & 0xFF);
}

#define WEBSOCKET_MIN_HEAD_SIZE 6
struct frame_info {
    struct {
        uint8_t fin;
        uint8_t rsv1;
        uint8_t rsv2;
        uint8_t rsv3;
        uint8_t opcode;
        uint8_t mask;
        uint8_t payload_len;
    } header;
    bool opencode_valiad;
    uint64_t req_payload_len;
    uint64_t req_frame_len;
    uint8_t *req_mask_addr;
    uint8_t *req_payload_addr;
    uint8_t resp_payload_offset;
    uint64_t resp_frame_len;
};

inline static bool check_opencode(uint8_t opcode)
{
    if ((opcode >= 0x3 && opcode <= 0x7) || (opcode >= 0xb && opcode <= 0xf))
        return false;

    return true;
}

#define WEBSOCKET_BASE_SIZE 2
#define WEBSOCKET_MASK_SIZE 4
static bool decode_frame_info(frame_info &info, uint8_t * const data, int len)
{
    if (len < WEBSOCKET_MIN_HEAD_SIZE)
        return false;

    info.header.fin = (data[0] & 0x80) != 0;
    info.header.rsv1 = (data[0] & 0x40) != 0;
    info.header.rsv2 = (data[0] & 0x20) != 0;
    info.header.rsv3 = (data[0] & 0x10) != 0;
    info.header.opcode = data[0] & 0xf;
    info.header.mask = (data[1] & 0x80) != 0;
    info.header.payload_len = data[1] & 0x7f;

    int ex_size;
    info.opencode_valiad = check_opencode(info.header.opcode);
    if (info.header.payload_len < 126) {
        ex_size = 0;
        info.req_payload_len = info.header.payload_len;
    } else if (info.header.payload_len == 126) {
        ex_size = 2;
        info.req_payload_len = read_big_u16(data + 2);
    } else { // 127
        if (len < 10)
            return false;
        ex_size = 8;
        info.req_payload_len = read_big_u64(data + 2);
    }

    info.req_mask_addr = (data + WEBSOCKET_BASE_SIZE + ex_size);
    info.req_payload_addr = (data + WEBSOCKET_BASE_SIZE + ex_size + WEBSOCKET_MASK_SIZE);
    info.req_frame_len = info.req_payload_len + WEBSOCKET_BASE_SIZE + ex_size + WEBSOCKET_MASK_SIZE;
    info.resp_payload_offset = WEBSOCKET_BASE_SIZE + ex_size;
    info.resp_frame_len = info.req_payload_len + WEBSOCKET_BASE_SIZE + ex_size;

    return true;
}

static std::string get_websocket_key(const std::string& str)
{
    std::string line;
    bool found = false;
    std::istringstream stream(str);
    while (getline(stream, line)) {
        if (line.find("Sec-WebSocket-Key") != std::string::npos) {
            found = true;
            break;
        }
    }

    if (!found) {
        LOG_ERROR_("Fail to get [Sec-WebSocket-Key]\n");
        return "";
    }

    return line.substr(line.find(':') + 2);
}

static void http_response_upgrade(TcpConnectionPtr conn, const char *data)
{
	std::string response_head = SWITCH_RESPONE_HEAD;
	std::string key = get_websocket_key(data);
    if (key.size() == 0) {
        return;
    }

	key.erase(key.length() - 1);
	std::string accpet_str = calculateSecWebSocketAccept(key);
	accpet_str += "\r\n\r\n";
	response_head += accpet_str;
	conn->send(response_head.data(), response_head.length());
}

static std::string get_http_date()
{
    char buf[32];
    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};

    gmtime_r(&now, &tm_buf);
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_buf);

    return std::string(buf);
}

static void http_response_html(TcpConnectionPtr conn, std::string filepath)
{
	char buf[32] = {0};
	std::string body = FileInstance::get_Instance().get_openfile(filepath);
    std::string response = HTTP_RESPONSE_SATUS200;
    response += "Date: " + get_http_date() + "\r\n";
	sprintf(buf, "Content-Length: %lu\r\n", body.length());
	response += buf;
	response += HTTP_RESPONSE_TYPE;
	response += body;
	conn->send(response.data(), response.length());
}

static void http_response_404(TcpConnectionPtr conn)
{
	char buf[32] = {0};
	std::string body = FileInstance::get_Instance().get_openfile(HTML_NODOUND_SRC);
    std::string response = HTTP_RESPONSE_SATUS404;
    response += "Date: " + get_http_date() + "\r\n";
	sprintf(buf, "Content-Length: %lu\r\n", body.length());
	response += buf;
	response += HTTP_RESPONSE_TYPE;
	response += body;
	conn->send(response.data(), response.length());
}

static void http_response_ico(TcpConnectionPtr conn)
{
	char buf[32] = {0};
	std::string response = HTTP_ICO_HEAD;
	std::string body = FileInstance::get_Instance().get_openfile(HTTP_ICO_SRC);
	sprintf(buf, "Content-Length: %lu\r\n", body.length());
	response += buf;
    response += "\r\n";
	response += body;
	conn->send(response.data(), response.length());
}

static int handle_websocket_frame(TcpConnectionPtr conn, const char *idata, int len)
{
	frame_info info = {0};
	uint8_t *data = (uint8_t*)(idata);

    decode_frame_info(info, data, len);

	if (!info.opencode_valiad || info.header.mask != 1) {
		LOG_ERROR_("Unsupported websocket request!\n");
		return len;
	}

	char *response = new char[info.resp_frame_len + 1];
	response[info.resp_frame_len] = '\0';

    response[0] = idata[0];
    response[1] = idata[1];

	response[1] &= (~(1 << 7));
	if (info.header.payload_len == 126) {
		write_big_u16((uint8_t*)response + WEBSOCKET_BASE_SIZE, info.req_payload_len);
	} else if (info.header.payload_len == 127) {
		write_big_u64((uint8_t*)response + WEBSOCKET_BASE_SIZE, info.req_payload_len);
	}

	for (uint64_t i = 0; i < info.req_payload_len; i++) {
		response[i + info.resp_payload_offset] = info.req_payload_addr[i] ^ (info.req_mask_addr[ i % WEBSOCKET_MASK_SIZE]);
	}

    if (info.header.opcode == 0x8) { // Close
        conn->close_conn();
	} else if (info.header.opcode == 0x9) { // PING
		response[info.resp_payload_offset + 1] = 'O';
		response[0] = (response[0] & 0xF0) | 0xA;
		conn->send(response, info.resp_frame_len);
	} else if (info.header.opcode == 0xA) { // PONG
		// Do nothing
	} else {
		conn->notify_others(response, info.resp_frame_len);
	}
	delete[] response;
	return info.req_frame_len;
}

bool ChatRoom::bind_to_tcpserver(TcpServer &server, int http_port, int websocket_port)
{
	server.add_listen(http_port,
		[](TcpConnectionPtr conn, const char* data, int len) -> int {
			if (strstr(data, "GET ") != NULL) {
				if (len >= 14 && strncmp("GET / HTTP/1.1", data, 14) == 0) {
                    http_response_html(conn, HTML_INDEX_SRC);
				} else if (len >= 18 && strncmp("GET /chat HTTP/1.1", data, 18) == 0) {
					http_response_html(conn, HTML_INTERFACE_SRC);
                } else if (len >= 25 && strncmp("GET /favicon.ico HTTP/1.1", data, 25) == 0) {
                    http_response_ico(conn);
				} else {
                    http_response_404(conn);
                }
			} else {
				LOG_ERROR_("Unsupported request!\n");
			}
			return len;
		},
		nullptr
	);
	server.add_listen(websocket_port,
		[](TcpConnectionPtr conn, const char* data, int len) -> int {
			int ret = len;
			if (len >= 14 && strncmp("GET / HTTP/1.1", data, 14) == 0) {
				http_response_upgrade(conn, data);
			} else {
				ret = handle_websocket_frame(conn, data, len);
			}
			return ret;
		},
		[](const char* data, int len)->bool {
			frame_info info = {0};
            if (!decode_frame_info(info, (uint8_t*)data, len))
                return false;
			if (!info.opencode_valiad || info.header.mask != 1) // invalliad frame, return true to handle error
				return true;
			if (info.req_frame_len <= len)
				return true;
			return false;
		}
	);

    server_list_.push_back(&server);
    return true;
}

static int get_timer_fd(int sec = 1, int ms = 0)
{
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (tfd < 0) return -1;
    struct itimerspec ts;
    ts.it_value.tv_sec = sec;
    ts.it_value.tv_nsec = (ms * 1000000LL);
    ts.it_interval = ts.it_value;
    timerfd_settime(tfd, 0, &ts, nullptr);
    return tfd;
}

ChatRoom::ChatRoom()
{
	FileInstance::get_Instance().add_openfile(HTML_INDEX_SRC);
	FileInstance::get_Instance().add_openfile(HTML_INTERFACE_SRC);
    FileInstance::get_Instance().add_openfile(HTML_NODOUND_SRC);
    FileInstance::get_Instance().add_openfile(HTTP_ICO_SRC, 1);
}

void ChatRoom::start(TcpServer &server)
{
    int fd = get_timer_fd(20, 0);
    if (fd < 0) {
        LOG_WARN_("Fail to get timerfd\n");
    } else {
        server.set_timer(fd, [&server](uint64_t val){
            server.check_inactive_connect(300);
        });
    }

    server.start();
}

void ChatRoom::stop(TcpServer &server)
{
    server.stop();
}

void ChatRoom::stop_all()
{
    for (auto &server : server_list_) {
        server->stop();
    }
}