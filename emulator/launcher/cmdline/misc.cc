/* Copyright (C) 2007-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#include "android/utils/misc.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "absl/log/log.h"

#include "android/utils/stralloc.h"

#define E(...) ALOGE(__VA_ARGS__)

extern void print_tabular(const char** strings, int count, const char* prefix, int width) {
    int nrows, ncols, r, c, n, maxw = 0;

    for (n = 0; n < count; n++) {
        int len = strlen(strings[n]);
        if (len > maxw) maxw = len;
    }
    maxw += 2;
    ncols = width / maxw;
    nrows = (count + ncols - 1) / ncols;

    for (r = 0; r < nrows; r++) {
        printf("%s", prefix);
        for (c = 0; c < ncols; c++) {
            int index = c * nrows + r;
            if (index >= count) {
                break;
            }
            printf("%-*s", maxw, strings[index]);
        }
        printf("\n");
    }
}

extern void string_translate_char(char* str, char from, char to) {
    char* p = str;
    while (p != nullptr && (p = strchr(p, from)) != nullptr) *p++ = to;
}

extern void buffer_translate_char(char* buff, unsigned buff_len, const char* src, char from_char,
                                  char to_char) {
    int len = strlen(src);
    buffer_translate_char_with_len(buff, buff_len, src, len, from_char, to_char);
}

extern void buffer_translate_char_with_len(char* buff, unsigned buff_len, const char* src,
                                           unsigned src_len, char from_char, char to_char) {
    if (src_len >= buff_len) src_len = buff_len - 1;

    memcpy(buff, src, src_len);
    buff[src_len] = 0;

    string_translate_char(buff, from_char, to_char);
}

/** TEMP CHAR STRINGS
 **
 ** implement a circular ring of temporary string buffers
 **/

struct TempString {
    struct TempString* next;
    char* buffer;
    int size;
};

#define MAX_TEMP_STRINGS 16

static TempString temp_strings[MAX_TEMP_STRINGS];
static int temp_string_n;

extern char* tempstr_get(int size) {
    TempString* t = &temp_strings[temp_string_n];

    if (++temp_string_n >= MAX_TEMP_STRINGS) temp_string_n = 0;

    size += 1; /* reserve 1 char for terminating zero */

    if (t->size < size) {
        t->buffer = static_cast<char*>(realloc(t->buffer, size));
        if (t->buffer == nullptr) {
            LOG(FATAL) << "could not allocate " << size << " bytes";
        }
        t->size = size;
    }
    return t->buffer;
}

extern char* tempstr_format(const char* fmt, ...) {
    va_list args;
    char* result;
    STRALLOC_DEFINE(s);
    va_start(args, fmt);
    stralloc_formatv(s, fmt, args);
    va_end(args);
    result = stralloc_to_tempstr(s);
    stralloc_reset(s);
    return result;
}

/** QUOTING
 **
 ** dumps a human-readable version of a string. this replaces
 ** newlines with \n, etc...
 **/

extern const char* quote_bytes(const char* str, int len) {
    STRALLOC_DEFINE(s);
    char* q;

    stralloc_add_quote_bytes(s, str, len);
    q = stralloc_to_tempstr(s);
    stralloc_reset(s);
    return q;
}

extern const char* quote_str(const char* str) {
    int len = strlen(str);
    return quote_bytes(str, len);
}

/** HEXADECIMAL CHARACTER SEQUENCES
 **/

static int Hexdigit(int c) {
    unsigned d;

    d = static_cast<unsigned>(c - '0');
    if (d < 10) return d;

    d = static_cast<unsigned>(c - 'a');
    if (d < 6) return d + 10;

    d = static_cast<unsigned>(c - 'A');
    if (d < 6) return d + 10;

    return -1;
}

int hex2int(const uint8_t* hex, int len) {
    int result = 0;
    while (len > 0) {
        int c = Hexdigit(*hex++);
        if (c < 0) return -1;

        result = (result << 4) | c;
        len--;
    }
    return result;
}

void int2hex(uint8_t* hex, int len, int val) {
    static const uint8_t kHexchars[17] = "0123456789abcdef";
    while (--len >= 0) *hex++ = kHexchars[(val >> (len * 4)) & 15];
}

/** STRING PARAMETER PARSING
 **/

int strtoi(const char* nptr, char** endptr, int base) {
    long val;

    errno = 0;
    val = strtol(nptr, endptr, base);
    if (errno) {
        return (val == LONG_MAX) ? INT_MAX : INT_MIN;
    } else {
        if (val == static_cast<int>(val)) {
            return static_cast<int>(val);
        } else {
            errno = ERANGE;
            return val > 0 ? INT_MAX : INT_MIN;
        }
    }
}

int get_token_value(const char* params, const char* name, char* value, int val_size) {
    const char* val_end;
    int len = strlen(name);
    const char* par_end = params + strlen(params);
    const char* par_start = strstr(params, name);

    /* Search for 'name=' */
    while (par_start != nullptr) {
        /* Make sure that we're within the parameters buffer. */
        if ((par_end - par_start) < len) {
            par_start = nullptr;
            break;
        }
        /* Make sure that par_start starts at the beginning of <name>, and only
         * then check for '=' value separator. */
        if ((par_start == params || (*(par_start - 1) == ' ')) && par_start[len] == '=') {
            break;
        }
        /* False positive. Move on... */
        par_start = strstr(par_start + 1, name);
    }
    if (par_start == nullptr) {
        return -1;
    }

    /* Advance past 'name=', and calculate value's string length. */
    par_start += len + 1;
    val_end = strchr(par_start, ' ');
    if (val_end == nullptr) {
        val_end = par_start + strlen(par_start);
    }
    len = val_end - par_start;

    /* Check if fits... */
    if ((len + 1) <= val_size) {
        memcpy(value, par_start, len);
        value[len] = '\0';
        return 0;
    } else {
        return len + 1;
    }
}

int get_token_value_alloc(const char* params, const char* name, char** value) {
    char tmp;
    int res;

    /* Calculate size of string buffer required for the value. */
    const int val_size = get_token_value(params, name, &tmp, 0);
    if (val_size < 0) {
        *value = nullptr;
        return val_size;
    }

    /* Allocate string buffer, and retrieve the value. */
    *value = static_cast<char*>(malloc(val_size));
    if (*value == nullptr) {
        LOG(ERROR) << "Unable to allocated " << val_size << " bytes for string buffer.";

        return -2;
    }
    res = get_token_value(params, name, *value, val_size);
    if (res) {
        LOG(ERROR) << "Unable to retrieve value into allocated buffer.";
        free(*value);
        *value = nullptr;
    }

    return res;
}

int get_token_value_int(const char* params, const char* name, int* value) {
    char val_str[64];  // Should be enough for all numeric values.
    if (!get_token_value(params, name, val_str, sizeof(val_str))) {
        errno = 0;
        *value = strtoi(val_str, nullptr, 10);
        if (errno) {
            LOG(ERROR) << "Value '" << val_str << "' of the parameter '" << name << "' in '"
                       << params << "' is not a decimal number.";
            return -2;
        } else {
            return 0;
        }
    } else {
        return -1;
    }
}
