cmake_minimum_required(VERSION 3.20)
project (client)

set(CMAKE_CXX_STANDARD 17)
link_libraries(ws2_32)
set(CMAKE_EXE_LINKER_FLAGS "-static")
add_executable(client main.cpp game.cpp game.h http.cpp http.h)
add_executable(client_bot main2.cpp game.cpp game.h http.cpp http.h)
