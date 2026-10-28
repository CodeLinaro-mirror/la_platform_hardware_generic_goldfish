/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1  // NOLINT(readability-identifier-naming): dladdr()
#endif

#include "gfxstream_backend_loader.h"

#include <errno.h>
#include <glib.h>
#include <gmodule.h>
#include <stdbool.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#define GFXSTREAM_BACKEND_LIBRARY_NAME "gfxstream_backend.dll"
#else
#include <dlfcn.h>
#if defined(__APPLE__)
#define GFXSTREAM_BACKEND_LIBRARY_NAME "libgfxstream_backend.dylib"
#else
#define GFXSTREAM_BACKEND_LIBRARY_NAME "libgfxstream_backend.so"
#endif
#endif

enum {
    GFXSTREAM_LOAD_STATE_LOADED = 1,
    GFXSTREAM_LOAD_STATE_FAILED = 2,
};

static struct gfxstream_backend_fns s_fns;
static GModule* s_module;
static gsize s_load_state = 0;
static char* s_error;
static int s_load_errno;

static GModule* GfxstreamBackendOpen(const char* path, GString* errors) {
    GModule* module = g_module_open(path, G_MODULE_BIND_LOCAL);
    if (!module) {
        const char* err = g_module_error();
        g_string_append_printf(errors, "\n  %s: %s", path, err ? err : "g_module_open failed");
    }
    return module;
}

/* Directory containing the goldfish plugin itself (libgfxstream_backend ships next to it). */
static char* GfxstreamBackendPluginDir(void) {
#if defined(_WIN32)
    HMODULE module = NULL;
    wchar_t wpath[4096];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(uintptr_t)&GfxstreamBackendPluginDir, &module)) {
        return NULL;
    }
    DWORD len = GetModuleFileNameW(module, wpath, G_N_ELEMENTS(wpath));
    if (len == 0 || len >= G_N_ELEMENTS(wpath)) {
        return NULL;
    }
    char* path = g_utf16_to_utf8((const gunichar2*)wpath, -1, NULL, NULL, NULL);
    if (!path) {
        return NULL;
    }
    char* dir = g_path_get_dirname(path);
    g_free(path);
    return dir;
#else
    Dl_info info;
    if (!dladdr((const void*)&GfxstreamBackendPluginDir, &info) || !info.dli_fname) {
        return NULL;
    }
    return g_path_get_dirname(info.dli_fname);
#endif
}

static gsize GfxstreamBackendDoLoad(void) {
    GString* errors = g_string_new(NULL);
    const char* lib_dir = g_getenv("ANDROID_EMULATOR_LIBRARY_DIR");
    char* plugin_dir = NULL;
    bool any_candidate_existed = false;

    if (lib_dir && *lib_dir) {
        char* path = g_build_filename(lib_dir, GFXSTREAM_BACKEND_LIBRARY_NAME, NULL);
        if (g_file_test(path, G_FILE_TEST_EXISTS)) {
            any_candidate_existed = true;
        }
        s_module = GfxstreamBackendOpen(path, errors);
        g_free(path);
    }
    if (!s_module) {
        plugin_dir = GfxstreamBackendPluginDir();
        if (plugin_dir && (!lib_dir || strcmp(plugin_dir, lib_dir) != 0)) {
            char* path = g_build_filename(plugin_dir, GFXSTREAM_BACKEND_LIBRARY_NAME, NULL);
            if (g_file_test(path, G_FILE_TEST_EXISTS)) {
                any_candidate_existed = true;
            }
            s_module = GfxstreamBackendOpen(path, errors);
            g_free(path);
        }
    }
    g_free(plugin_dir);

    if (!s_module && !(lib_dir && *lib_dir)) {
        /*
         * Dynamic loader search path (LD_LIBRARY_PATH, DYLD_LIBRARY_PATH, rpath,
         * PATH), only when no packaged library directory was given: a packaged
         * emulator whose copy fails to load must report that rather than
         * silently pick up a foreign libgfxstream_backend from the system.
         */
        s_module = GfxstreamBackendOpen(GFXSTREAM_BACKEND_LIBRARY_NAME, errors);
    }
    if (!s_module) {
        s_load_errno = any_candidate_existed ? EIO : ENOENT;
        s_error = g_strdup_printf("failed to load " GFXSTREAM_BACKEND_LIBRARY_NAME
                                  " (ANDROID_EMULATOR_LIBRARY_DIR=%s):%s",
                                  lib_dir ? lib_dir : "<unset>", errors->str);
        g_string_free(errors, TRUE);
        return GFXSTREAM_LOAD_STATE_FAILED;
    }

    GString* missing = g_string_new(NULL);
#define GFXSTREAM_BACKEND_RESOLVE_SLOT(slot, ret, params, args)                           \
    do {                                                                                  \
        gpointer sym_ = NULL;                                                             \
        if (g_module_symbol(s_module, "stream_renderer_" #slot, &sym_) && sym_ != NULL) { \
            memcpy(&s_fns.slot, &sym_, sizeof(s_fns.slot));                               \
        } else {                                                                          \
            g_string_append_printf(missing, " stream_renderer_" #slot);                   \
        }                                                                                 \
    } while (0);
    GFXSTREAM_BACKEND_FOR_EACH_FN(GFXSTREAM_BACKEND_RESOLVE_SLOT)
#undef GFXSTREAM_BACKEND_RESOLVE_SLOT

    gsize state;
    if (missing->len > 0) {
        s_load_errno = ENOSYS;
        s_error = g_strdup_printf(GFXSTREAM_BACKEND_LIBRARY_NAME " is missing required symbols:%s",
                                  missing->str);
        memset(&s_fns, 0, sizeof(s_fns));
        /* Keep the module mapped: the library is never unloaded once mapped. */
        state = GFXSTREAM_LOAD_STATE_FAILED;
    } else {
        s_load_errno = 0;
        state = GFXSTREAM_LOAD_STATE_LOADED;
    }
    g_string_free(missing, TRUE);
    g_string_free(errors, TRUE);
    return state;
}

const struct gfxstream_backend_fns* gfxstream_backend_load(char** error_msg, int* out_errno) {
    /*
     * Use g_once_init_enter/leave rather than g_once(): GLib's g_once() macro
     * performs an unlocked atomic load of once->status while g_once_impl()
     * writes G_ONCE_STATUS_PROGRESS non-atomically, tripping ThreadSanitizer.
     */
    if (g_once_init_enter(&s_load_state)) {
        gsize state = GfxstreamBackendDoLoad();
        g_once_init_leave(&s_load_state, state);
    }
    if (s_error) {
        if (error_msg) {
            *error_msg = g_strdup(s_error);
        }
        if (out_errno) {
            *out_errno = s_load_errno;
        }
        return NULL;
    }
    if (out_errno) {
        *out_errno = 0;
    }
    return &s_fns;
}

const struct gfxstream_backend_fns* gfxstream_backend_fns(void) {
    return (gsize)g_atomic_pointer_get(&s_load_state) == GFXSTREAM_LOAD_STATE_LOADED ? &s_fns
                                                                                     : NULL;
}
