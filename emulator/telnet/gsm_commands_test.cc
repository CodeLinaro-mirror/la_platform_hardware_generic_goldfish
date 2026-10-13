#include "gsm_commands.h"

#include <gtest/gtest.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "console_context.h"

namespace goldfish::telnet {
namespace {

class GsmCommandsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        builder_ = std::make_unique<CommandRegistryBuilder>("/dummy/token");
        auto gsm = builder_->Command("gsm", "GSM commands");
        RegisterGsmCommands(gsm);
        registry_ = builder_->Build();
    }

    std::unique_ptr<CommandRegistryBuilder> builder_;
    std::unique_ptr<CommandRegistry> registry_;
};

TEST_F(GsmCommandsTest, GsmListReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm list", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result.status().message(), kGsmDeprecatedMessage);
}

TEST_F(GsmCommandsTest, GsmStatusReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm status", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result.status().message(), kGsmDeprecatedMessage);
}

TEST_F(GsmCommandsTest, GsmCallReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm call 1234567", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result.status().message(), kGsmDeprecatedMessage);
}

TEST_F(GsmCommandsTest, GsmCancelReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm cancel 1234567", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result.status().message(), kGsmDeprecatedMessage);
}

TEST_F(GsmCommandsTest, GsmDataReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm data roaming", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result.status().message(), kGsmDeprecatedMessage);
}

TEST_F(GsmCommandsTest, GsmSignalReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm signal 15", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result.status().message(), kGsmDeprecatedMessage);
}

TEST_F(GsmCommandsTest, GsmBusyReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm busy 1234567", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result.status().message(), kGsmDeprecatedMessage);
}

TEST_F(GsmCommandsTest, GsmHoldReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm hold 1234567", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result.status().message(), kGsmDeprecatedMessage);
}

TEST_F(GsmCommandsTest, GsmAcceptReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm accept 1234567", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result.status().message(), kGsmDeprecatedMessage);
}

TEST_F(GsmCommandsTest, GsmVoiceReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm voice roaming", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result.status().message(), kGsmDeprecatedMessage);
}

TEST_F(GsmCommandsTest, GsmSignalProfileReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm signal-profile 3", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result.status().message(), kGsmDeprecatedMessage);
}

TEST_F(GsmCommandsTest, GsmMeterReturnsUnimplemented) {
    ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result_on = (*registry_)("gsm meter on", ctx);
    ASSERT_FALSE(result_on.ok());
    EXPECT_EQ(result_on.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result_on.status().message(), kGsmDeprecatedMessage);

    auto result_off = (*registry_)("gsm meter off", ctx);
    ASSERT_FALSE(result_off.ok());
    EXPECT_EQ(result_off.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result_off.status().message(), kGsmDeprecatedMessage);
}

}  // namespace
}  // namespace goldfish::telnet
