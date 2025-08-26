/*
 * Copyright (c) 2025, Alibaba Group Holding Limited. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#ifndef _AIEXT_H_
#define _AIEXT_H_

#include <stddef.h>
#include "jni.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIEXT_VERSION_1 0xBABA0001
// The result of initializing native acceleration unit.
typedef enum {
  AIEXT_OK    = JNI_OK,
  AIEXT_ERROR = JNI_ERR,
} aiext_result_t;

typedef unsigned long aiext_handle_t;
#define INVALID_HANDLE (0xFFFFFFFFFFFFFFFF)

// API for AI Extension unit
struct AIEXT_ENV_ {
  // Return vm version string
  aiext_result_t (*get_jvm_version)(char* buf, int buf_size);

  // Return current aiext version
  jint (*get_aiext_version)();

  // Find JVM flag and value, value is stored in value_buf,
  aiext_result_t (*get_jvm_flag)(const char* flag_name, char* value_buf, int buf_size);

  // Set JVM flag with new value string
  aiext_result_t (*set_jvm_flag)(const char* flag_name, const char* value);

  // Check if cpu feature is supported
  jboolean (*support_cpu_feature)(const char* feature);

  // Register native accel function
  aiext_result_t (*register_native_accel_provider)(const char* klass, const char* method, const char* sig, const char* native_func_name, void* native_entry);

  // Get field offset, return -1 for failure
  jlong (*get_field_offset)(const char* klass, const char* method, const char* sig);

  // Get extension info (name, version, param_list)
  aiext_result_t (*get_extension_info)(const aiext_handle_t handle, char* name_buf, int name_buf_size, char* version_info, int version_buf_size, char* param_list_buf, int param_list_buf_size);

  // Extension specific handle, stored after aiext_init
  // aiext_handle_t handle;
};

typedef struct AIEXT_ENV_ AIEXT_ENV;

// API provided by AI Extension, they are invoked by jvm

// Initialize extension
typedef aiext_result_t (*aiext_init_t)(const AIEXT_ENV* env);

// Post init, invoked after jvm init
typedef aiext_result_t (*aiext_post_init_t)(const AIEXT_ENV* env);

// Destroy ai extension
typedef aiext_result_t (*aiext_finalize_t)(const AIEXT_ENV* env);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _AIEXT_H_
