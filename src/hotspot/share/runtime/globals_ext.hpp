/*
 * Copyright (c) 1997, 2024, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2024, Alibaba Group Holding Limited. All Rights Reserved.
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
 *
 */

#ifndef SHARE_RUNTIME_GLOBALS_EXT_HPP
#define SHARE_RUNTIME_GLOBALS_EXT_HPP

#define DRAGONWELL_FLAGS(develop,                                              \
                         develop_pd,                                           \
                         product,                                              \
                         product_pd,                                           \
                         notproduct,                                           \
                         range,                                                \
                         constraint)                                           \
  product(bool, UseNewCode4, false, DIAGNOSTIC,                                \
          "Testing Only: Use the new version while testing")                   \
                                                                               \
  product(bool, ReplaceLLMemBarWithLoadAcquire, false,                         \
          "Replace LoadLoad membar with load-acquire")                         \
                                                                               \
  product(bool, TraceNonProfiledHotCodeHeapActivities, false, DIAGNOSTIC,      \
          "Trace activities of NonProfiledHotCodeHeap")                        \
                                                                               \
  product(uintx, NonProfiledHotCodeHeapSize, 0,                                \
          "Size of hot code heap with non-profiled methods (in bytes)")        \
          range(0, max_uintx)                                                  \
                                                                               \
  product(bool, AllocIVtableStubInNonProfiledHotCodeHeap, false,               \
          "Allocate itable/vtable in NonProfiledHotCodeHeap")                  \
                                                                               \
  AIEXT_ONLY(product(bool, UseAIExtension, false, EXPERIMENTAL,                \
                     "Enable Alibaba Dragonwell AI Extension"))                \
                                                                               \
  AIEXT_ONLY(product(ccstrlist, AIExtensionUnit, "", EXPERIMENTAL,             \
                     "Load external AI-Extension units"))                      \

#endif // SHARE_RUNTIME_GLOBALS_EXT_HPP

