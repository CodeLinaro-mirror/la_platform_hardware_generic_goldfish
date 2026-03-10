#if defined(_WIN32)

#include <winsock2.h>

class Environment : public ::testing::Environment {
  public:
    void SetUp() override {
        WSADATA Data;
        WSAStartup(MAKEWORD(2, 2), &Data);
    }

    void TearDown() override { WSACleanup(); }
};

testing::Environment* const env = testing::AddGlobalTestEnvironment(new Environment);
#endif
