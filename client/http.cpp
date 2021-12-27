#include "http.h"

#include <iostream>
#include <sstream>

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
    result = ::connect(endpoint, address, address_size);
  }

  if (result == SOCKET_ERROR) {
    if (WSAGetLastError() != WSAEWOULDBLOCK) throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to connect");

    select_write(ms_timeout);

    char socketErrorPointer[sizeof(int)];
    socklen_t optionLength = sizeof(socketErrorPointer);
    if (getsockopt(endpoint, SOL_SOCKET, SO_ERROR, socketErrorPointer, &optionLength) == SOCKET_ERROR)
      throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to get socket option");

    int socketError;
    std::memcpy(&socketError, socketErrorPointer, sizeof(socketErrorPointer));

    if (socketError != 0) throw std::system_error(socketError, std::system_category(), "Failed to connect");
  }
}

size_t http::Socket::send(const void* buffer, size_t length, uint64_t timeout) {
  select_write(timeout);
  int result = ::send(endpoint, reinterpret_cast<const char*>(buffer), static_cast<int>(length), 0);

  while (result == -1 && WSAGetLastError() == WSAEINTR) result = ::send(endpoint, reinterpret_cast<const char*>(buffer), static_cast<int>(length), 0);

  if (result == -1) throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to send data");

  return static_cast<size_t>(result);
}

size_t http::Socket::read(void* buffer, size_t length, const uint64_t timeout) {
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

  while (result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) result = select(0, nullptr, &set, nullptr, (ms_timeout >= 0) ? &timeout : nullptr);

  if (result == SOCKET_ERROR) throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to select write socket");
  if (result == 0) throw httpResponseError("Timeout");
}

void http::Socket::select_read(const int64_t ms_timeout) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(endpoint, &set);

  TIMEVAL timeout{static_cast<LONG>((ms_timeout / 1000)), static_cast<LONG>((ms_timeout % 1000) * 1000)};

  int result = select(0, &set, nullptr, nullptr, (ms_timeout >= 0) ? &timeout : nullptr);

  while (result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) result = select(0, &set, nullptr, nullptr, (ms_timeout >= 0) ? &timeout : nullptr);

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
    : domain(host), port(port), host(host + ":" + port), timeout_default(timeout) {}

http::Response http::Session::call(uint16_t code, const std::vector<uint8_t>& payload, std::chrono::milliseconds timeout) {
  Response response;

  std::vector<uint8_t> data = payload;

  std::ostringstream oss;

  oss.write(reinterpret_cast<char*>(&code), sizeof(code));
  auto str = oss.str();
  data.insert(data.begin(), str.begin(), str.end());

  auto result = send("GET", data, timeout);

  data.clear();

  std::istringstream iss(std::string(result.data.begin(), result.data.begin() + 2));

  iss.read(reinterpret_cast<char*>(&response.status), sizeof(code));

  response.data.assign(result.data.begin() + 2, result.data.end());


  return response;
}

http::Response http::Session::call(uint16_t code, const std::vector<uint8_t>& payload) { return call(code, payload, timeout_default); }
http::Response http::Session::call(uint16_t code, const uint8_t* payload, size_t length, std::chrono::milliseconds timeout) {
  std::vector<uint8_t> vec(&payload[0], &payload[length]);
  return call(code, vec, timeout);
}
http::Response http::Session::call(uint16_t code, const uint8_t* payload, size_t length) { return call(code, payload, length, timeout_default); }
http::Response http::Session::call(uint16_t code, const std::string& str, std::chrono::milliseconds timeout) {
  return call(code, reinterpret_cast<const uint8_t*>(str.c_str()), str.length(), timeout);
}
http::Response http::Session::call(uint16_t code, const std::string& str) { return call(code, str, timeout_default); }
http::Response http::Session::call(uint16_t code, const http::StreamBuilder& builder, std::chrono::milliseconds timeout,
                                   const http::StreamReader& reader) {
  std::ostringstream ss;

  if (!builder) throw std::invalid_argument("builder is nullptr");

  builder(ss);

  auto result = call(code, ss.str(), timeout);


  for (const auto& item : result.data) std::cout << static_cast<int>(item) << " ";

  std::cout << std::endl;

  std::istringstream in(std::string(result.data.begin(), result.data.end()));

  if (reader) {
    reader(in, result.status);
  }

  return result;
}

http::Response http::Session::call(uint16_t code, const http::StreamBuilder& builder, const http::StreamReader& reader) {
  return call(code, builder, timeout_default, reader);
}
http::Response http::Session::send(const std::string& method, const std::vector<uint8_t>& body, const std::chrono::milliseconds timeout) {
  const auto stopTime = std::chrono::steady_clock::now() + timeout;


  addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* info;
  if (getaddrinfo(domain.c_str(), port.c_str(), &hints, &info) != 0)
    throw std::system_error(WSAGetLastError(), std::system_category(), "Failed to get address info of " + domain);

  const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addressInfo{info, freeaddrinfo};

  // RFC 7230, 3.1.1. Request Line
  std::string headerData = method + " " + path + " HTTP/1.1\r\n";

  // RFC 7230, 3.2. Header Fields
  headerData += "Host: " + host +
                "\r\n"
                "Content-Length: " +
                std::to_string(body.size()) +
                "\r\n"
                "\r\n";

  std::vector<uint8_t> requestData(headerData.begin(), headerData.end());
  requestData.insert(requestData.end(), body.begin(), body.end());

  Socket socket;

  // take the first address from the list
  socket.connect(addressInfo->ai_addr, static_cast<socklen_t>(addressInfo->ai_addrlen),
                 (timeout.count() >= 0) ? getRemainingMilliseconds(stopTime) : -1);

  auto remaining = requestData.size();
  auto sendData = requestData.data();

  // send the request
  while (remaining > 0) {
    const auto size = socket.send(sendData, remaining, (timeout.count() >= 0) ? getRemainingMilliseconds(stopTime) : -1);
    remaining -= size;
    sendData += size;
  }

  std::array<std::uint8_t, 4096> tempBuffer{};
  constexpr std::array<std::uint8_t, 2> crlf = {'\r', '\n'};
  Response response;
  std::vector<std::uint8_t> responseData;
  enum class State { parsingStatusLine, parsingHeaders, parsingBody } state = State::parsingStatusLine;
  bool contentLengthReceived = false;
  std::size_t contentLength = 0;
  bool chunkedResponse = false;
  std::size_t expectedChunkSize = 0;
  bool removeCrlfAfterChunk = false;

  // read the response
  for (;;) {
    const auto size = socket.read(tempBuffer.data(), tempBuffer.size(), (timeout.count() >= 0) ? getRemainingMilliseconds(stopTime) : -1);
    if (size == 0) return response;

    responseData.insert(responseData.end(), tempBuffer.begin(), tempBuffer.begin() + size);

    if (state != State::parsingBody)
      for (;;) {
        const auto i = std::search(responseData.begin(), responseData.end(), crlf.begin(), crlf.end());

        if (i == responseData.end()) break;

        const std::string line(responseData.begin(), i);
        responseData.erase(responseData.begin(), i + 2);

        if (line.empty()) {
          state = State::parsingBody;
          break;
        } else if (state == State::parsingStatusLine) {
          state = State::parsingHeaders;

          const auto httpEndIterator = std::find(line.begin(), line.end(), ' ');

          if (httpEndIterator != line.end()) {
            const auto statusStartIterator = httpEndIterator + 1;
            const auto statusEndIterator = std::find(statusStartIterator, line.end(), ' ');
            const std::string status{statusStartIterator, statusEndIterator};
            response.status = std::stoi(status);
          }
        } else if (state == State::parsingHeaders) {
          const auto colonPosition = line.find(':');

          if (colonPosition == std::string::npos) throw httpResponseError("Invalid header: " + line);

          auto headerName = line.substr(0, colonPosition);

          const auto toLower = [](const char c) { return (c >= 'A' && c <= 'Z') ? c - ('A' - 'a') : c; };

          std::transform(headerName.begin(), headerName.end(), headerName.begin(), toLower);

          auto headerValue = line.substr(colonPosition + 1);

          const auto isNotWhitespace = [](const char c) { return c != ' ' && c != '\t'; };

          // ltrim
          headerValue.erase(headerValue.begin(), std::find_if(headerValue.begin(), headerValue.end(), isNotWhitespace));

          // rtrim
          headerValue.erase(std::find_if(headerValue.rbegin(), headerValue.rend(), isNotWhitespace).base(), headerValue.end());

          if (headerName == "content-length") {
            contentLength = std::stoul(headerValue);
            contentLengthReceived = true;
            response.data.reserve(contentLength);
          } else if (headerName == "transfer-encoding") {
            if (headerValue == "chunked")
              chunkedResponse = true;
            else
              throw httpResponseError("Unsupported transfer encoding: " + headerValue);
          }
        }
      }

    if (state == State::parsingBody) {
      if (chunkedResponse) {
        for (;;) {
          if (expectedChunkSize > 0) {
            const auto toWrite = std::min(expectedChunkSize, responseData.size());
            response.data.insert(response.data.end(), responseData.begin(), responseData.begin() + static_cast<std::ptrdiff_t>(toWrite));
            responseData.erase(responseData.begin(), responseData.begin() + static_cast<std::ptrdiff_t>(toWrite));
            expectedChunkSize -= toWrite;

            if (expectedChunkSize == 0) removeCrlfAfterChunk = true;
            if (responseData.empty()) break;
          } else {
            if (removeCrlfAfterChunk) {
              if (responseData.size() < 2) break;

              if (!std::equal(crlf.begin(), crlf.end(), responseData.begin())) throw httpResponseError("Invalid chunk");

              removeCrlfAfterChunk = false;
              responseData.erase(responseData.begin(), responseData.begin() + 2);
            }

            const auto i = std::search(responseData.begin(), responseData.end(), crlf.begin(), crlf.end());

            if (i == responseData.end()) break;

            const std::string line(responseData.begin(), i);
            responseData.erase(responseData.begin(), i + 2);

            expectedChunkSize = std::stoul(line, nullptr, 16);
            if (expectedChunkSize == 0) return response;
          }
        }
      } else {
        response.data.insert(response.data.end(), responseData.begin(), responseData.end());
        responseData.clear();

        // got the whole content
        if (contentLengthReceived && response.data.size() >= contentLength) return response;
      }
    }
  }

  return response;
}
