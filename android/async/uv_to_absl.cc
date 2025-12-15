// Copyright (C) 2025 The Android Open Source Project
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
#include "goldfish/async/uv_to_absl.h"

#include <uv.h>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace goldfish::async {
absl::Status UvErrToAbslStatus(int error_code) {
    if (error_code >= 0) {
        return absl::OkStatus();
    }

    absl::StatusCode absl_code = absl::StatusCode::kUnknown;
    switch (error_code) {
    // --- Resource Management ---
    case UV_EAGAIN:
    case UV_EBUSY:
    case UV_EMFILE:
    case UV_ENFILE:
    case UV_ENOBUFS:
    case UV_ENOMEM:
    case UV_ENOSPC:
    case UV_EMLINK:
        absl_code = absl::StatusCode::kResourceExhausted;
        break;

    // --- Permissions and State ---
    case UV_EACCES:
    case UV_EPERM:
    case UV_EROFS:
        absl_code = absl::StatusCode::kPermissionDenied;
        break;
    case UV_EALREADY:
    case UV_ECONNREFUSED:
    case UV_ECONNRESET:
    case UV_EISCONN:
    case UV_ENOTCONN:
    case UV_ESHUTDOWN:
    case UV_EPIPE:
        absl_code = absl::StatusCode::kFailedPrecondition;
        break;

    // --- Arguments and Input ---
    case UV_E2BIG:
    case UV_EAI_BADFLAGS:
    case UV_EAI_BADHINTS:
    case UV_EBADF:
    case UV_ECHARSET:
    case UV_EDESTADDRREQ:
    case UV_EFAULT:
    case UV_EINVAL:
    case UV_ENOTSOCK:
    case UV_EPROTOTYPE:
    case UV_ESPIPE:
    case UV_ENOTTY:
    case UV_EFTYPE:
    case UV_EILSEQ:
        absl_code = absl::StatusCode::kInvalidArgument;
        break;

    // --- Existence and Not Found ---
    case UV_EADDRNOTAVAIL:
    case UV_EAI_NODATA:
    case UV_EAI_NONAME:
    case UV_ENODEV:
    case UV_ENOENT:
    case UV_ESRCH:
    case UV_ENXIO:
        absl_code = absl::StatusCode::kNotFound;
        break;
    case UV_EADDRINUSE:
    case UV_EEXIST:
        absl_code = absl::StatusCode::kAlreadyExists;
        break;

    // --- Unimplemented and Unsupported ---
    case UV_EAFNOSUPPORT:
    case UV_EAI_ADDRFAMILY:
    case UV_EAI_FAMILY:
    case UV_EAI_PROTOCOL:
    case UV_EAI_SERVICE:
    case UV_EAI_SOCKTYPE:
    case UV_ENOPROTOOPT:
    case UV_ENOSYS:
    case UV_ENOTSUP:
    case UV_EPROTONOSUPPORT:
    case UV_ESOCKTNOSUPPORT:
    case UV_EUNATCH:
        absl_code = absl::StatusCode::kUnimplemented;
        break;

    // --- Network and Availability ---
    case UV_EAI_AGAIN:
    case UV_EHOSTUNREACH:
    case UV_ENETDOWN:
    case UV_ENETUNREACH:
    case UV_ENONET:
    case UV_ETXTBSY:
        absl_code = absl::StatusCode::kUnavailable;
        break;
    case UV_ETIMEDOUT:
        absl_code = absl::StatusCode::kDeadlineExceeded;
        break;

    // --- Cancellation and Interruption ---
    case UV_EAI_CANCELED:
    case UV_ECANCELED:
    case UV_EINTR:
        absl_code = absl::StatusCode::kCancelled;
        break;
    case UV_ECONNABORTED:
        absl_code = absl::StatusCode::kAborted;
        break;

    // --- Ranges and Boundaries ---
    case UV_EAI_OVERFLOW:
    case UV_EFBIG:
    case UV_ELOOP:
    case UV_EMSGSIZE:
    case UV_ENAMETOOLONG:
    case UV_EOVERFLOW:
    case UV_ERANGE:
    case UV_EOF:
        absl_code = absl::StatusCode::kOutOfRange;
        break;

    // --- Internal and Protocol Errors ---
    case UV_EIO:
    case UV_EPROTO:
        absl_code = absl::StatusCode::kInternal;
        break;
    case UV_EAI_FAIL:
    case UV_EISDIR:
    case UV_ENOTDIR:
    case UV_ENOTEMPTY:
    case UV_EXDEV:
        absl_code = absl::StatusCode::kFailedPrecondition;
        break;

    default:
        absl_code = absl::StatusCode::kInternal;
        break;
    }

    const std::string message =
            absl::StrCat(uv_strerror(error_code), " (", uv_err_name(error_code), ")");
    return {absl_code, message};
}
}  // namespace goldfish::async