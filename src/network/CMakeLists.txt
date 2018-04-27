# build service
set(SOURCE_FILES
    #uv/ServerImpl.cpp
    #uv/Worker.cpp

    coroutines/ServerImpl.cpp
    coroutines/Worker.cpp
    coroutines/Client.cpp
    coroutines/EventLoop.cpp

    nonblocking/EventLoop.cpp
    nonblocking/ServerImpl.cpp
    nonblocking/Worker.cpp
    nonblocking/Utils.cpp
)

add_library(Network ${SOURCE_FILES})
target_link_libraries(Network pthread uv Coroutine Protocol Execute ${CMAKE_THREAD_LIBS_INIT})