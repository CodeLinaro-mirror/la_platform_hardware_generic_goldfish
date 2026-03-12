// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "goldfish/parsing/arg_stream.h"

#include <gtest/gtest.h>

namespace goldfish::parsing {

TEST(ArgStreamTest, BasicParsing) {
    ArgStream args("foo bar  baz");
    EXPECT_EQ(args.Next(), "foo");
    EXPECT_EQ(args.Next(), "bar");
    EXPECT_EQ(args.Next(), "baz");
    EXPECT_TRUE(args.Empty());
    EXPECT_EQ(args.Next(), "");
}

TEST(ArgStreamTest, Peek) {
    ArgStream args("foo bar");
    EXPECT_EQ(args.Peek(), "foo");
    EXPECT_EQ(args.Peek(), "foo");  // Multiple peeks return same result
    EXPECT_EQ(args.Next(), "foo");  // Next returns peeked result
    EXPECT_EQ(args.Peek(), "bar");
}

TEST(ArgStreamTest, Remaining) {
    ArgStream args("cmd arg1 arg2 arg3");
    args.Next();  // consume cmd
    EXPECT_EQ(args.Remaining(), "arg1 arg2 arg3");
    args.Next();
    EXPECT_EQ(args.Remaining(), "arg2 arg3");
}

TEST(ArgStreamTest, NextInt) {
    ArgStream args("123 abc");

    // Success case
    auto val = args.NextInt();
    ASSERT_TRUE(val.ok());
    EXPECT_EQ(*val, 123);

    // Failure case: should NOT consume the token
    auto err = args.NextInt();
    EXPECT_FALSE(err.ok());
    EXPECT_EQ(args.Peek(), "abc");  // Verification that token is still there
    EXPECT_EQ(args.Next(), "abc");  // Now we consume it as string
}

TEST(ArgStreamTest, NextDouble) {
    ArgStream args("3.14 -0.001 1e6 2.5e-3 invalid");

    // Success cases
    EXPECT_DOUBLE_EQ(*args.NextDouble(), 3.14);
    EXPECT_DOUBLE_EQ(*args.NextDouble(), -0.001);
    EXPECT_DOUBLE_EQ(*args.NextDouble(), 1000000.0);
    EXPECT_DOUBLE_EQ(*args.NextDouble(), 0.0025);

    // Failure case: should NOT consume
    auto err = args.NextDouble();
    EXPECT_FALSE(err.ok());
    EXPECT_EQ(args.Peek(), "invalid");
}

TEST(ArgStreamTest, NextBool) {
    ArgStream args("on off true false yes no 1 0 True False Yes No ON OFF YES NO invalid");
    EXPECT_TRUE(*args.NextBool());
    EXPECT_FALSE(*args.NextBool());
    EXPECT_TRUE(*args.NextBool());
    EXPECT_FALSE(*args.NextBool());
    EXPECT_TRUE(*args.NextBool());
    EXPECT_FALSE(*args.NextBool());
    EXPECT_TRUE(*args.NextBool());
    EXPECT_FALSE(*args.NextBool());
    EXPECT_TRUE(*args.NextBool());
    EXPECT_FALSE(*args.NextBool());
    EXPECT_TRUE(*args.NextBool());
    EXPECT_FALSE(*args.NextBool());
    EXPECT_TRUE(*args.NextBool());
    EXPECT_FALSE(*args.NextBool());
    EXPECT_TRUE(*args.NextBool());
    EXPECT_FALSE(*args.NextBool());

    // Failure case: should NOT consume
    auto err = args.NextBool();
    EXPECT_FALSE(err.ok());
    EXPECT_EQ(args.Next(), "invalid");
}

TEST(ArgStreamTest, Empty) {
    ArgStream args("   ");
    EXPECT_TRUE(args.Empty());
    EXPECT_EQ(args.Peek(), "");
}

TEST(ArgStreamTest, LeadingTrailingWhitespace) {
    ArgStream args("  pull  ");
    EXPECT_FALSE(args.Empty());
    EXPECT_EQ(args.Next(), "pull");
    EXPECT_TRUE(args.Empty());
}

TEST(ArgStreamTest, BashLike_MidTokenQuoting) {
    // Verifies that quotes can start in the middle of a token (e.g. key="value").
    ArgStream args("name=\"my value\"");
    EXPECT_EQ(args.Next(), "name=my value");
    EXPECT_TRUE(args.Empty());
}

TEST(ArgStreamTest, BashLike_Concatenation) {
    // Verifies that multiple quoted and unquoted segments concatenate into one token.
    ArgStream args("prefix'middle'suffix");
    EXPECT_EQ(args.Next(), "prefixmiddlesuffix");
    EXPECT_TRUE(args.Empty());
}

TEST(ArgStreamTest, BashLike_ComplexMixing) {
    // Verifies a complex mix of quotes and escapes in a single token.
    // Equivalent to bash: printf "%s\n" start" middle "' end'\ and\ more
    ArgStream args(R"(start" middle "' end'\ and\ more)");
    EXPECT_EQ(args.Next(), "start middle  end and more");
    EXPECT_TRUE(args.Empty());
}

TEST(ArgStreamTest, QuotedArguments) {
    ArgStream args("pull \"my snapshot name\" /path/to/dest");
    EXPECT_EQ(args.Next(), "pull");
    EXPECT_EQ(args.Next(), "my snapshot name");
    EXPECT_EQ(args.Next(), "/path/to/dest");
}

TEST(ArgStreamTest, EscapedSpaces) {
    ArgStream args("start /path/with\\ spaces/file.webm");
    EXPECT_EQ(args.Next(), "start");
    EXPECT_EQ(args.Next(), "/path/with spaces/file.webm");
}

TEST(ArgStreamTest, TrailingEscape) {
    ArgStream args("cmd \\");
    EXPECT_EQ(args.Next(), "cmd");
    EXPECT_EQ(args.Next(), "\\");  // Trailing backslash preserved
}

TEST(ArgStreamTest, EmptyQuotedString) {
    ArgStream args(R"("")");
    EXPECT_FALSE(args.Empty());  // It's a token
    EXPECT_EQ(args.Next(), "");
    EXPECT_TRUE(args.Empty());
}

TEST(ArgStreamTest, UnterminatedQuotedString) {
    ArgStream args(R"("abc)");
    EXPECT_EQ(args.Next(), "abc");
    EXPECT_TRUE(args.Empty());
}

TEST(ArgStreamTest, LineMethod) {
    std::string input = "  cmd  arg1   arg2  ";
    ArgStream args(input);

    // Should return original string
    EXPECT_EQ(args.Line(), input);

    // Should still return original string after consumption
    args.Next();
    EXPECT_EQ(args.Line(), input);

    args.Next();
    args.Next();
    EXPECT_TRUE(args.Empty());
    EXPECT_EQ(args.Line(), input);
}

TEST(ArgStreamTest, LongLine) {
    std::string long_arg(5000, 'a');
    ArgStream args("cmd " + long_arg);
    EXPECT_EQ(args.Next(), "cmd");
    EXPECT_EQ(args.Next(), long_arg);
}

TEST(ArgStreamTest, ManySmallArguments) {
    std::string input;
    for (int i = 0; i < 1000; ++i) {
        input += std::to_string(i) + " ";
    }
    ArgStream args(input);
    for (int i = 0; i < 1000; ++i) {
        auto val = args.NextInt();
        ASSERT_TRUE(val.ok()) << "Failed at " << i;
        EXPECT_EQ(*val, i);
    }
}

TEST(ArgStreamTest, EscapedBackslash) {
    ArgStream args(R"(\\)");
    EXPECT_EQ(args.Next(), R"(\)");
}

TEST(ArgStreamTest, MixedQuotesNested) {
    {
        ArgStream args(R"("a'b")");
        EXPECT_EQ(args.Next(), "a'b");
    }
    {
        ArgStream args(R"('a"b')");
        EXPECT_EQ(args.Next(), R"(a"b)");
    }
}

TEST(ArgStreamTest, PeekAndRemainingConsistency) {
    ArgStream args("  arg1 arg2");
    EXPECT_EQ(args.Remaining(), "arg1 arg2");
    EXPECT_EQ(args.Peek(), "arg1");
    EXPECT_EQ(args.Remaining(), "arg1 arg2");  // Peek doesn't advance index
    args.Next();
    EXPECT_EQ(args.Remaining(), "arg2");
}

TEST(ArgStreamTest, TypeSafeExtractionEmptyStream) {
    ArgStream args("");
    EXPECT_FALSE(args.NextInt().ok());
    EXPECT_FALSE(args.NextDouble().ok());
    EXPECT_FALSE(args.NextBool().ok());
}

// --- Section: Legacy Compatibility Tests ---
// Our argument parse must be able to parse
// the "old style" commands.

TEST(ArgStreamTest, Legacy_ParseEscapedLaunchStringProperly) {
    // Input: "foo" " zoo " "A \"quote\"" "\\t\\n\""
    std::string parse_str = R"#("foo" " zoo " "A \"quote\"" "\\t\\n\"")#";
    ArgStream args(parse_str);

    EXPECT_EQ(args.Next(), "foo");
    EXPECT_EQ(args.Next(), " zoo ");
    EXPECT_EQ(args.Next(), "A \"quote\"");
    EXPECT_EQ(args.Next(), "\\t\\n\"");
    EXPECT_TRUE(args.Empty());
}

TEST(ArgStreamTest, Legacy_Quotes_Im_Doing_Great) {
    ArgStream args(R"#("I'm" doing great)#");
    EXPECT_EQ(args.Next(), "I'm");
    EXPECT_EQ(args.Next(), "doing");
    EXPECT_EQ(args.Next(), "great");
    EXPECT_TRUE(args.Empty());
}

TEST(ArgStreamTest, Legacy_Cant_Believe_Snowing) {
    ArgStream args(R"#("I can't believe it's snowing" 'again')#");
    EXPECT_EQ(args.Next(), "I can't believe it's snowing");
    EXPECT_EQ(args.Next(), "again");
    EXPECT_TRUE(args.Empty());
}

TEST(ArgStreamTest, Legacy_This_Is_Fine) {
    ArgStream args(R"#('"This is fine' "it's true")#");
    EXPECT_EQ(args.Next(), "\"This is fine");
    EXPECT_EQ(args.Next(), "it's true");
    EXPECT_TRUE(args.Empty());
}

TEST(ArgStreamTest, Legacy_Mixed_Quotes_Everywhere) {
    // This input specifically tests escaped single quotes inside single quotes: \'
    // Bash usually doesn't do this, but the legacy telnet parser did.
    // i.e.  printf "%s\n" '"Quotes \' are \' everwhere" in this one block \" string'
    // doesn't even parse in the shell.
    ArgStream args(R"#('"Quotes \' are \' everwhere" in this one block \" string')#");
    EXPECT_EQ(args.Next(), "\"Quotes ' are ' everwhere\" in this one block \" string");
    EXPECT_TRUE(args.Empty());
}

// --- Section: Strict Bash Compliance (Currently DISABLED) ---
// These tests represent areas where the current "Best Effort" parser
// deviates from the formal POSIX/Bash shell specification.
// Enable these if implementing the TODO in arg_stream.cc.
TEST(ArgStreamTest, DISABLED_Bash_SingleQuoteBackslashLiteral) {
    // In strict Bash, backslash is a literal character inside single quotes.
    // 'a\b' -> a\b
    // Current behavior: treats \ as escape, returns "ab"
    ArgStream args("'a\\b'");
    EXPECT_EQ(args.Next(), "a\\b");
}

TEST(ArgStreamTest, DISABLED_Bash_DoubleQuoteBackslashSelective) {
    // In strict Bash, backslash in DQ is ONLY an escape if followed by $ ` " \ or newline.
    // "a\b" -> a\b
    // Current behavior: treats \ as escape for ANY char, returns "ab"
    ArgStream args("\"a\\b\"");
    EXPECT_EQ(args.Next(), "a\\b");
}

TEST(ArgStreamTest, DISABLED_Bash_DoubleQuoteEscapeDoubleQuote) {
    // In strict Bash, \" inside DQ is "
    ArgStream args("\"a\\\"b\"");
    EXPECT_EQ(args.Next(), "a\"b");
}

}  // namespace goldfish::parsing
