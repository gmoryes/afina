# build service
set(SOURCE_FILES
    Engine.cpp
)

add_library(Coroutine ${SOURCE_FILES})
target_link_libraries(Coroutine ${CMAKE_THREAD_LIBS_INIT})
