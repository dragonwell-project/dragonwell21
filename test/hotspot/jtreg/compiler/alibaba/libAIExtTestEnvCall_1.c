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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aiext.h"

// We just use `JNIEXPORT` and `JNICALL` macros from the JNI header,
// to prevent the linker from hiding the functions in this library.
//
// Note that all functions in the library have nothing to do with JNI.
#include "jni_md.h"

#if defined(__x86_64__)
#define GET_NPCHS get_jvm_flag_uintx
#define SET_NPCHS set_jvm_flag_uintx
#define NPCHS_TYPE uintptr_t
#define NPCHS_FMT PRIuPTR
#elif defined(__aarch64__)
#define GET_NPCHS get_jvm_flag_intx
#define SET_NPCHS set_jvm_flag_intx
#define NPCHS_TYPE intptr_t
#define NPCHS_FMT PRIdPTR
#endif

JNIEXPORT aiext_result_t JNICALL aiext_init(const aiext_env_t* env,
                                            aiext_handle_t handle) {
  // Check feature name, version and parameter list.
  char feature[32], version[32], param[32];
  aiext_result_t result =
      env->get_unit_info(handle, feature, sizeof(feature), version,
                         sizeof(version), param, sizeof(param));
  if (result != AIEXT_OK) {
    return result;
  }
  printf("aiext_init: feature=%s, version=%s, param=%s\n", feature, version,
         param);
  if (strcmp(feature, "libAIExtTestEnvCall") != 0 ||
      strcmp(version, "1") != 0 || param[0] != '\0') {
    return AIEXT_ERROR;
  }

  // Read flag `NonProfiledCodeHeapSize`.
  NPCHS_TYPE size;
  result = env->GET_NPCHS("NonProfiledCodeHeapSize", &size);
  printf("Result %d, NonProfiledCodeHeapSize=%" NPCHS_FMT "\n", result, size);
  if (result != AIEXT_OK) {
    return result;
  }

  // Shrink `NonProfiledCodeHeapSize`.
  size -= 4096 * 20;
  result = env->SET_NPCHS("NonProfiledCodeHeapSize", size);
  if (result != AIEXT_OK) {
    return result;
  }

  // Read again.
  NPCHS_TYPE new_size;
  result = env->GET_NPCHS("NonProfiledCodeHeapSize", &new_size);
  if (result != AIEXT_OK) {
    return result;
  }
  printf("Result %d, NonProfiledCodeHeapSize=%" NPCHS_FMT "\n", result,
         new_size);

  // Check the new size.
  return new_size == size ? AIEXT_OK : AIEXT_ERROR;
}
