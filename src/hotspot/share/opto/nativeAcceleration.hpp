/*
 * Copyright (c) 2025 Alibaba Group Holding Limited. All Rights Reserved.
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

#ifndef SHARE_OPTO_NATIVEACCELERATION_HPP
#define SHARE_OPTO_NATIVEACCELERATION_HPP

#include "oops/symbol.hpp"
#include "opto/callGenerator.hpp"
#include "utilities/growableArray.hpp"

// Entry for loaded native acceleration units.
struct NativeAccelUnit {
  // String to native acceleration unit library path.
  // Allocated on the C heap, and should be freed before VM exit.
  const char* path;
  // Handle of the loaded native acceleration unit library.
  void* handle;

  // Comparator for the native acceleration unit library entry.
  static int compare(const NativeAccelUnit& e1, const NativeAccelUnit& e2);
};

// Entry for accelerated Java method calls.
class AccelCallEntry : public CHeapObj<mtCompiler> {
 private:
  friend class NativeAccelTable;

  // Comparator for the acceleration table entry.
  static int compare(AccelCallEntry* const& e1, AccelCallEntry* const& e2);

  Symbol* _klass;
  Symbol* _method;
  Symbol* _signature;
  const char* _native_func_name;
  void* _native_func;

  // For finding entries.
  AccelCallEntry(Symbol* klass, Symbol* method, Symbol* signature)
      : _klass(klass),
        _method(method),
        _signature(signature),
        _native_func_name(nullptr),
        _native_func(nullptr) {}

  AccelCallEntry(Symbol* klass, Symbol* method, Symbol* signature,
                 const char* native_func_name, void* native_func)
      : _klass(klass),
        _method(method),
        _signature(signature),
        _native_func_name(native_func_name),
        _native_func(native_func) {}

 public:
  // Returns the native function name.
  const char* native_func_name() const { return _native_func_name; }

  // Returns the native function pointer.
  void* native_func() const { return _native_func; }
};

// The native acceleration table.
class NativeAccelTable : public AllStatic {
 private:
  // The acceleration table, which is sorted by the class name, method name and
  // signature.
  //
  // The table is initialized during startup, and will never be modified, so it
  // does not need to be protected by locks.
  static GrowableArrayCHeap<AccelCallEntry*, mtCompiler>* _accel_table;

  // Map for storing path of loaded native acceleration unit libraries and their
  // handles.
  //
  // The Map is initialized during startup, and will never be modified, so it
  // does not need to be protected by locks.
  static GrowableArrayCHeap<NativeAccelUnit, mtCompiler>* _loaded_units;

  // Loads a native acceleration unit library from the given path.
  // Returns `false` on error.
  static bool load_unit(const char* path);

 public:
  // Loads native acceleration libraries, creates the acceleration table.
  // Returns `false` on error.
  static bool init();

  // Deletes the acceleration table and frees all related resources.
  static void destroy();

  // Finds the acceleration entry for a given method.
  static const AccelCallEntry* find(Symbol* klass, Symbol* method,
                                    Symbol* signature);

#ifdef ASSERT
  // Returns `true` if the given call is a accelerated native call.
  static bool is_accel_native_call(CallNode* call);
#endif  // ASSERT
};

// Call generator for accelerated Java method calls.
class AccelCallGenerator : public InlineCallGenerator {
 private:
  bool _is_virtual;

 public:
  AccelCallGenerator(ciMethod* m, bool is_virtual)
      : InlineCallGenerator(m), _is_virtual(is_virtual) {}

  bool is_virtual() const override { return _is_virtual; }

  JVMState* generate(JVMState* jvms) override;
};

#endif  // SHARE_OPTO_NATIVEACCELERATION_HPP
