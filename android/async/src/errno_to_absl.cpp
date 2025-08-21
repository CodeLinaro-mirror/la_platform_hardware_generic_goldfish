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
#include "goldfish/async/errno_to_absl.h"

#include <cerrno>
#include <cstring>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace goldfish::async {

absl::Status ErrnoToAbslStatus(int error_code) {
    if (error_code == 0) {
        return absl::OkStatus();
    }

    absl::StatusCode absl_code = absl::StatusCode::kUnknown;
    switch (error_code) {
    // Operation not permitted / Permission denied
    case EPERM:
    case EACCES:
    case EROFS:
        absl_code = absl::StatusCode::kPermissionDenied;
        break;

    // No such file or directory / No such process
    case ENOENT:
    case ESRCH:
    case ENXIO:
    case ENODEV:
        absl_code = absl::StatusCode::kNotFound;
        break;

    // Interrupted system call
    case EINTR:
#ifdef ECANCELED
    case ECANCELED:
#endif
        absl_code = absl::StatusCode::kCancelled;
        break;

    // I/O error
    case EIO:
        absl_code = absl::StatusCode::kInternal;
        break;

    // Bad file number / Invalid argument
    case EBADF:
    case EINVAL:
    case ENOTTY:
    case ESPIPE:
    case EDESTADDRREQ:
    case EPROTOTYPE:
    case ENOTSOCK:
        absl_code = absl::StatusCode::kInvalidArgument;
        break;

    // Try again / Resource temporarily unavailable
    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
#endif
    case ENOMEM:
    case ENOBUFS:
    case ENFILE:
    case EMFILE:
    case ENOSPC:
    case EBUSY:
    case EMLINK:
        absl_code = absl::StatusCode::kResourceExhausted;
        break;

    // File exists
    case EEXIST:
    case EADDRINUSE:
        absl_code = absl::StatusCode::kAlreadyExists;
        break;

    // Cross-device link / Not a directory / Is a directory
    case EXDEV:
    case ENOTDIR:
    case EISDIR:
    case ENOTEMPTY:
    case EPIPE:
    case ECONNREFUSED:
    case ECONNRESET:
    case EISCONN:
    case ENOTCONN:
#ifdef ESHUTDOWN
    case ESHUTDOWN:
#endif
    case EALREADY:
        absl_code = absl::StatusCode::kFailedPrecondition;
        break;

    // File too large / Result too large
    case EFBIG:
    case ERANGE:
    case EMSGSIZE:
    case ENAMETOOLONG:
    case EOVERFLOW:
        absl_code = absl::StatusCode::kOutOfRange;
        break;

    // No such device or address
    case EADDRNOTAVAIL:
        absl_code = absl::StatusCode::kNotFound;
        break;

    // Function not implemented
    case ENOSYS:
    case ENOPROTOOPT:
    case EPROTONOSUPPORT:
#ifdef ESOCKTNOSUPPORT
    case ESOCKTNOSUPPORT:
#endif
#ifdef EOPNOTSUPP
    case EOPNOTSUPP:
#endif
#ifdef EPFNOSUPPORT
    case EPFNOSUPPORT:
#endif
    case EAFNOSUPPORT:
        absl_code = absl::StatusCode::kUnimplemented;
        break;

    // Network is down / Network is unreachable / Host is down
    case ENETDOWN:
    case ENETUNREACH:
    case EHOSTUNREACH:
#ifdef ENONET
    case ENONET:
#endif
#ifdef ETXTBSY
    case ETXTBSY:
#endif
        absl_code = absl::StatusCode::kUnavailable;
        break;

    // Connection timed out
    case ETIMEDOUT:
        absl_code = absl::StatusCode::kDeadlineExceeded;
        break;

    // Connection aborted
    case ECONNABORTED:
#ifdef EOWNERDEAD
    case EOWNERDEAD:
#endif
#ifdef ENOTRECOVERABLE
    case ENOTRECOVERABLE:
#endif
        absl_code = absl::StatusCode::kAborted;
        break;

    // Protocol error
    case EPROTO:
        absl_code = absl::StatusCode::kInternal;
        break;
    }

    std::string message = absl::StrCat(strerror(error_code), " (errno=", error_code, ")");
    return absl::Status(absl_code, message);
}

}  // namespace goldfish::async
