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

#include "opto/nativeAcceleration.hpp"

#include <string.h>

#include "classfile/symbolTable.hpp"
#include "logging/log.hpp"
#include "memory/resourceArea.hpp"
#include "naccel.h"
#include "opto/callnode.hpp"
#include "opto/graphKit.hpp"
#include "opto/multnode.hpp"
#include "opto/type.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/os.hpp"
#include "utilities/debug.hpp"
#include "utilities/ostream.hpp"

GrowableArrayCHeap<AccelCallEntry*, mtCompiler>*
    NativeAccelTable::_accel_table = nullptr;

GrowableArrayCHeap<NativeAccelUnit, mtCompiler>*
    NativeAccelTable::_loaded_units = nullptr;

int NativeAccelUnit::compare(const NativeAccelUnit& e1,
                             const NativeAccelUnit& e2) {
  return strcmp(e1.path, e2.path);
}

int AccelCallEntry::compare(AccelCallEntry* const& e1,
                            AccelCallEntry* const& e2) {
  return e1->_klass < e2->_klass           ? -1
         : e1->_klass > e2->_klass         ? 1
         : e1->_method < e2->_method       ? -1
         : e1->_method > e2->_method       ? 1
         : e1->_signature < e2->_signature ? -1
         : e1->_signature > e2->_signature ? 1
                                           : 0;
}

bool NativeAccelTable::load_unit(const char* path) {
  // Check if we have already loaded the unit.
  assert(_loaded_units != nullptr, "must be initialized");
  bool found;
  int index =
      _loaded_units->find_sorted<NativeAccelUnit, NativeAccelUnit::compare>(
          NativeAccelUnit{path, nullptr}, found);
  if (found) {
    tty->print_cr("Error: Duplicate native acceleration unit `%s`", path);
    return false;
  }

  // Try to load the library.
  char ebuf[1024];
  void* handle = os::dll_load(path, ebuf, sizeof(ebuf));
  if (handle == nullptr) {
    tty->print_cr("Error: Could not load native acceleration unit `%s`", path);
    tty->print_cr("Error: %s", ebuf);
    return false;
  }

  // Get the entry point.
  naccel_initialize_t init =
      (naccel_initialize_t)os::dll_lookup(handle, "naccel_initialize");
  if (init == nullptr) {
    tty->print_cr(
        "Error: Could not find `naccel_initialize` "
        "in native acceleration unit `%s`",
        path);
    return false;
  }

  // Get native acceleration entries.
  naccel_unit_t unit;
  naccel_init_result_t result = init(&unit);
  if (result != NACCEL_INIT_OK || unit.entries == nullptr) {
    tty->print_cr("Error: Could not initialize native acceleration unit `%s`",
                  path);
    return false;
  }

  // Create a new unit entry.
  const char* copied_path = os::strdup(path, mtCompiler);
  _loaded_units->insert_before(index, NativeAccelUnit{copied_path, handle});

  // Create native acceleration entries.
  for (size_t i = 0; i < unit.num_entries; ++i) {
    // Check if the entry is valid.
    const naccel_entry_t* entry = &unit.entries[i];
    if (entry->klass == nullptr || entry->method == nullptr ||
        entry->signature == nullptr || entry->native_func_name == nullptr ||
        entry->native_func_name[0] == '\0' || entry->native_func == nullptr) {
      tty->print_cr("Error: Invalid entry %zu in native acceleration unit `%s`",
                    i, path);
      return false;
    }

    // Create symbols.
    Symbol* klass = SymbolTable::new_permanent_symbol(entry->klass);
    Symbol* method = SymbolTable::new_permanent_symbol(entry->method);
    Symbol* signature = SymbolTable::new_permanent_symbol(entry->signature);

    // Check if the entry presents.
    bool found;
    AccelCallEntry key(klass, method, signature);
    int index =
        _accel_table->find_sorted<AccelCallEntry*, AccelCallEntry::compare>(
            &key, found);
    if (found) {
      tty->print_cr(
          "Error: Duplicate native acceleration entry found for %s::%s%s",
          entry->klass, entry->method, entry->signature);
      return false;
    }

    // Create entry and add to table.
    _accel_table->insert_before(
        index, new AccelCallEntry(klass, method, signature,
                                  entry->native_func_name, entry->native_func));
  }
  return true;
}

bool NativeAccelTable::init() {
  // Create tables.
  assert(_accel_table == nullptr && _loaded_units == nullptr,
         "init should only be called once");
  _accel_table = new GrowableArrayCHeap<AccelCallEntry*, mtCompiler>();
  _loaded_units = new GrowableArrayCHeap<NativeAccelUnit, mtCompiler>();

  // Quit if native acceleration is not enabled.
  if (!UseNativeAcceleration) {
    return true;
  }

  // Load the builtin native acceleration unit.
  if (!load_unit("libnaccel.so")) {
    return false;
  }

  // Load other native acceleration units.
  char* paths = os::strdup(NativeAccelerationUnit);
  size_t paths_len = strlen(paths);
  char* path = paths;
  while (path < paths + paths_len) {
    // Find the next path.
    char* p = path;
    while (*p != '\n' && *p != '\0') {
      ++p;
    }
    *p = '\0';

    // Load the current unit.
    if (!load_unit(path)) {
      os::free(paths);
      return false;
    }

    path = p + 1;
  }
  os::free(paths);

  // Shrink tables.
  _accel_table->shrink_to_fit();
  _loaded_units->shrink_to_fit();

  // Check if there are any entries loaded.
  if (_accel_table->is_empty()) {
    warning(
        "No native acceleration entries were found in any of the units, "
        "native acceleration will have no effect");
  }
  return true;
}

void NativeAccelTable::destroy() {
  // Close all loaded libraries and free related resources.
  for (const auto& e : *_loaded_units) {
    // Call the finalize function if present.
    naccel_finalize_t finalize =
        (naccel_finalize_t)os::dll_lookup(e.handle, "naccel_finalize");
    if (finalize != nullptr) {
      finalize();
    }

    // Free and unload.
    os::free((void*)e.path);
    os::dll_unload(e.handle);
  }

  // Free entries.
  for (const auto& e : *_accel_table) {
    delete e;
  }

  // Delete tables.
  delete _accel_table;
  delete _loaded_units;
}

const AccelCallEntry* NativeAccelTable::find(Symbol* klass, Symbol* method,
                                             Symbol* signature) {
  assert(_accel_table != nullptr, "must be initialized");
  if (_accel_table->is_empty()) {
    return nullptr;
  }

  bool found;
  AccelCallEntry key(klass, method, signature);
  int index =
      _accel_table->find_sorted<AccelCallEntry*, AccelCallEntry::compare>(
          &key, found);

  if (found) {
    return _accel_table->at(index);
  }
  return nullptr;
}

#ifdef ASSERT
bool NativeAccelTable::is_accel_native_call(CallNode* call) {
  assert(_accel_table != nullptr, "must be initialized");
  if (_accel_table->is_empty()) {
    return false;
  }

  CallLeafNode* cl = call->as_CallLeaf();
  if (cl == nullptr) {
    return false;
  }

  for (const auto& e : *_accel_table) {
    if (strcmp(e->_native_func_name, cl->_name) == 0) {
      return true;
    }
  }
  return false;
}
#endif  // ASSERT

// Fills the given type field(s) by the given CI type.
static void fill_type_field(const Type**& field, ciType* type, bool is_arg,
                            bool& has_fp_type) {
  switch (type->basic_type()) {
    case T_BOOLEAN:
      *field++ = TypeInt::BOOL;
      break;
    case T_CHAR:
      *field++ = TypeInt::CHAR;
      break;
    case T_FLOAT:
      *field++ = Type::FLOAT;
      has_fp_type = true;
      break;
    case T_DOUBLE:
      *field++ = Type::DOUBLE;
      *field++ = Type::HALF;
      has_fp_type = true;
      break;
    case T_BYTE:
      *field++ = TypeInt::BYTE;
      break;
    case T_SHORT:
      *field++ = TypeInt::SHORT;
      break;
    case T_INT:
      *field++ = TypeInt::INT;
      break;
    case T_LONG:
      *field++ = TypeLong::LONG;
      *field++ = Type::HALF;
      break;
    case T_OBJECT:
      *field++ = TypeInstPtr::BOTTOM;
      break;
    case T_ARRAY:
      if (is_arg) {
        // Base pointer does not point to a Java object,
        // so we use raw pointer here.
        *field++ = TypeRawPtr::BOTTOM;
        // Append an integer for array only when it's an argument.
        *field++ = TypeInt::INT;
      } else {
        // We expect the function returns a Java array.
        *field++ = TypeOopPtr::BOTTOM;
      }
      break;
    case T_VOID:
      assert(!is_arg, "void argument?");
      break;
    default:
      // Other basic types can't be represented by method signatures.
      ShouldNotReachHere();
  }
}

JVMState* AccelCallGenerator::generate(JVMState* jvms) {
  GraphKit kit(jvms);
  ciMethod* callee = method();
  ciSignature* signature = callee->signature();

  // Get number of stack slots required for arguments.
  // Array arguments should be passed to native functions as tuples of base
  // pointer and length (int), so they require an additional slot.
  int arg_size = callee->arg_size();
  for (int i = 0; i < signature->count(); ++i) {
    if (signature->type_at(i)->basic_type() == T_ARRAY) {
      ++arg_size;
    }
  }

  // Create argument types.
  bool has_fp_type = false;
  const Type** fields = TypeTuple::fields(arg_size);
  const Type** field = fields + TypeFunc::Parms;
  if (!callee->is_static()) {
    // `this` pointer.
    *field++ = TypeInstPtr::NOTNULL;
  }
  int arg_index = 0;
  while (field < fields + TypeFunc::Parms + arg_size) {
    fill_type_field(field, signature->type_at(arg_index++), true, has_fp_type);
  }
  const TypeTuple* args_tuple =
      TypeTuple::make(TypeFunc::Parms + arg_size, fields);

  // Create return type.
  ciType* ret_type = signature->return_type();
  fields = TypeTuple::fields(ret_type->size());
  field = fields + TypeFunc::Parms;
  fill_type_field(field, ret_type, false, has_fp_type);
  const TypeTuple* ret_tuple =
      TypeTuple::make(TypeFunc::Parms + ret_type->size(), fields);

  // Create function type.
  const TypeFunc* func_type = TypeFunc::make(args_tuple, ret_tuple);

  // Create call node.
  void* native_func = callee->accel_call_entry()->native_func();
  const char* name = callee->accel_call_entry()->native_func_name();
  CallNode* call;
  if (has_fp_type) {
    call = new CallLeafNode(func_type, (address)native_func, name,
                            TypePtr::BOTTOM);
  } else {
    call = new CallLeafNoFPNode(func_type, (address)native_func, name,
                                TypePtr::BOTTOM);
  }

  // Setup inputs and arguments.
  kit.set_predefined_input_for_runtime_call(call);
  arg_index = 0;
  int req_index = TypeFunc::Parms;
  if (!callee->is_static()) {
    // `this` pointer.
    call->init_req(req_index++, kit.argument(arg_index++));
  }
  for (int i = 0; i < signature->count(); ++i) {
    ciType* arg_type = signature->type_at(i);
    Node* arg = kit.argument(arg_index++);
    switch (arg_type->basic_type()) {
      case T_ARRAY: {
        // Pass array's base address and length to the native function.
        ciType* elem_type = arg_type->as_array_klass()->element_type();
        BasicType elem_bt = elem_type->basic_type();
        Node* addr = kit.array_element_address(arg, kit.intcon(0), elem_bt);
        Node* len = kit.load_array_length(arg);
        call->init_req(req_index++, addr);
        call->init_req(req_index++, len);
        break;
      }
      case T_DOUBLE:
      case T_LONG: {
        call->init_req(req_index++, arg);
        Node* top = kit.argument(arg_index++);
        assert(top == kit.top(), "must be top");
        call->init_req(req_index++, top);
        break;
      }
      default: {
        call->init_req(req_index++, arg);
        break;
      }
    }
  }

  // Try to optimize.
  Node* c = kit.gvn().transform(call);
  assert(c == call, "cannot disappear");

  // Setup outputs.
  kit.set_predefined_output_for_runtime_call(call);

  // Setup return value (if presents).
  if (!ret_type->is_void()) {
    Node* result = kit.gvn().transform(new ProjNode(call, TypeFunc::Parms));
    kit.push_node(ret_type->basic_type(), result);
  }

  // Done.
  return kit.transfer_exceptions_into_jvms();
}
