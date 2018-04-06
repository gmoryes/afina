#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <network/nonblocking/Utils.h>

using namespace Afina;

TEST(SmartStringTester, InitTestCtrs) {
    Utils::SmartString string("abc");

    char buf[] = "aaaa";

    string.Put(buf);
    ASSERT_EQ(string.size(), 7);
}

TEST(SmartStringTester, InitTest) {
    Utils::SmartString string("abc");

    ASSERT_EQ(string.size(), 3);
    ASSERT_EQ(string[0], 'a');
    ASSERT_EQ(string[1], 'b');
    ASSERT_EQ(string[2], 'c');
}

TEST(SmartStringTester, PutTest) {
    Utils::SmartString string("abc");

    string.Put("de");

    ASSERT_EQ(string.size(), 5);
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(string[i], 'a' + i);
    }

    string.Put("fg");
    ASSERT_EQ(string.size(), 7);
    for (int i = 0; i < 7; i++) {
        ASSERT_EQ(string[i], 'a' + i);
    }
}

TEST(SmartStringTester, EraseTest_1) {
    Utils::SmartString string("abcdef");

    ASSERT_EQ(string.size(), 6);

    string.Erase(2);
    ASSERT_EQ(string.size(), 4);
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(string[i], 'c' + i);
    }
}

TEST(SmartStringTester, EraseTest_2) {
    Utils::SmartString string("a");

    ASSERT_EQ(string.size(), 1);

    string.Erase(1);
    ASSERT_EQ(string.size(), 0);
}

TEST(SmartStringTester, EraseTest_3) {
    Utils::SmartString string("a");

    ASSERT_EQ(string.size(), 1);

    string.Erase(2);
    ASSERT_EQ(string.size(), 0);
}

TEST(SmartStringTester, PutEraseTest) {
    Utils::SmartString string("a");

    ASSERT_EQ(string.size(), 1);

    string.Put("bcdef");
    ASSERT_EQ(string.size(), 6);
    for (int i = 0; i < 6; i++) {
        ASSERT_EQ(string[i], 'a' + i);
    }

    string.Erase(2);
    ASSERT_EQ(string.size(), 4);
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(string[i], 'c' + i);
    }

    string.Put("gh");
    ASSERT_EQ(string.size(), 6);
    for (int i = 0; i < 6; i++) {
        ASSERT_EQ(string[i], 'c' + i);
    }

    string.Erase(1);
    ASSERT_EQ(string.size(), 5);
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(string[i], 'd' + i);
    }

    string.Put("0123456789");
    ASSERT_EQ(string.size(), 15);
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(string[i], 'd' + i);
    }
    for (int i = 5; i < 15; i++) {
        ASSERT_EQ(string[i], '0' + i - 5);
    }
}