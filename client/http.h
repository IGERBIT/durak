#ifndef CLIENT_HTTP_H
#define CLIENT_HTTP_H

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#pragma push_macro("WIN32_LEAN_AND_MEAN")
#pragma push_macro("NOMINMAX")

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif  // WIN32_LEAN_AND_MEAN

#ifndef NOMINMAX
#define NOMINMAX
#endif  // NOMINMAX

#include <winsock2.h>
#include <ws2tcpip.h>

#include <istream>
#include "httpreq.cpp"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#pragma comment(lib, "AdvApi32.lib")

namespace http {


class httpRequestError final : public std::logic_error {
   public:
    explicit httpRequestError(const char* str);
    explicit httpRequestError(const std::string& str);
};

class httpResponseError final : public std::logic_error {
   public:
    explicit httpResponseError(const char* str);
    explicit httpResponseError(const std::string& str);
};

class WSA final {
   private:
    bool is_started = false;

   public:
    WSA();
    WSA(WSA&& other) noexcept;
    ~WSA();

    WSA& operator=(WSA&& other) noexcept;
};

class Socket final {
   private:
    SOCKET endpoint = INVALID_SOCKET;
    void select_write(int64_t ms_timeout);
    void select_read(int64_t ms_timeout);

   public:
    Socket();
    Socket(Socket&& other) noexcept;
    ~Socket();

    Socket& operator=(Socket&& other) noexcept;
    void connect(const struct sockaddr* address, socklen_t address_size, uint64_t ms_timeout);
    size_t send(const void* buffer, size_t length, uint64_t timeout);
    size_t read(void* buffer, size_t length, uint64_t timeout);
};


struct Response final {
    uint16_t error = 0;
    std::vector<uint8_t> data;
};

using StreamBuilder = std::function<void(std::ostringstream &oss)>;
using StreamReader = std::function<void(std::istringstream &iss, uint16_t error)>;

class Session final {
   private:
    std::string host;
    std::string port;
    http_req::Request request;

    std::chrono::milliseconds timeout_default;

    bool send(const uint8_t* payload, size_t length, uint64_t timeout);

   public:
    Session(const std::string& host, const std::string& port, std::chrono::milliseconds timeout = std::chrono::milliseconds{-1});
    Response call(uint16_t code, const std::vector<uint8_t>& payload, std::chrono::milliseconds timeout);
    Response call(uint16_t code, const std::vector<uint8_t>& payload);
    Response call(uint16_t code, const uint8_t* payload, size_t length, std::chrono::milliseconds timeout);
    Response call(uint16_t code, const uint8_t* payload, size_t length);
    Response call(uint16_t code, const std::string &str, std::chrono::milliseconds timeout);
    Response call(uint16_t code, const std::string &str);

    Response call(uint16_t code, const StreamBuilder& builder, std::chrono::milliseconds timeout, const StreamReader& reader = nullptr);
    Response call(uint16_t code, const StreamBuilder& builder, const StreamReader& reader = nullptr);
};

}  // namespace http

#endif  // CLIENT_HTTP_H
