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

class NativeAccelTable;

// Entry for loaded native acceleration units.
class NativeAccelUnit : public CHeapObj<mtCompiler> {
friend NativeAccelTable;
private:
  // String parsed from argument option
  // Feature name
  const char* feature;
  // Version string
  const char* version;
  // Optional parameter list
  const char* param_list;
  // Handle of the loaded native acceleration unit library.
  void* handle;

public:
  // Comparator for the native acceleration unit library entry.
  static int compare(NativeAccelUnit* const& e1, NativeAccelUnit* const& e2);

  // Parse argument option string to construct a unit
  static NativeAccelUnit* parse_from_option(const char* arg_option);

  NativeAccelUnit(const char* f, const char* v, const char* p) : handle(nullptr) {
    assert(f != nullptr && v != nullptr, "sanity");
    feature = os::strdup(f);
    version = os::strdup(v);
    if (p != nullptr) {
      param_list = os::strdup(p);
    }
  }

  ~NativeAccelUnit() {
    os::free((void*)feature);
    os::free((void*)version);
    if (param_list != nullptr) {
      os::free((void *) param_list);
    }
    if (handle != nullptr) {
      os::dll_unload(handle);
    }
  }

  // Load the extension unit and verify before run
  bool load_and_verify();

  // Print name of extension
  void name(char* buf, size_t len) { snprintf(buf, len, "%s_%s", feature, version); }
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
  static GrowableArrayCHeap<NativeAccelUnit*, mtCompiler>* _loaded_units;

 public:
  // Loads ai extension units from parsed unit list, creates the acceleration table.
  // Returns `false` on error.
  static bool init();

  // AI Extension initialize work after Java VM init
  static bool post_init();

  // Add Native acceleration unit to list, used when match arguments
  static void add_unit(NativeAccelUnit* unit);

  // Deletes the acceleration table and frees all related resources.
  static void destroy();

  // Utility helper to loads an AI-Ext unit library from the given path.
  // Returns handle of loaded library, nullptr for failure
  static void* load_unit(const char* path);

  // Add a new acceleration entry into table
  static AccelCallEntry* add_entry(const char* klass, const char* method,
                                   const char* signature, const char* native_func_name,
                                   void* native_entry);

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
