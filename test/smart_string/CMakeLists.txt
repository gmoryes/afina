# build service
set(SOURCE_FILES
    SmartStringTester.cpp
)

add_executable(runSmartStringTests ${SOURCE_FILES} ${BACKWARD_ENABLE})
target_link_libraries(runSmartStringTests Network gtest gtest_main)

add_backward(runSmartStringTests)
add_test(runSmartStringTests runSmartStringTests)
