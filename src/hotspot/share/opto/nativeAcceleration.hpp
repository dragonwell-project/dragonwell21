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

#include "aiext.h"
#include "oops/symbol.hpp"
#include "opto/callGenerator.hpp"
#include "utilities/macros.hpp"

#if !INCLUDE_AIEXT
#error "This file should not be included if `aiext` is not enabled"
#endif  // !INCLUDE_AIEXT

class NativeAccelTable;

// Entry for loaded AI-Extension units.
class NativeAccelUnit : public CHeapObj<mtCompiler> {
 private:
  friend class NativeAccelTable;

  // Strings parsed from argument option:
  // Feature name.
  const char* _feature;
  // Version string.
  const char* _version;
  // Optional parameter list.
  const char* _param_list;

  // Handle of the loaded AI-Extension unit library.
  void* _handle;

  // Handle for identifying AI-Extension units.
  aiext_handle_t _aiext_handle;

  // Comparator for the AI-Extension unit library entry.
  static int compare(NativeAccelUnit* const& u1, NativeAccelUnit* const& u2);

  // Parses the given argument string to construct a unit.
  // The returned unit should be properly deleted before the VM exits.
  static NativeAccelUnit* parse_from_arg(const char* arg);

  NativeAccelUnit(const char* feature, const char* version,
                  const char* param_list, aiext_handle_t aiext_handle)
      : _handle(nullptr), _aiext_handle(aiext_handle) {
    assert(feature != nullptr && version != nullptr, "sanity");
    _feature = os::strdup(feature);
    _version = os::strdup(version);
    if (param_list != nullptr) {
      _param_list = os::strdup(param_list);
    } else {
      _param_list = nullptr;
    }
  }

  // Loads the extension unit.
  bool load();

 public:
  ~NativeAccelUnit() {
    os::free((void*)_feature);
    os::free((void*)_version);
    if (_param_list != nullptr) {
      os::free((void*)_param_list);
    }
    if (_handle != nullptr) {
      os::dll_unload(_handle);
    }
  }

  // Returns the feature name.
  const char* feature() const { return _feature; }

  // Returns the version string.
  const char* version() const { return _version; }

  // Returns the parameter list (optional, can be `nullptr`).
  const char* param_list() const { return _param_list; }
};

// Entry for accelerated Java method calls.
class AccelCallEntry : public CHeapObj<mtCompiler> {
 private:
  friend class NativeAccelTable;

  Symbol* _klass;
  Symbol* _method;
  Symbol* _signature;
  const char* _native_func_name;
  void* _native_func;

  // Comparator for the acceleration table entry.
  static int compare(AccelCallEntry* const& e1, AccelCallEntry* const& e2);

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
 public:
  // Loads AI-Extension units from parsed unit list.
  // Returns `false` on error.
  static bool init();

  // Initializes AI-Extension after Java VM initialization.
  static bool post_init();

  // Deletes tables and frees all related resources.
  static void destroy();

  // Adds a new acceleration entry to table.
  static AccelCallEntry* add_entry(const char* klass, const char* method,
                                   const char* signature,
                                   const char* native_func_name,
                                   void* native_entry);

  // Finds the acceleration entry for a given method.
  static const AccelCallEntry* find(Symbol* klass, Symbol* method,
                                    Symbol* signature);

#ifdef ASSERT
  // Returns `true` if the given call is a accelerated native call.
  static bool is_accel_native_call(CallNode* call);
#endif  // ASSERT

  // Finds AI-Extension unit by handle.
  static const NativeAccelUnit* find_unit(aiext_handle_t handle);
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
