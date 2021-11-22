#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

static constexpr uint16_t buffer_size = 4096;



int main()
{
    WSADATA wsaData;
    int iResult;
    SOCKET ConnectSocket = INVALID_SOCKET;
    char rec_buff[buffer_size];
    auto test_text = "hello, world";

    struct addrinfo *result = NULL,
            *ptr = NULL,
            hints;

    std::cout<<"Hello, World!"<<std::endl;
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if(iResult != 0) {
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    iResult = getaddrinfo("127.0.0.1","2021", &hints, &result);

    for (ptr = result;ptr != nullptr ; ptr = ptr->ai_next) {
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

        if(ConnectSocket == INVALID_SOCKET) {
            WSACleanup();
            return 2;
        }

        iResult = connect(ConnectSocket, ptr->ai_addr, ptr->ai_addrlen);

        if(iResult == SOCKET_ERROR) {
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (ConnectSocket == INVALID_SOCKET) {
        printf("Unable to connect to server!\n");
        WSACleanup();
        return 1;
    }





    // Receive until the peer closes the connection
    do {
        std::string str;
        std::getline(std::cin, str);

        if(str == "exit") {
            // shutdown the connection since no more data will be sent
            iResult = shutdown(ConnectSocket, SD_SEND);
            if (iResult == SOCKET_ERROR) {
                printf("shutdown failed with error: %d\n", WSAGetLastError());
                closesocket(ConnectSocket);
                WSACleanup();
                return 1;
            }

            return 0;
        }

        iResult = send( ConnectSocket, str.c_str(), str.length(), 0 );
        if (iResult == SOCKET_ERROR) {
            printf("send failed with error: %d\n", WSAGetLastError());
            closesocket(ConnectSocket);
            WSACleanup();
            return 1;
        }

        iResult = recv(ConnectSocket, rec_buff, buffer_size, 0);
        if ( iResult > 0 )
        {
            printf("Bytes received: %d\n", iResult);
            std::cout << rec_buff << std::endl;
        }
        else if ( iResult == 0 )
            printf("Connection closed\n");
        else
            printf("recv failed with error: %d\n", WSAGetLastError());

    } while( iResult > 0 );

    // cleanup
    closesocket(ConnectSocket);
    WSACleanup();

    return 0;
}
