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

#include "naccel.h"

// We just use `JNIEXPORT` and `JNICALL` macros from the JNI header,
// to prevent the linker from hiding the functions in this library.
//
// Note that all functions in the library have nothing to do with JNI.
#include "jni_md.h"

// For ()V static method.
static void hello() { printf("Hello again from native library!\n"); }

JNIEXPORT naccel_init_result_t JNICALL aiext_initialize(naccel_unit_t *unit) {
  return NACCEL_INIT_OK;
}

JNIEXPORT naccel_init_result_t JNICALL aiext_post_init(naccel_unit_t *unit) {
  static const naccel_entry_t entries[] = {
      NACCEL_ENTRY("TestNativeAcceleration$Launcher", "hello", "()V", "hello",
                   hello),
  };
  unit->num_entries = sizeof(entries) / sizeof(entries[0]);
  unit->entries = entries;
  return NACCEL_INIT_OK;
}
