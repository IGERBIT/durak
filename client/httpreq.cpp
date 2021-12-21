#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

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

namespace http_req {
class RequestError final : public std::logic_error {
   public:
    explicit RequestError(const char* str) : std::logic_error{str} {}
    explicit RequestError(const std::string& str) : std::logic_error{str} {}
};

class ResponseError final : public std::runtime_error {
   public:
    explicit ResponseError(const char* str) : std::runtime_error{str} {}
    explicit ResponseError(const std::string& str) : std::runtime_error{str} {}
};

enum class InternetProtocol : std::uint8_t { V4, V6 };

inline namespace detail {
class WinSock final {
   public:
    WinSock() {
        WSADATA wsaData;
        const auto error = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (error != 0) throw std::system_error(error, std::system_category(), "WSAStartup failed");

        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            WSACleanup();
            throw std::runtime_error("Invalid WinSock version");
        }

        started = true;
    }

    ~WinSock() {
        if (started) WSACleanup();
    }

    WinSock(WinSock&& other) noexcept : started{other.started} { other.started = false; }

    WinSock& operator=(WinSock&& other) noexcept {
        if (&other == this) return *this;
        if (started) WSACleanup();
        started = other.started;
        other.started = false;
        return *this;
    }

   private:
    bool started = false;
};

inline int getLastError() noexcept { return WSAGetLastError(); }

constexpr int getAddressFamily(const InternetProtocol internetProtocol) {
    return (internetProtocol == InternetProtocol::V4) ? AF_INET
           : (internetProtocol == InternetProtocol::V6)
               ? AF_INET6
               : throw RequestError("Unsupported protocol");
}

class Socket final {
   public:
    using Type = SOCKET;
    static constexpr Type invalid = INVALID_SOCKET;

    explicit Socket(const InternetProtocol internetProtocol)
        : endpoint{socket(getAddressFamily(internetProtocol), SOCK_STREAM, IPPROTO_TCP)} {
        if (endpoint == invalid)
            throw std::system_error(getLastError(), std::system_category(),
                                    "Failed to create socket");

        ULONG mode = 1;
        if (ioctlsocket(endpoint, FIONBIO, &mode) != 0) {
            close();
            throw std::system_error(WSAGetLastError(), std::system_category(),
                                    "Failed to get socket flags");
        }
    }

    ~Socket() {
        if (endpoint != invalid) close();
    }

    Socket(Socket&& other) noexcept : endpoint{other.endpoint} { other.endpoint = invalid; }

    Socket& operator=(Socket&& other) noexcept {
        if (&other == this) return *this;
        if (endpoint != invalid) close();
        endpoint = other.endpoint;
        other.endpoint = invalid;
        return *this;
    }

    void connect(const struct sockaddr* address, const socklen_t addressSize,
                 const std::int64_t timeout) {
        auto result = ::connect(endpoint, address, addressSize);
        while (result == -1 && WSAGetLastError() == WSAEINTR)
            result = ::connect(endpoint, address, addressSize);

        if (result == -1) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                select(SelectType::write, timeout);

                char socketErrorPointer[sizeof(int)];
                socklen_t optionLength = sizeof(socketErrorPointer);
                if (getsockopt(endpoint, SOL_SOCKET, SO_ERROR, socketErrorPointer, &optionLength) ==
                    -1)
                    throw std::system_error(WSAGetLastError(), std::system_category(),
                                            "Failed to get socket option");

                int socketError;
                std::memcpy(&socketError, socketErrorPointer, sizeof(socketErrorPointer));

                if (socketError != 0)
                    throw std::system_error(socketError, std::system_category(),
                                            "Failed to connect");
            } else
                throw std::system_error(WSAGetLastError(), std::system_category(),
                                        "Failed to connect");
        }
    }

    std::size_t send(const void* buffer, const std::size_t length, const std::int64_t timeout) {
        select(SelectType::write, timeout);
        auto result =
            ::send(endpoint, reinterpret_cast<const char*>(buffer), static_cast<int>(length), 0);

        while (result == -1 && WSAGetLastError() == WSAEINTR)
            result = ::send(endpoint, reinterpret_cast<const char*>(buffer),
                            static_cast<int>(length), 0);

        if (result == -1)
            throw std::system_error(WSAGetLastError(), std::system_category(),
                                    "Failed to send data");

        return static_cast<std::size_t>(result);
    }

    std::size_t recv(void* buffer, const std::size_t length, const std::int64_t timeout) {
        select(SelectType::read, timeout);
        auto result =
            ::recv(endpoint, reinterpret_cast<char*>(buffer), static_cast<int>(length), 0);

        while (result == -1 && WSAGetLastError() == WSAEINTR)
            result = ::recv(endpoint, reinterpret_cast<char*>(buffer), static_cast<int>(length), 0);

        if (result == -1)
            throw std::system_error(WSAGetLastError(), std::system_category(),
                                    "Failed to read data");
        return static_cast<std::size_t>(result);
    }

   private:
    enum class SelectType { read, write };

    void select(const SelectType type, const std::int64_t timeout) {
        fd_set descriptorSet;
        FD_ZERO(&descriptorSet);
        FD_SET(endpoint, &descriptorSet);

        TIMEVAL selectTimeout{static_cast<LONG>(timeout / 1000),
                              static_cast<LONG>((timeout % 1000) * 1000)};
        auto count = ::select(0, (type == SelectType::read) ? &descriptorSet : nullptr,
                              (type == SelectType::write) ? &descriptorSet : nullptr, nullptr,
                              (timeout >= 0) ? &selectTimeout : nullptr);

        while (count == -1 && WSAGetLastError() == WSAEINTR)
            count = ::select(0, (type == SelectType::read) ? &descriptorSet : nullptr,
                             (type == SelectType::write) ? &descriptorSet : nullptr, nullptr,
                             (timeout >= 0) ? &selectTimeout : nullptr);

        if (count == -1)
            throw std::system_error(WSAGetLastError(), std::system_category(),
                                    "Failed to select socket");
        else if (count == 0)
            throw ResponseError("Request timed out");
    }

    void close() noexcept { closesocket(endpoint); }


    Type endpoint = invalid;
};
}  // namespace detail

struct Response final {
    // RFC 7231, 6. Response Status Codes
    enum Status {

    };

    int status = 0;
    std::string description;
    std::vector<std::string> headers;
    std::vector<std::uint8_t> body;
};

class Request final {
   public:
    explicit Request(const std::string& url, const InternetProtocol protocol = InternetProtocol::V4)
        : internetProtocol{protocol} {
        const auto schemeEndPosition = url.find("://");

        if (schemeEndPosition != std::string::npos) {
            scheme = url.substr(0, schemeEndPosition);
            path = url.substr(schemeEndPosition + 3);
        } else {
            scheme = "http";
            path = url;
        }

        const auto fragmentPosition = path.find('#');

        // remove the fragment part
        if (fragmentPosition != std::string::npos) path.resize(fragmentPosition);

        const auto pathPosition = path.find('/');

        if (pathPosition == std::string::npos) {
            host = path;
            path = "/";
        } else {
            host = path.substr(0, pathPosition);
            path = path.substr(pathPosition);
        }

        const auto portPosition = host.find(':');

        if (portPosition != std::string::npos) {
            domain = host.substr(0, portPosition);
            port = host.substr(portPosition + 1);
        } else {
            domain = host;
            port = "80";
        }
    }

    Response send(const std::string& method = "GET", const std::string& body = "",
                  const std::vector<std::string>& headers = {},
                  const std::chrono::milliseconds timeout = std::chrono::milliseconds{-1}) {
        return send(method, std::vector<uint8_t>(body.begin(), body.end()), headers, timeout);
    }

    Response send(const std::string& method, const std::vector<uint8_t>& body,
                  const std::vector<std::string>& headers,
                  const std::chrono::milliseconds timeout = std::chrono::milliseconds{-1}) {
        const auto stopTime = std::chrono::steady_clock::now() + timeout;

        if (scheme != "http") throw RequestError("Only HTTP scheme is supported");

        addrinfo hints = {};
        hints.ai_family = getAddressFamily(internetProtocol);
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* info;
        if (getaddrinfo(domain.c_str(), port.c_str(), &hints, &info) != 0)
            throw std::system_error(getLastError(), std::system_category(),
                                    "Failed to get address info of " + domain);

        const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addressInfo{info, freeaddrinfo};

        // RFC 7230, 3.1.1. Request Line
        std::string headerData = method + " " + path + " HTTP/1.1\r\n";

        for (const auto& header : headers) headerData += header + "\r\n";

        // RFC 7230, 3.2. Header Fields
        headerData += "Host: " + host +
                      "\r\n"
                      "Content-Length: " +
                      std::to_string(body.size()) +
                      "\r\n"
                      "\r\n";

        std::vector<uint8_t> requestData(headerData.begin(), headerData.end());
        requestData.insert(requestData.end(), body.begin(), body.end());

        Socket socket{internetProtocol};

        // take the first address from the list
        socket.connect(addressInfo->ai_addr, static_cast<socklen_t>(addressInfo->ai_addrlen),
                       (timeout.count() >= 0) ? getRemainingMilliseconds(stopTime) : -1);

        auto remaining = requestData.size();
        auto sendData = requestData.data();

        // send the request
        while (remaining > 0) {
            const auto size =
                socket.send(sendData, remaining,
                            (timeout.count() >= 0) ? getRemainingMilliseconds(stopTime) : -1);
            remaining -= size;
            sendData += size;
        }

        std::array<std::uint8_t, 4096> tempBuffer;
        constexpr std::array<std::uint8_t, 2> crlf = {'\r', '\n'};
        Response response;
        std::vector<std::uint8_t> responseData;
        enum class State {
            parsingStatusLine,
            parsingHeaders,
            parsingBody
        } state = State::parsingStatusLine;
        bool contentLengthReceived = false;
        std::size_t contentLength = 0;
        bool chunkedResponse = false;
        std::size_t expectedChunkSize = 0;
        bool removeCrlfAfterChunk = false;

        // read the response
        for (;;) {
            const auto size =
                socket.recv(tempBuffer.data(), tempBuffer.size(),
                            (timeout.count() >= 0) ? getRemainingMilliseconds(stopTime) : -1);
            if (size == 0)  // disconnected
                return response;

            responseData.insert(responseData.end(), tempBuffer.begin(), tempBuffer.begin() + size);

            if (state != State::parsingBody)
                for (;;) {
                    // RFC 7230, 3. Message Format
                    const auto i = std::search(responseData.begin(), responseData.end(),
                                               crlf.begin(), crlf.end());

                    // didn't find a newline
                    if (i == responseData.end()) break;

                    const std::string line(responseData.begin(), i);
                    responseData.erase(responseData.begin(), i + 2);

                    // empty line indicates the end of the header section (RFC 7230, 2.1.
                    // Client/Server Messaging)
                    if (line.empty()) {
                        state = State::parsingBody;
                        break;
                    } else if (state == State::parsingStatusLine)  // RFC 7230, 3.1.2. Status Line
                    {
                        state = State::parsingHeaders;

                        const auto httpEndIterator = std::find(line.begin(), line.end(), ' ');

                        if (httpEndIterator != line.end()) {
                            const auto statusStartIterator = httpEndIterator + 1;
                            const auto statusEndIterator =
                                std::find(statusStartIterator, line.end(), ' ');
                            const std::string status{statusStartIterator, statusEndIterator};
                            response.status = std::stoi(status);

                            if (statusEndIterator != line.end()) {
                                const auto descriptionStartIterator = statusEndIterator + 1;
                                response.description =
                                    std::string{descriptionStartIterator, line.end()};
                            }
                        }
                    } else if (state == State::parsingHeaders)  // RFC 7230, 3.2. Header Fields
                    {
                        response.headers.push_back(line);

                        const auto colonPosition = line.find(':');

                        if (colonPosition == std::string::npos)
                            throw ResponseError("Invalid header: " + line);

                        auto headerName = line.substr(0, colonPosition);

                        const auto toLower = [](const char c) {
                            return (c >= 'A' && c <= 'Z') ? c - ('A' - 'a') : c;
                        };

                        std::transform(headerName.begin(), headerName.end(), headerName.begin(),
                                       toLower);

                        auto headerValue = line.substr(colonPosition + 1);

                        // RFC 7230, Appendix B. Collected ABNF
                        const auto isNotWhitespace = [](const char c) {
                            return c != ' ' && c != '\t';
                        };

                        // ltrim
                        headerValue.erase(
                            headerValue.begin(),
                            std::find_if(headerValue.begin(), headerValue.end(), isNotWhitespace));

                        // rtrim
                        headerValue.erase(
                            std::find_if(headerValue.rbegin(), headerValue.rend(), isNotWhitespace)
                                .base(),
                            headerValue.end());

                        if (headerName == "content-length") {
                            contentLength = std::stoul(headerValue);
                            contentLengthReceived = true;
                            response.body.reserve(contentLength);
                        } else if (headerName == "transfer-encoding") {
                            if (headerValue == "chunked")
                                chunkedResponse = true;
                            else
                                throw ResponseError("Unsupported transfer encoding: " +
                                                    headerValue);
                        }
                    }
                }

            if (state == State::parsingBody) {
                // Content-Length must be ignored if Transfer-Encoding is received (RFC 7230, 3.2.
                // Content-Length)
                if (chunkedResponse) {
                    for (;;) {
                        if (expectedChunkSize > 0) {
                            const auto toWrite = (std::min)(expectedChunkSize, responseData.size());
                            response.body.insert(
                                response.body.end(), responseData.begin(),
                                responseData.begin() + static_cast<std::ptrdiff_t>(toWrite));
                            responseData.erase(
                                responseData.begin(),
                                responseData.begin() + static_cast<std::ptrdiff_t>(toWrite));
                            expectedChunkSize -= toWrite;

                            if (expectedChunkSize == 0) removeCrlfAfterChunk = true;
                            if (responseData.empty()) break;
                        } else {
                            if (removeCrlfAfterChunk) {
                                if (responseData.size() < 2) break;

                                if (!std::equal(crlf.begin(), crlf.end(), responseData.begin()))
                                    throw ResponseError("Invalid chunk");

                                removeCrlfAfterChunk = false;
                                responseData.erase(responseData.begin(), responseData.begin() + 2);
                            }

                            const auto i = std::search(responseData.begin(), responseData.end(),
                                                       crlf.begin(), crlf.end());

                            if (i == responseData.end()) break;

                            const std::string line(responseData.begin(), i);
                            responseData.erase(responseData.begin(), i + 2);

                            expectedChunkSize = std::stoul(line, nullptr, 16);
                            if (expectedChunkSize == 0) return response;
                        }
                    }
                } else {
                    response.body.insert(response.body.end(), responseData.begin(),
                                         responseData.end());
                    responseData.clear();

                    // got the whole content
                    if (contentLengthReceived && response.body.size() >= contentLength)
                        return response;
                }
            }
        }

        return response;
    }

   private:
    static std::int64_t getRemainingMilliseconds(
        const std::chrono::steady_clock::time_point time) noexcept {
        const auto now = std::chrono::steady_clock::now();
        const auto remainingTime =
            std::chrono::duration_cast<std::chrono::milliseconds>(time - now);
        return (remainingTime.count() > 0) ? remainingTime.count() : 0;
    }

    WinSock winSock;
    InternetProtocol internetProtocol;
    std::string scheme;
    std::string host;
    std::string domain;
    std::string port;
    std::string path;
};
}  // namespace http

#endif  // HTTPREQUEST_HPP