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
#include "runtime/arguments.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/os.hpp"
#include "utilities/debug.hpp"
#include "utilities/ostream.hpp"

#if INCLUDE_AIEXT
GrowableArrayCHeap<AccelCallEntry*, mtCompiler>*
    NativeAccelTable::_accel_table = nullptr;

GrowableArrayCHeap<NativeAccelUnit*, mtCompiler>*
    NativeAccelTable::_loaded_units = new GrowableArrayCHeap<NativeAccelUnit*, mtCompiler>();

int NativeAccelUnit::compare(NativeAccelUnit* const &e1,
                             NativeAccelUnit* const &e2) {
  // param list is skipped in compare
  return strcmp(e1->feature, e2->feature) == 0
      && strcmp(e1->version, e2->version) == 0;
}

// max length of feature name and version string
#define MAX_UNIT_COMPONENT_LEN 50
// max length of parameter list
#define MAX_UNIT_PARAM_LIST_LEN 200

bool parse_feature_and_version(const char* str, size_t maxlen, char* feature, char* version) {
  const char* pos = strchr(str, '_');
  if (pos == nullptr) return false;
  size_t len = pos - str;
  if (len > MAX_UNIT_COMPONENT_LEN || len >= maxlen) {
    return false;
  } else {
    strncpy(feature, str, len);
    feature[len] = 0;
  }

  const size_t ver_len = maxlen - len - 1;
  const char*  ver_str = str + len + 1;
  if (ver_len > MAX_UNIT_COMPONENT_LEN) {
    return false;
  }

  size_t i=0;
  bool has_dot = false;
  for (; i<ver_len; i++) {
    char c = ver_str[i];
    if (c == '.') {
      if (!has_dot) {
        has_dot = true;
      } else {
        // find multiple dot
        return false;
      }
    } else if ( c < '0' || c > '9') {
      return false;
    }
    version[i] = c;
  }
  version[i]=0;
  assert(strlen(feature) <= MAX_UNIT_COMPONENT_LEN && strlen(version) <= MAX_UNIT_COMPONENT_LEN, "sanity");

  return true;
}

bool parse_param_list(const char* str, char* param_list) {
  if (strlen(str) > MAX_UNIT_PARAM_LIST_LEN) return false;
  strcpy(param_list, str);
  const char* colon_pos;
  const char* param_group = str;
  while ((colon_pos = strchr(param_group, ':')) != nullptr) {
    const char* delim = strchr(param_group, '=');
    if (delim == nullptr || delim >= colon_pos) return false;  // TODO: check duplicate '=' in one group
    param_group = colon_pos+1;
  }
  // last group
  const char* delim = strchr(param_group, '=');
  if (delim == nullptr) return false;  // TODO: check duplicate '=' in one group
  return true;
}

NativeAccelUnit* NativeAccelUnit::parse_from_option(const char *arg_option) {
  // The argument option has pattern feature_version?param1=val1:param2=val2
  char feature[MAX_UNIT_COMPONENT_LEN+1];
  char version[MAX_UNIT_COMPONENT_LEN+1];
  char param_list[MAX_UNIT_PARAM_LIST_LEN+1];
  bool parse_result = false;
  const char* option = arg_option+1;   // ignore the heading '=' chararcter;
  const char* pos;
  bool has_param_list = false;
  if ((pos = strchr(option, '?')) != nullptr) {
    has_param_list = true;
    parse_result = parse_feature_and_version(option, pos - option, feature, version);
  } else {
    parse_result = parse_feature_and_version(option, strlen(option), feature, version);
  }
  if (!parse_result) return nullptr;

  if (has_param_list) {
    parse_result = parse_param_list(pos+1, param_list);
  }
  if (!parse_result) return nullptr;

  return new NativeAccelUnit(feature, version, param_list);
}
#undef MAX_UNIT_COMPONENT_LEN
#undef MAX_UNIT_PARAM_LIST_LEN

bool NativeAccelUnit::load_and_verify() {
  const char * java_home_path = Arguments::get_java_home();
  const char * cpu_arch = AMD64_ONLY("x86-64") AARCH64_ONLY("aarch64") "";
  NOT_AMD64(NOT_AARCH64(ShouldNotReachHere();))  // Only x86_64 and aarch64 are supported
  // $JAVA_HOME/lib/ai-ext/feature_ver_arch.so
  int lib_path_len = strlen(java_home_path) + 17 + strlen(feature) + strlen(version) + strlen(cpu_arch);
  // Used by testing
  const char * alt_ext_path = ::getenv("ALT_AI_EXT_PATH");
  if (alt_ext_path != nullptr) {
    // $ALT_AI_EXT_PATH/feature_ver_arch.so
    lib_path_len = strlen(alt_ext_path) + 6 + strlen(feature) + strlen(version) + strlen(cpu_arch);
  }
  char * buf = (char*)os::malloc(lib_path_len + 1, mtCompiler);
  assert(buf != nullptr, "OOM on native malloc");
  if (alt_ext_path != nullptr) {
    snprintf(buf, lib_path_len+1, "%s/%s_%s_%s.so", alt_ext_path, feature, version, cpu_arch);
  } else {
    snprintf(buf, lib_path_len+1, "%s/lib/ai-ext/%s_%s_%s.so", java_home_path, feature, version, cpu_arch);
  }
  int prefix_len = strlen(buf) - strlen(cpu_arch) - 4;   // reduce size of "_cpuarch.so"
  // First try with cpuarch in path
  void * lib_handle = NativeAccelTable::load_unit(buf);
  if (lib_handle == nullptr) {
    // Second try without cpu arch
    buf[prefix_len] = 0;
    strcat(buf, ".so");
    lib_handle = NativeAccelTable::load_unit(buf);
  }
  handle = lib_handle;

  // extension verification is done in aiext_initialize()

  os::free(buf);
  return lib_handle != nullptr;
}

void NativeAccelTable::add_unit(NativeAccelUnit *unit) {
  assert(_loaded_units != nullptr, "must be initialized");
  bool found;
  int index = _loaded_units->find_sorted<NativeAccelUnit*, NativeAccelUnit::compare>(
          unit, found);
  if (found) {
    tty->print_cr("Error: Duplicate native acceleration unit `%s_%s`", unit->feature, unit->version);
    return;
  }
  _loaded_units->insert_before(index, unit);
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

void* NativeAccelTable::load_unit(const char* path) {
  // Try to load the library.
  char ebuf[1024];
  void* handle = os::dll_load(path, ebuf, sizeof(ebuf));
  if (handle == nullptr) {
    tty->print_cr("Error: Could not load ai extension unit `%s`", path);
    tty->print_cr("Error: %s", ebuf);
    return nullptr;
  }

  // Get the entry point.
  naccel_initialize_t init =
      (naccel_initialize_t)os::dll_lookup(handle, "aiext_initialize");
  if (init == nullptr) {
    tty->print_cr(
        "Error: Could not find `aiext_initialize` "
        "in native acceleration unit `%s`",
        path);
    return nullptr;
  }

  // Get native acceleration entries.
  naccel_unit_t unit;
  naccel_init_result_t result = init(&unit);
  if (result != NACCEL_INIT_OK) {
    tty->print_cr("Error: Could not initialize ai extension unit `%s`",
                  path);
    return nullptr;
  }
  return handle;
}

bool NativeAccelTable::init() {
  // Quit if native acceleration is not enabled.
  if (!UseAIExtension) {
    return true;
  }

  // Create tables.
  assert(_accel_table == nullptr, "init should only be called once");
  _accel_table = new GrowableArrayCHeap<AccelCallEntry*, mtCompiler>();

  if (_loaded_units->is_empty()) {
    ttyLocker ttyl;
    tty->print_cr("Warning: AI-Extension unit is not provided in jvm arguments.");
    return true;
  }

  for (const auto& e : *_loaded_units) {
    if (!e->load_and_verify()) {
      ttyLocker ttyl;
      char buf[200];
      e->name(buf, sizeof(buf));
      tty->print_cr("Error: failed to load AI-Extension unit %s", buf);
      return false;
    };
  }
  return true;
}

bool NativeAccelTable::post_init() {
  if (!UseAIExtension) {
    return true;
  }

  for (const auto& e : *_loaded_units) {
    if (e->handle != nullptr) {
      // invoke aiext_post_init
      naccel_initialize_t post_init =
          (naccel_initialize_t)os::dll_lookup(e->handle, "aiext_post_init");
      if (post_init != nullptr) {
        naccel_unit_t unit;
        naccel_init_result_t result = post_init(&unit);
        char ext_name[200];
        e->name(ext_name, sizeof(ext_name));
        if (result != NACCEL_INIT_OK || unit.entries == nullptr) {
          tty->print_cr("Error: Could not post vm initialize ai extension unit `%s`", ext_name);
          return false;
        }
        // Create native acceleration entries.
        for (size_t i = 0; i < unit.num_entries; ++i) {
          // Check if the entry is valid.
          const naccel_entry_t* entry = &unit.entries[i];
          if (entry->klass == nullptr || entry->method == nullptr ||
              entry->signature == nullptr || entry->native_func_name == nullptr ||
              entry->native_func_name[0] == '\0' || entry->native_func == nullptr) {
            tty->print_cr("Error: Invalid entry %zu in ai extension unit `%s`",
                          i, ext_name);
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
      }
    };
  }
  return true;
}

/*
bool NativeAccelTable::init() {
  // Quit if native acceleration is not enabled.
  if (!UseNativeAcceleration) {
    return true;
  }

  // Create tables.
  assert(_accel_table == nullptr, "init should only be called once");
  _accel_table = new GrowableArrayCHeap<AccelCallEntry*, mtCompiler>();

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
*/

void NativeAccelTable::destroy() {
  // Close all loaded libraries and free related resources.
  for (const auto& e : *_loaded_units) {
    if (e->handle != nullptr) {
      // Call the finalize function if present.
      naccel_finalize_t finalize =
          (naccel_finalize_t)os::dll_lookup(e->handle, "aiext_finalize");
      if (finalize != nullptr) {
        finalize();
      }
    }

    // Free and unload.
    delete e;
  }

  // Delete tables.
  delete _accel_table;
  delete _loaded_units;
}

const AccelCallEntry* NativeAccelTable::find(Symbol* klass, Symbol* method,
                                             Symbol* signature) {
  if (!UseAIExtension) return nullptr;

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
  if (!UseAIExtension) return false;
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
#endif
