#include "command_registry.h"

#include <gtest/gtest.h>

#include "absl/status/status.h"

namespace goldfish::telnet {
using ArgStream = goldfish::parsing::ArgStream;

class CommandRegistryTest : public ::testing::Test {
  protected:
    void SetUp() override {
        builder_ = std::make_unique<CommandRegistryBuilder>("/tmp/test_token");
    }

    std::unique_ptr<CommandRegistry> Build() { return builder_->Build(); }

    std::unique_ptr<CommandRegistryBuilder> builder_;
};

TEST_F(CommandRegistryTest, BasicCommand) {
    builder_->Command("test", "test command")
            .Safe()
            .Handler([](LineCommandHandler::Context&, ArgStream&) { return "executed"; });

    auto registry = Build();
    LineCommandHandler::Context ctx;
    auto res = (*registry)("test", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(*res, "executed");
}

TEST_F(CommandRegistryTest, CommandAliasing) {
    builder_->Command("quit|exit", "close session")
            .Safe()
            .Handler([](LineCommandHandler::Context&, ArgStream&) { return "goodbye"; });

    auto registry = Build();
    LineCommandHandler::Context ctx;

    // Both 'quit' and 'exit' should work
    auto res = (*registry)("quit", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(*res, "goodbye");

    res = (*registry)("exit", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(*res, "goodbye");
}

TEST_F(CommandRegistryTest, EmptyLines) {
    auto registry = Build();
    LineCommandHandler::Context ctx;
    auto res = (*registry)("", ctx);
    ASSERT_FALSE(res.ok());
    EXPECT_EQ(res.status().code(), absl::StatusCode::kNotFound);

    res = (*registry)("   ", ctx);
    ASSERT_FALSE(res.ok());
    EXPECT_EQ(res.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(CommandRegistryTest, HelpFormatting) {
    builder_->Command("ping", "check alive").Safe();
    builder_->Command("avd", "virtual device control")
            .Safe()
            .Sub("name", "query device name")
            .Handler([](auto&, auto&) { return "emu"; });

    auto registry = Build();
    LineCommandHandler::Context ctx;

    // Standard help
    auto res = (*registry)("help", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_TRUE(res->find("Android console commands:") != std::string::npos);
    EXPECT_TRUE(res->find("ping") != std::string::npos);

    // Verbose help
    res = (*registry)("help-verbose", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_TRUE(res->find("Android console command help:") != std::string::npos);
    EXPECT_TRUE(res->find("ping             check alive") != std::string::npos);
}

TEST_F(CommandRegistryTest, SecurityInheritance) {
    builder_->Command("safe", "parent")
            .Safe()
            .Sub("child", "inherited safety")
            .Handler([](LineCommandHandler::Context&, ArgStream&) { return "win"; });

    auto registry = Build();
    LineCommandHandler::Context ctx;
    ctx.authenticated = false;
    auto res = (*registry)("safe child", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(*res, "win");
}

TEST_F(CommandRegistryTest, AuthenticationEnforcement) {
    builder_->Command("secret", "hidden").Handler([](LineCommandHandler::Context&, ArgStream&) {
        return "hacked";
    });

    auto registry = Build();
    LineCommandHandler::Context ctx;
    ctx.authenticated = false;

    // Should be rejected with NotFoundError to avoid leaking existence
    auto res = (*registry)("secret", ctx);
    ASSERT_FALSE(res.ok());
    EXPECT_EQ(res.status().code(), absl::StatusCode::kNotFound);

    // After auth, should work
    ctx.authenticated = true;
    res = (*registry)("secret", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(*res, "hacked");
}

TEST_F(CommandRegistryTest, TypedArguments) {
    builder_->Command("location", "location management")
            .Safe()
            .Sub("gps", "gps control")
            .On("set", "set gps location",
                [](LineCommandHandler::Context&, double lat, std::optional<double> lon) {
                    return "Location updated";
                });

    auto registry = Build();
    LineCommandHandler::Context ctx;
    auto res = (*registry)("location gps set 12.3 45.6", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(*res, "Location updated");
}

TEST_F(CommandRegistryTest, CustomContextExtensibility) {
    struct MySession : public LineCommandHandler::Context {
        std::string userId = "user123";
    };

    builder_->Command("whoami", "show user info")
            .Safe()
            .On("id", "show user id", [](MySession& ctx) { return ctx.userId; });

    auto registry = Build();
    MySession my_session;
    my_session.userId = "custom_id";

    auto res = (*registry)("whoami id", my_session);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(*res, "custom_id");
}

TEST_F(CommandRegistryTest, RootLevelOnMethod) {
    builder_->On("root", "abstract", [](LineCommandHandler::Context&) { return "ok"; }).Safe();
    auto registry = Build();
    LineCommandHandler::Context ctx;
    auto res = (*registry)("root", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(*res, "ok");
}

TEST_F(CommandRegistryTest, AbstractVsDescription) {
    const std::string kDetailedHelp = "Line 1\r\nLine 2\r\nLine 3";
    builder_->Command("detailed", "summary", kDetailedHelp)
            .Safe()
            .Handler([](LineCommandHandler::Context&, ArgStream&) -> absl::StatusOr<std::string> {
                return "done";
            });

    auto registry = Build();
    LineCommandHandler::Context ctx;

    // Verbose root help should show abstract
    auto res = (*registry)("help-verbose", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_TRUE(res->find("detailed         summary") != std::string::npos);
    EXPECT_TRUE(res->find(kDetailedHelp) == std::string::npos);

    // Command specific help should show description
    res = (*registry)("help detailed", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_TRUE(res->find(kDetailedHelp) != std::string::npos);
}

TEST_F(CommandRegistryTest, SubCommandAbstractVsDescription) {
    const std::string kDetailedSubHelp = "Detailed Sub Help";
    auto cmd = builder_->Command("parent", "parent summary");
    cmd.Safe();
    cmd.On("child", "child summary", kDetailedSubHelp,
           [](LineCommandHandler::Context&) -> absl::StatusOr<std::string> { return "ok"; });

    auto registry = Build();
    LineCommandHandler::Context ctx;

    // Help for parent should show child abstract
    auto res = (*registry)("help parent", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_TRUE(res->find("child summary") != std::string::npos);
    EXPECT_TRUE(res->find(kDetailedSubHelp) == std::string::npos);

    // Help for child should show child description
    res = (*registry)("help parent child", ctx);
    ASSERT_TRUE(res.ok());
    EXPECT_TRUE(res->find(kDetailedSubHelp) != std::string::npos);
}

}  // namespace goldfish::telnet
