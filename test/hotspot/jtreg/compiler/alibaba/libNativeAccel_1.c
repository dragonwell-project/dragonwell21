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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "aiext.h"
#include "naccel.h"

// We just use `JNIEXPORT` and `JNICALL` macros from the JNI header,
// to prevent the linker from hiding the functions in this library.
//
// Note that all functions in the library have nothing to do with JNI.
#include "jni_md.h"

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
static void hello_bytes(const int8_t *chars, int32_t len) {
  printf("Hello, I got %.*s (bytes)!\n", len, chars);
}

// For (Ljava/lang/Object;)V static method.
static void hello_object(const void *obj) {
  printf("Hello, I got %p (object)!\n", obj);
}

// For (S)V method (with a `this` pointer).
static void hello_short_method(const void *this, int16_t i) {
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
static void add_arrays(const void *this, int32_t *a, int32_t a_len, int32_t *b,
                       int32_t b_len) {
  for (int i = 0; i < a_len && i < b_len; i++) {
    a[i] += b[i];
  }
}

JNIEXPORT aiext_result_t JNICALL aiext_init(const AIEXT_ENV* env) {
  return AIEXT_OK;
}

JNIEXPORT aiext_result_t JNICALL aiext_post_init(const AIEXT_ENV *env) {
  /*
  static const naccel_entry_t entries[] = {
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "hello", "()V", "hello",
                   hello),
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "hello", "(I)V",
                   "hello_int", hello_int),
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "hello", "(J)V",
                   "hello_long", hello_long),
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "hello", "(F)V",
                   "hello_float", hello_float),
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "hello", "(D)V",
                   "hello_double", hello_double),
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "hello", "([B)V",
                   "hello_bytes", hello_bytes),
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "hello",
                   "(Ljava/lang/Object;)V", "hello_object", hello_object),
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "hello", "(S)V",
                   "hello_short_method", hello_short_method),
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "add", "(II)I",
                   "add_ints", add_ints),
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "add", "(DD)D",
                   "add_doubles", add_doubles),
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "add", "([I[I)V",
                   "add_arrays", add_arrays),
  };
  unit->num_entries = sizeof(entries) / sizeof(entries[0]);
  unit->entries = entries;
  */
  return AIEXT_OK;
}

JNIEXPORT aiext_result_t JNICALL aiext_finalize(const AIEXT_ENV* env) {
  printf("aiext_finalize\n");
  return AIEXT_OK;
}
