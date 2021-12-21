#include "http.h"

#include <iostream>
#include <sstream>

#include "httpreq.cpp"

http::httpRequestError::httpRequestError(const char* str) : std::logic_error{str} {}
http::httpRequestError::httpRequestError(const std::string& str) : std::logic_error{str} {}

http::httpResponseError::httpResponseError(const char* str) : std::logic_error{str} {}
http::httpResponseError::httpResponseError(const std::string& str) : std::logic_error{str} {}

// WSA

http::WSA::WSA() {
    WSADATA wsa_data;
    WORD version = MAKEWORD(2, 2);
    const auto r_code = WSAStartup(version, &wsa_data);
    if (r_code != 0) {
        throw std::system_error(r_code, std::system_category(), "WSAStartup failed");
    }

    if (version != wsa_data.wVersion) {
        WSACleanup();
        throw std::runtime_error("Invalid WinSock version");
    }
    is_started = true;
}

http::WSA::WSA(WSA&& other) noexcept : is_started{other.is_started} { other.is_started = false; }

http::WSA::~WSA() {
    if (is_started) WSACleanup();
}

http::WSA& http::WSA::operator=(WSA&& other) noexcept {
    if (&other == this) return *this;
    if (is_started) WSACleanup();
    is_started = other.is_started;
    other.is_started = false;
    return *this;
}

// Socket

http::Socket::Socket() : endpoint{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)} {
    if (endpoint == INVALID_SOCKET) throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to create socket");

    ULONG mode = 1;
    if (ioctlsocket(endpoint, FIONBIO, &mode) != 0) {
        closesocket(endpoint);
        throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to get socket flags");
    }
}

http::Socket::Socket(Socket&& other) noexcept : endpoint{other.endpoint} { other.endpoint = INVALID_SOCKET; }

http::Socket::~Socket() {
    if (endpoint != INVALID_SOCKET) closesocket(endpoint);
}

http::Socket& http::Socket::operator=(Socket&& other) noexcept {
    if (&other == this) return *this;
    if (endpoint != INVALID_SOCKET) closesocket(endpoint);
    endpoint = other.endpoint;
    other.endpoint = INVALID_SOCKET;
    return *this;
}

void http::Socket::connect(const struct sockaddr* address, const socklen_t address_size, const uint64_t ms_timeout) {
    int result = ::connect(endpoint, address, address_size);

    while (result == -1 && WSAGetLastError() == WSAEINTR) {
        std::cout << std::endl << "r " << result << std::endl << WSAGetLastError();
        result = ::connect(endpoint, address, address_size);
    }

    if (result == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to connect");

        select_write(ms_timeout);

        char socketErrorPointer[sizeof(int)];
        socklen_t optionLength = sizeof(socketErrorPointer);
        if (getsockopt(endpoint, SOL_SOCKET, SO_ERROR, socketErrorPointer, &optionLength) == -1)
            throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to get socket option");

        int socketError;
        std::memcpy(&socketError, socketErrorPointer, sizeof(socketErrorPointer));

        if (socketError != 0) throw std::system_error(socketError, std::system_category(), "Failed to connect");
    }
}

size_t http::Socket::send(const void* buffer, size_t length, uint64_t timeout) {
    select_write(timeout);
    int result = ::send(endpoint, reinterpret_cast<const char*>(buffer), static_cast<int>(length), 0);

    while (result == -1 && WSAGetLastError() == WSAEINTR)
        result = ::send(endpoint, reinterpret_cast<const char*>(buffer), static_cast<int>(length), 0);

    if (result == -1) throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to send data");

    return static_cast<size_t>(result);
}

size_t http::Socket::read(void* buffer, size_t length, uint64_t timeout) {
    select_read(timeout);
    int result = ::recv(endpoint, reinterpret_cast<char*>(buffer), static_cast<int>(length), 0);

    while (result == -1 && WSAGetLastError() == WSAEINTR) result = ::recv(endpoint, reinterpret_cast<char*>(buffer), static_cast<int>(length), 0);

    if (result == -1) throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to read data");

    return static_cast<size_t>(result);
}

void http::Socket::select_write(const int64_t ms_timeout) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(endpoint, &set);

    TIMEVAL timeout{static_cast<LONG>((ms_timeout / 1000)), static_cast<LONG>((ms_timeout % 1000) * 1000)};

    int result = select(0, nullptr, &set, nullptr, (ms_timeout >= 0) ? &timeout : nullptr);

    while (result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR)
        result = select(0, nullptr, &set, nullptr, (ms_timeout >= 0) ? &timeout : nullptr);

    if (result == SOCKET_ERROR) throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to select write socket");
    if (result == 0) throw httpResponseError("Timeout");
}

void http::Socket::select_read(const int64_t ms_timeout) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(endpoint, &set);

    TIMEVAL timeout{static_cast<LONG>((ms_timeout / 1000)), static_cast<LONG>((ms_timeout % 1000) * 1000)};

    int result = select(0, &set, nullptr, nullptr, (ms_timeout >= 0) ? &timeout : nullptr);

    while (result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR)
        result = select(0, &set, nullptr, nullptr, (ms_timeout >= 0) ? &timeout : nullptr);

    if (result == SOCKET_ERROR) throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to select read socket");
    if (result == 0) throw httpResponseError("Timeout");
}

// Session

std::int64_t getRemainingMilliseconds(const std::chrono::steady_clock::time_point time) noexcept {
    const auto now = std::chrono::steady_clock::now();
    const auto remainingTime = std::chrono::duration_cast<std::chrono::milliseconds>(time - now);
    return (remainingTime.count() > 0) ? remainingTime.count() : 0;
}

http::Session::Session(const std::string& host, const std::string& port, std::chrono::milliseconds timeout)
    : host(host), port(""), timeout_default(timeout), request(host + ":" + port, http_req::InternetProtocol::V4) {

    /*addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;


    addrinfo* info;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &info) != 0)
        throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to get address info of " + host);

    const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addersInfo{info, freeaddrinfo};

    sessionSocket.connect(addersInfo->ai_addr, static_cast<int>(addersInfo->ai_addrlen), -1);*/


}

http::Response http::Session::call(uint16_t code, const std::vector<uint8_t>& payload, std::chrono::milliseconds timeout) {
    Response response;

    std::vector<uint8_t> data = payload;

    std::ostringstream oss;

    oss.write(reinterpret_cast<char *>(&code), sizeof(code));
    auto str = oss.str();
    data.insert(data.begin(), str.begin(), str.end());

    auto result = request.send("GET", data, {}, timeout);

    data.clear();

    std::istringstream iss(std::string(result.body.begin(), result.body.begin() + 2));

    iss.read(reinterpret_cast<char *>(&response.error), sizeof(code));

    response.data.assign(result.body.begin() + 2, result.body.end());



    /*const auto stopTime = std::chrono::steady_clock::now() + timeout;
    Response response;

    std::ostringstream oss;

    oss.write(reinterpret_cast<char*>(&code), sizeof(code));

    uint32_t length = payload.size();
    oss.write(reinterpret_cast<char*>(&length), sizeof(length));

    auto str = oss.str();
    auto data = reinterpret_cast<const uint8_t*>(str.c_str());

    if (!send(data, str.length(), timeout.count() > 0 ? getRemainingMilliseconds(stopTime) : -1)) {
        return response;
    }

    oss.clear();

    if (!send(payload.data(), payload.size(), timeout.count() > 0 ? getRemainingMilliseconds(stopTime) : -1)) {
        return response;
    }

    std::array<uint8_t, 4096> buffer{};
    std::vector<uint8_t> responseData;

    size_t meta_rem = 6;
    uint32_t response_rem = 1;

    uint16_t res_code{};

    while (response_rem > 0) {
        if (meta_rem > 0) {
            const auto size = sessionSocket.read(buffer.data(), meta_rem, timeout.count() > 0 ? getRemainingMilliseconds(stopTime) : -1);
            meta_rem -= size;

            if (meta_rem == 0) {
                res_code = *reinterpret_cast<uint16_t*>(buffer.data());
                response_rem = *reinterpret_cast<uint32_t*>(buffer.data() + 2);

                response.data.reserve(response_rem);

                response.error = res_code;
            }
        } else {
            const auto size = sessionSocket.read(buffer.data(), response_rem, timeout.count() > 0 ? getRemainingMilliseconds(stopTime) : -1);
            response_rem -= size;

            response.data.insert(response.data.end(), buffer.begin(), buffer.begin() + size);

            if (response_rem < 1) {
                break;
            }
        }
    }*/



    return response;
}
bool http::Session::send(const uint8_t* payload, size_t length, uint64_t timeout) {
    auto rem = length;

    /*while (rem > 0) {
        const auto size = sessionSocket.send(payload, rem, timeout);

        if (size == SOCKET_ERROR) return false;

        rem -= size;
        payload += size;
    }*/

    return true;
}
http::Response http::Session::call(uint16_t code, const std::vector<uint8_t>& payload) { return call(code, payload, timeout_default); }
http::Response http::Session::call(uint16_t code, const uint8_t* payload, size_t length, std::chrono::milliseconds timeout) {
    std::vector<uint8_t> vec(&payload[0], &payload[length]);
    return call(code, vec, timeout);
}
http::Response http::Session::call(uint16_t code, const uint8_t* payload, size_t length) { return call(code, payload, length, timeout_default); }
http::Response http::Session::call(uint16_t code, const std::string &str, std::chrono::milliseconds timeout) {
    return call(code, reinterpret_cast<const uint8_t *>(str.c_str()), str.length(), timeout);
}
http::Response http::Session::call(uint16_t code, const std::string& str) { return call(code, str, timeout_default); }
http::Response http::Session::call(uint16_t code, const http::StreamBuilder& builder, std::chrono::milliseconds timeout, const http::StreamReader& reader) {
    std::ostringstream ss;

    if(!builder) throw std::invalid_argument("builder is nullptr");

    builder(ss);

    auto result = call(code, ss.str(), timeout);



    for (const auto &item : result.data)
        std::cout << static_cast<int>(item) << " ";

    std::cout << std::endl;

    std::istringstream in(std::string(result.data.begin(), result.data.end()));

    if(reader) {
        reader(in, result.error);
    }

    return result;
}

http::Response http::Session::call(uint16_t code, const http::StreamBuilder& builder, const http::StreamReader& reader) { return call(code, builder, timeout_default, reader); }
