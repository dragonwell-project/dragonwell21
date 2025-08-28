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

// We just use `JNIEXPORT` and `JNICALL` macros from the JNI header,
// to prevent the linker from hiding the functions in this library.
//
// Note that all functions in the library have nothing to do with JNI.
#include "jni_md.h"

JNIEXPORT aiext_result_t JNICALL aiext_init(const aiext_env_t* env) {
  // Read flag `NonProfiledCodeHeapSize`.
  char buf[4096];
  aiext_result_t result =
      env->get_jvm_flag("NonProfiledCodeHeapSize", buf, sizeof(buf));
  printf("Result %d, NonProfiledCodeHeapSize=%s\n", result, buf);
  if (result != AIEXT_OK) {
    return result;
  }

  // Shrink `NonProfiledCodeHeapSize`.
  long size = strtol(buf, NULL, 10);
  size -= 4096 * 20;  // Reduce 80KB.
  snprintf(buf, sizeof(buf), "%ld", size);
  result = env->set_jvm_flag("NonProfiledCodeHeapSize", buf);
  if (result != AIEXT_OK) {
    return result;
  }

  // Read again.
  result = env->get_jvm_flag("NonProfiledCodeHeapSize", buf, sizeof(buf));
  if (result != AIEXT_OK) {
    return result;
  }
  printf("Result %d, NonProfiledCodeHeapSize=%s", result, buf);

  // Check the new size.
  long new_size = strtol(buf, NULL, 10);
  return new_size == size ? AIEXT_OK : AIEXT_ERROR;
}

JNIEXPORT aiext_result_t JNICALL aiext_post_init(const aiext_env_t* env) {
  // Check CPU feature.
#if defined(__x86_64__)
  const char* feature = "avx2";
#elif defined(__aarch64__)
  const char* feature = "neon";
#endif
  int exist = env->support_cpu_feature(feature);
  printf("Support %s? %s\n", feature, exist ? "true" : "false");

  // Check invalid CPU feature.
  exist = env->support_cpu_feature("invalid feature");
  return exist ? AIEXT_ERROR : AIEXT_OK;
}
