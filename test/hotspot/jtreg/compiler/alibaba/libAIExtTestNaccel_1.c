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

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "aiext.h"

static int offset_x_int, offset_x_double;

static void* native_provider(const aiext_env_t* env,
                             const char* native_func_name, void* data) {
  if (offset_x_int == 0) {
    offset_x_int =
        env->get_field_offset("TestAIExtension$Launcher", "x_int", "I");
  }
  if (offset_x_double == 0) {
    offset_x_double =
        env->get_field_offset("TestAIExtension$Launcher", "x_double", "D");
  }
  printf("Compiling `%s`, offset_x_int=%d, offset_x_double=%d\n",
         native_func_name, offset_x_int, offset_x_double);
  if (offset_x_int < 0 || offset_x_double < 0) {
    return NULL;
  }
  return data;
}

// For ()V static method.
static void hello() { printf("Hello from native library!\n"); }

// For (I)V static method.
static void hello_int(int32_t i) {
  printf("Hello, I got %" PRId32 " (int)!\n", i);
}

// For (J)V static method.
static void hello_long(int64_t l) {
  printf("Hello, I got %" PRId64 " (long)!\n", l);
}

// For (F)V static method.
static void hello_float(float f) { printf("Hello, I got %.2f (float)!\n", f); }

// For (D)V static method.
static void hello_double(double d) {
  printf("Hello, I got %.2f (double)!\n", d);
}

// For ([B)V static method.
static void hello_bytes(const int8_t* chars, int32_t len) {
  printf("Hello, I got %.*s (bytes)!\n", len, chars);
}

// For (Ljava/lang/Object;)V static method.
static void hello_object(const void* obj) {
  printf("Hello, I got %p (object)!\n", obj);
}

// For (S)V method (with a `this` pointer).
static void hello_short_method(const void* this, int16_t i) {
  printf("Hello, I got %p (this) and %" PRId16 " (short)!\n", this, i);
}

// Adds two integers.
// For (II)I static method.
static int32_t add_ints(int32_t a, int32_t b) { return a + b; }

// Adds two doubles.
// For (DD)D static method.
static double add_doubles(double a, double b) { return a + b; }

// Adds two integer arrays, updates the first array in-place.
// For ([I[I)V method.
static void add_arrays(const void* this, int32_t* a, int32_t a_len, int32_t* b,
                       int32_t b_len) {
  for (int i = 0; i < a_len && i < b_len; i++) {
    a[i] += b[i];
  }
}

// Adds the given integer to object's field.
// For (I)V method.
static void add_to_int(void* this, int32_t i) {
  assert(offset_x_int > 0 && "Invalid field offset");
  int32_t* x_int = (int32_t*)((char*)this + offset_x_int);
  *x_int += i;
}

// Adds the given double to object's field.
// For (D)V method.
static void add_to_double(void* this, double d) {
  assert(offset_x_double > 0 && "Invalid field offset");
  double* x_double = (double*)((char*)this + offset_x_double);
  *x_double += d;
}

JNIEXPORT aiext_result_t JNICALL aiext_init(const aiext_env_t* env,
                                            aiext_handle_t handle) {
  return AIEXT_OK;
}

JNIEXPORT aiext_result_t JNICALL aiext_post_init(const aiext_env_t* env,
                                                 aiext_handle_t handle) {
#define REPLACE_WITH_NATIVE(m, s, f)                                          \
  do {                                                                        \
    aiext_result_t res;                                                       \
    res = env->register_naccel_provider("TestAIExtension$Launcher", m, s, #f, \
                                        f, NULL);                             \
    if (res != AIEXT_OK) {                                                    \
      return res;                                                             \
    }                                                                         \
  } while (0)
#define REPLACE_WITH_PROVIDER(m, s, f)                                        \
  do {                                                                        \
    aiext_result_t res;                                                       \
    res = env->register_naccel_provider("TestAIExtension$Launcher", m, s, #f, \
                                        f, native_provider);                  \
    if (res != AIEXT_OK) {                                                    \
      return res;                                                             \
    }                                                                         \
  } while (0)

  REPLACE_WITH_NATIVE("hello", "()V", hello);
  REPLACE_WITH_NATIVE("hello", "(I)V", hello_int);
  REPLACE_WITH_NATIVE("hello", "(J)V", hello_long);
  REPLACE_WITH_NATIVE("hello", "(F)V", hello_float);
  REPLACE_WITH_NATIVE("hello", "(D)V", hello_double);
  REPLACE_WITH_NATIVE("hello", "([B)V", hello_bytes);
  REPLACE_WITH_NATIVE("hello", "(Ljava/lang/Object;)V", hello_object);
  REPLACE_WITH_NATIVE("hello", "(S)V", hello_short_method);

  REPLACE_WITH_NATIVE("add", "(II)I", add_ints);
  REPLACE_WITH_NATIVE("add", "(DD)D", add_doubles);
  REPLACE_WITH_NATIVE("add", "([I[I)V", add_arrays);

  REPLACE_WITH_PROVIDER("add_to_int", "(I)V", add_to_int);
  REPLACE_WITH_PROVIDER("add_to_double", "(D)V", add_to_double);

  REPLACE_WITH_PROVIDER("should_skip", "()V", NULL);

#undef REPLACE_WITH_NATIVE
#undef REPLACE_WITH_PROVIDER
  return AIEXT_OK;
}

JNIEXPORT void JNICALL aiext_finalize(const aiext_env_t* env,
                                      aiext_handle_t handle) {
  printf("aiext_finalize\n");
}
