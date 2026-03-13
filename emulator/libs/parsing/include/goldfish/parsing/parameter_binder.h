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
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "goldfish/parsing/arg_stream.h"

/**
 * @namespace goldfish::parsing
 * @brief Utilities for binding text-based argument streams to typed C++ functions.
 *
 * This header provides a bridge between the untyped world of command-line strings
 * (via ArgStream) and the strongly-typed world of C++ handlers. It uses template
 * metaprogramming to automatically inspect function signatures, parse the
 * necessary arguments from a stream, and invoke the function safely.
 */
namespace goldfish::parsing {

// --- Argument Extraction ---

/**
 * @brief Trait-based extractor for pulling specific types from an ArgStream.
 *
 * Each specialization of ArgExtractor knows how to consume one or more tokens
 * from the ArgStream and convert them into a specific C++ type.
 *
 * ### Extending the Binder
 * To support a new type `T`, simply provide a specialization of ArgExtractor<T>
 * that implements the static `Extract` method.
 */
template <typename T>
struct ArgExtractor;

/** @brief Parses exactly one token as an integer. */
template <>
struct ArgExtractor<int> {
    static absl::StatusOr<int> Extract(ArgStream& args) { return args.NextInt(); }
};

/** @brief Parses exactly one token as a double. */
template <>
struct ArgExtractor<double> {
    static absl::StatusOr<double> Extract(ArgStream& args) { return args.NextDouble(); }
};

/** @brief Parses exactly one token as a boolean (on/off, true/false, 1/0). */
template <>
struct ArgExtractor<bool> {
    static absl::StatusOr<bool> Extract(ArgStream& args) { return args.NextBool(); }
};

/** @brief Consumes exactly one token as a std::string. */
template <>
struct ArgExtractor<std::string> {
    static absl::StatusOr<std::string> Extract(ArgStream& args) {
        if (args.Empty()) return absl::InvalidArgumentError("Missing argument");
        return args.Next();
    }
};

/**
 * @brief Handles optional trailing parameters.
 *
 * If the ArgStream is already empty, this returns std::nullopt (success).
 * Otherwise, it attempts to extract the underlying type T.
 */
template <typename T>
struct ArgExtractor<std::optional<T>> {
    static absl::StatusOr<std::optional<T>> Extract(ArgStream& args) {
        if (args.Empty()) return std::optional<T>(std::nullopt);
        auto val = ArgExtractor<T>::Extract(args);
        if (!val.ok()) return val.status();
        return std::optional<T>(std::move(*val));
    }
};

// --- Function Traits ---

/**
 * @brief Base trait helper to extract and normalize argument metadata.
 *
 * @tparam Args The pack of arguments from the handler signature (excluding Context).
 */
template <typename... Args>
struct FunctionTraitsBase {
    /**
     * @brief A tuple containing the decay-normalized types of the arguments.
     *
     * We use `std::decay_t` to strip references and const qualifiers. This ensures
     * that our internal storage (the tuple) holds actual values rather than
     * potentially dangling references to temporary stream tokens.
     */
    using args_tuple = std::tuple<std::decay_t<Args>...>;

    /** @brief The number of arguments the handler expects (excluding the Context). */
    static constexpr std::size_t kArity = sizeof...(Args);
};

/**
 * @brief Utility for inspecting the signature of a C++ callable (lambda or functor).
 *
 * FunctionTraits extracts the argument list from a lambda's `operator()` and ensures
 * that the return type matches the supported protocol types (Status, string, etc.).
 *
 * If a lambda returns an unsupported type, the primary template will fail to match
 * the specializations below, resulting in a "no matching member function" error.
 */
template <typename T>
struct FunctionTraits : FunctionTraits<decltype(&T::operator())> {};

// --- Explicit Specializations for Supported Return Types ---
// These specializations "whitelist" allowed return types and support any Context type
// as the first argument of the handler.
//
// Mechanics: We match `C::*` because lambdas are compiled into anonymous classes
// with a member `operator()`.
// @see https://blog.feabhas.com/2014/03/demystifying-c-lambdas/

// Specializations for standard (const) lambdas:
template <typename C, typename Context, typename... Args>
struct FunctionTraits<absl::StatusOr<std::string> (C::*)(Context&, Args...) const>
        : FunctionTraitsBase<Args...> {
    // Export the context type so it can be accessed from outside the trait struct.
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<absl::Status (C::*)(Context&, Args...) const> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<std::string (C::*)(Context&, Args...) const> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<const char* (C::*)(Context&, Args...) const> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

// Specializations for 'mutable' lambdas:
template <typename C, typename Context, typename... Args>
struct FunctionTraits<absl::StatusOr<std::string> (C::*)(Context&, Args...)>
        : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<absl::Status (C::*)(Context&, Args...)> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<std::string (C::*)(Context&, Args...)> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<const char* (C::*)(Context&, Args...)> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

// Specializations for ref-qualified lambdas (& and &&):
template <typename C, typename Context, typename... Args>
struct FunctionTraits<absl::StatusOr<std::string> (C::*)(Context&, Args...) &&>
        : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<absl::Status (C::*)(Context&, Args...) &&> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<std::string (C::*)(Context&, Args...) &&> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<const char* (C::*)(Context&, Args...) &&> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<absl::StatusOr<std::string> (C::*)(Context&, Args...) &>
        : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<absl::Status (C::*)(Context&, Args...) &> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<std::string (C::*)(Context&, Args...) &> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

template <typename C, typename Context, typename... Args>
struct FunctionTraits<const char* (C::*)(Context&, Args...) &> : FunctionTraitsBase<Args...> {
    using ContextType = Context;
};

// --- Result Normalization ---

/**
 * @brief Unifies different handler return types into a standard absl::StatusOr<std::string>.
 *
 * This allows handlers to return simple types like `const char*` or `absl::Status`
 * without having to explicitly wrap them in `absl::StatusOr<std::string>`.
 *
 * Example:
 * @code
 *   [](Context& ctx) { return "ok"; } // NormalizeResult handles the conversion.
 * @endcode
 */
inline absl::StatusOr<std::string> NormalizeResult(absl::Status status) {
    if (!status.ok()) return status;
    return "";
}

inline absl::StatusOr<std::string> NormalizeResult(std::string output) {
    return std::move(output);
}

inline absl::StatusOr<std::string> NormalizeResult(const char* output) {
    if (output == nullptr) return "";
    return std::string(output);
}

inline absl::StatusOr<std::string> NormalizeResult(absl::StatusOr<std::string> result) {
    return result;
}

// --- Invocation Helper ---

/**
 * @brief Orchestrates the extraction of arguments and the execution of the handler.
 *
 * This function bridges the gap between the untyped ArgStream and the typed lambda.
 *
 * @tparam F The callable (lambda/functor) to execute.
 * @tparam Context The type of the first argument required by the handler.
 * @tparam Is A pack of indices (0, 1, 2...) used to unpack the arguments.
 */
template <typename F, typename Context, std::size_t... Is,
          typename Traits = FunctionTraits<std::decay_t<F>>,
          typename ArgsTuple = typename Traits::args_tuple>
absl::StatusOr<std::string> InvokeFromStream(F&& func, Context& ctx, ArgStream& args,
                                             std::index_sequence<Is...>) {
    /**
     * Unroll the ArgStream parsing into a tuple.
     *
     * We use braced-initialization `{...}` to guarantee left-to-right evaluation
     * order. This ensures that arguments are consumed from the ArgStream in the
     * same order they appear in the function signature.
     * @see https://en.cppreference.com/w/cpp/language/eval_order
     *
     * Logic: For each index 'I' in 'Is', we look up the 'I-th' required type in
     * the Traits and call the corresponding ArgExtractor.
     *
     * @see https://www.cppstories.com/2022/tuple-iteration-basics
     * @see https://eli.thegreenplace.net/2014/variadic-templates-in-c
     */
    auto parsed_args =
            std::tuple{ArgExtractor<std::tuple_element_t<Is, ArgsTuple>>::Extract(args)...};

    /**
     * Validate all extracted arguments.
     *
     * Using a fold expression with '&&' allows us to stop and return the first
     * error encountered during parsing.
     */
    absl::Status s;
    const bool all_ok = ((s = std::get<Is>(parsed_args).status()).ok() && ...);
    if (!all_ok) {
        return s;
    }

    /**
     * Enforce strict argument counts.
     */
    if (!args.Empty()) {
        return absl::InvalidArgumentError("Too many arguments");
    }

    /**
     * Invoke the handler.
     *
     * We use std::move and the dereference operator (*) to "move" the parsed
     * values out of the StatusOr wrappers and directly into the function call.
     * Finally, we Normalize the result to satisfy the protocol return type.
     */
    return NormalizeResult(std::forward<F>(func)(ctx, std::move(*std::get<Is>(parsed_args))...));
}

/**
 * @brief Public entry point to bind and invoke a handler from an ArgStream.
 *
 * This wrapper calculates the function arity and generates the necessary
 * index sequence internally, providing a clean API for callers.
 *
 * ### Example:
 * @code
 *   auto handler = [](MyContext& ctx, int id, std::string name) {
 *       return absl::StrCat("ID: ", id, " Name: ", name);
 *   };
 *   ArgStream args("123 \"John Doe\"");
 *   auto result = BindAndInvoke(handler, my_ctx, args);
 * @endcode
 *
 * @param func The callable (lambda/functor) to execute.
 * @param ctx The context object to pass as the first argument.
 * @param args The stream of arguments to parse.
 * @return The result of the function call, normalized to StatusOr<string>.
 */
template <typename F, typename Context>
absl::StatusOr<std::string> BindAndInvoke(F&& func, Context& ctx, ArgStream& args) {
    using Traits = FunctionTraits<std::decay_t<F>>;
    return InvokeFromStream(std::forward<F>(func), ctx, args,
                            std::make_index_sequence<Traits::kArity>{});
}
}  // namespace goldfish::parsing
