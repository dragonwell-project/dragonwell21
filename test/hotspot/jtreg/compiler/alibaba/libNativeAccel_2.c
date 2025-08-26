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

#include <stdio.h>
#include <stdlib.h>

#include "aiext.h"
#include "naccel.h"

// We just use `JNIEXPORT` and `JNICALL` macros from the JNI header,
// to prevent the linker from hiding the functions in this library.
//
// Note that all functions in the library have nothing to do with JNI.
#include "jni_md.h"

// For ()V static method.
static void hello() { printf("Hello again from native library!\n"); }

JNIEXPORT aiext_result_t JNICALL aiext_init(const AIEXT_ENV* env) {
  return AIEXT_OK;
}

JNIEXPORT aiext_result_t JNICALL aiext_post_init(const AIEXT_ENV* env) {
  /*
  static const naccel_entry_t entries[] = {
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "hello", "()V", "hello",
                   hello),
  };
  unit->num_entries = sizeof(entries) / sizeof(entries[0]);
  unit->entries = entries;
  */
  char buf[4096];
  aiext_result_t result = env->get_jvm_flag("NonProfiledCodeHeapSize", buf, sizeof(buf));
  printf("result %d, NonProfiledCodeHeapSize=%s\n", result, buf);
  if (result != AIEXT_OK) {
    return result;
  }
  // shrink nonprofiledcodeheapsize
  char* end_pos;
  jlong old_size = strtol(buf, &end_pos, 10);
  snprintf(buf, sizeof(buf), "%ld", old_size-4096*20);  // reduce size 80k
  result = env->set_jvm_flag("NonProfiledCodeHeapSize", buf);
  if (result != AIEXT_OK) {
    return result;
  }
  // get size again
  result = env->get_jvm_flag("NonProfiledCodeHeapSize", buf, sizeof(buf));
  if (result != AIEXT_OK) {
    return result;
  }
  printf("result %d, NonProfiledCodeHeapSize=%s", result, buf);
#if defined(__x86_64__)
  const char* feature = "avx2";
#elif defined(__aarch64__)
  const char* feature = "neon";
#endif
  jboolean exist = env->support_cpu_feature(feature);
  result = exist ? AIEXT_OK : AIEXT_ERROR;
  if (result != AIEXT_OK) {
    return result;
  }
  exist = env->support_cpu_feature("fake_feature");
  result = exist ? AIEXT_ERROR : AIEXT_OK;

  return result;
}
