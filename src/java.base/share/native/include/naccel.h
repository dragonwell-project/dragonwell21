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

#ifndef _NACCEL_H_
#define _NACCEL_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Entry for a native function.
typedef struct {
  // The class name of the Java method to be replaced.
  const char *klass;
  // The method name of the Java method to be replaced.
  const char *method;
  // The method signature of the Java method to be replaced.
  const char *signature;
  // The name of the native function.
  const char *native_func_name;
  // The address of the native function.
  void *native_func;
} naccel_entry_t;

// Defines a native function entry.
#define NACCEL_ENTRY(k, m, s, n, f) \
  {                                 \
      k, m, s, n, (void *)f,        \
  }

// A unit of native acceleration.
typedef struct {
  // The number of entries in the unit.
  size_t num_entries;
  // The entries in the unit.
  const naccel_entry_t *entries;
} naccel_unit_t;

// The result of initializing native acceleration unit.
typedef enum {
  // Initialization was successful.
  NACCEL_INIT_OK,
  // Initialization failed.
  NACCEL_INIT_ERROR,
} naccel_init_result_t;

// Type of the initialization function.
typedef naccel_init_result_t (*naccel_initialize_t)(naccel_unit_t *unit);

// Type of the finalization function.
typedef void (*naccel_finalize_t)();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _NACCEL_H_
