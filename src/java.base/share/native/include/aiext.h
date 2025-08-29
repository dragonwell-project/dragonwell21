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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Versions of AI-Extension.
#define AIEXT_VERSION_1 0xBABA0001

// The result of initializing AI-Extension unit.
typedef enum {
  AIEXT_OK,
  AIEXT_ERROR,
} aiext_result_t;

// AI-Extension unit handle, for identification of a unit.
typedef unsigned long aiext_handle_t;

// API for AI Extension units.
typedef struct aiext_env {
  // Returns JVM version string.
  aiext_result_t (*get_jvm_version)(char* buf, size_t buf_size);

  // Returns current AI-Extension version.
  int (*get_aiext_version)();

  // Gets JVM flag by name.
#define DECL_GET_JVM_FLAG(n, t) \
  aiext_result_t (*get_jvm_flag_##n)(const char* name, t* value)
  DECL_GET_JVM_FLAG(bool, int);
  DECL_GET_JVM_FLAG(int, int);
  DECL_GET_JVM_FLAG(uint, unsigned int);
  DECL_GET_JVM_FLAG(intx, intptr_t);
  DECL_GET_JVM_FLAG(uintx, uintptr_t);
  DECL_GET_JVM_FLAG(uint64_t, uint64_t);
  DECL_GET_JVM_FLAG(size_t, size_t);
  DECL_GET_JVM_FLAG(double, double);
  aiext_result_t (*get_jvm_flag_ccstr)(const char* name, char* buf,
                                       size_t buf_size);
#undef DECL_GET_JVM_FLAG

  // Sets JVM flag with new value.
#define DECL_SET_JVM_FLAG(n, t) \
  aiext_result_t (*set_jvm_flag_##n)(const char* name, t value)
  DECL_SET_JVM_FLAG(bool, int);
  DECL_SET_JVM_FLAG(int, int);
  DECL_SET_JVM_FLAG(uint, unsigned int);
  DECL_SET_JVM_FLAG(intx, intptr_t);
  DECL_SET_JVM_FLAG(uintx, uintptr_t);
  DECL_SET_JVM_FLAG(uint64_t, uint64_t);
  DECL_SET_JVM_FLAG(size_t, size_t);
  DECL_SET_JVM_FLAG(double, double);
  DECL_SET_JVM_FLAG(ccstr, const char*);
#undef DECL_SET_JVM_FLAG

  // Returns non-zero if the given CPU feature is supported.
  int (*support_cpu_feature)(const char* feature);

  // Registers native acceleration provider for specific Java method.
  aiext_result_t (*register_naccel_provider)(const char* klass,
                                             const char* method,
                                             const char* sig,
                                             const char* native_func_name,
                                             void* native_entry);

  // Gets field offset in a Java class, returns `-1` on failure.
  int64_t (*get_field_offset)(const char* klass, const char* method,
                              const char* sig);

  // Gets unit info, including name, version and parameter list.
  aiext_result_t (*get_unit_info)(const aiext_handle_t handle, char* name_buf,
                                  size_t name_buf_size, char* version_info,
                                  size_t version_buf_size, char* param_list_buf,
                                  size_t param_list_buf_size);
} aiext_env_t;

// API provided by AI Extension, these will be invoked by JVM:

// Initializes AI-Extension unit.
typedef aiext_result_t (*aiext_init_t)(const aiext_env_t* env);

// Initializes AI-Extension unit after JVM's initialization.
typedef aiext_result_t (*aiext_post_init_t)(const aiext_env_t* env);

// Finalizes AI-Extension unit.
typedef aiext_result_t (*aiext_finalize_t)(const aiext_env_t* env);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _AIEXT_H_
