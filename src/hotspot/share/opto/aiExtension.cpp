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

#if defined(INCLUDE_AIEXT) && INCLUDE_AIEXT

#include "opto/aiExtension.hpp"

#include <string.h>

#include "classfile/symbolTable.hpp"
#include "logging/log.hpp"
#include "memory/resourceArea.hpp"
#include "opto/callnode.hpp"
#include "opto/graphKit.hpp"
#include "opto/multnode.hpp"
#include "opto/type.hpp"
#include "runtime/arguments.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "utilities/debug.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/ostream.hpp"

// Declared in `aiext.cpp`.
extern const aiext_env_t GLOBAL_AIEXT_ENV;

// Map for loaded AI-Extension unit.
//
// This map is initialized during startup, and will never be modified, so it
// does not need to be protected by locks.
static GrowableArrayCHeap<AIExtUnit*, mtCompiler>* loaded_units = nullptr;

// The acceleration table, which is sorted by the class name, method name
// and signature.
//
// This map should be locked properly when accessing it.
static GrowableArrayCHeap<AccelCallEntry*, mtCompiler>* accel_table = nullptr;

// Mutex for acceleration table.
static Mutex* accel_table_lock = nullptr;

int AIExtUnit::compare(AIExtUnit* const& u1, AIExtUnit* const& u2) {
  // Skip parameter list, and multiple versions for same feature is not allowed.
  return strcmp(u1->_feature, u2->_feature);
}

// Max length of feature name and version string.
#define MAX_UNIT_COMPONENT_LEN 50
// Max length of parameter list.
#define MAX_UNIT_PARAM_LIST_LEN 200

// Parses feature and version from the given string with the given length,
// and stores them into the given buffers. Returns `false` on failure.
static bool parse_feature_and_version(const char* str, size_t str_len,
                                      char* feature, char* version) {
  // Find '_' in the string.
  const char* pos = strchr(str, '_');
  if (pos == nullptr) {
    return false;
  }

  // Check and copy the feature name.
  size_t feature_len = pos - str;
  if (feature_len > MAX_UNIT_COMPONENT_LEN || feature_len >= str_len) {
    return false;
  } else {
    strncpy(feature, str, feature_len);
    feature[feature_len] = 0;
  }

  // Check the length of the version string.
  const size_t ver_len = str_len - feature_len - 1;
  const char* ver_str = str + feature_len + 1;
  if (ver_len > MAX_UNIT_COMPONENT_LEN) {
    return false;
  }

  // Check for format and copy the version string.
  size_t i = 0;
  bool has_dot = false;
  for (; i < ver_len; i++) {
    char c = ver_str[i];
    if (c == '.') {
      if (!has_dot) {
        has_dot = true;
      } else {
        // Found multiple dots.
        return false;
      }
    } else if (c < '0' || c > '9') {
      // Found non-digit character.
      return false;
    }
    version[i] = c;
  }
  version[i] = 0;

  assert(strlen(feature) <= MAX_UNIT_COMPONENT_LEN &&
             strlen(version) <= MAX_UNIT_COMPONENT_LEN,
         "sanity");
  return true;
}

// Parses the parameter list from the given string, and stores it
// in the given buffer. Returns `false` on failure.
static bool parse_param_list(const char* str, char* param_list) {
  // Check length and copy to the buffer.
  if (strlen(str) > MAX_UNIT_PARAM_LIST_LEN) {
    return false;
  }
  strcpy(param_list, str);

  // Check for parameter format.
  const char* colon_pos;
  const char* param_group = str;
  while ((colon_pos = strchr(param_group, ':')) != nullptr) {
    const char* delim = strchr(param_group, '=');
    if (delim == nullptr || delim >= colon_pos) {
      return false;  // TODO: check duplicate '=' in one group
    }
    param_group = colon_pos + 1;
  }

  // Check format for the last group.
  const char* delim = strchr(param_group, '=');
  if (delim == nullptr) {
    return false;  // TODO: check duplicate '=' in one group
  }
  return true;
}

AIExtUnit* AIExtUnit::parse_from_arg(const char* arg) {
  // The argument option has pattern `feature_version?param1=val1:param2=val2`.
  char feature[MAX_UNIT_COMPONENT_LEN + 1];
  char version[MAX_UNIT_COMPONENT_LEN + 1];
  char param_list_buf[MAX_UNIT_PARAM_LIST_LEN + 1];
  char* param_list = nullptr;

  const char* pos;
  if ((pos = strchr(arg, '?')) != nullptr) {
    param_list = param_list_buf;
    if (!parse_feature_and_version(arg, pos - arg, feature, version)) {
      return nullptr;
    }
  } else {
    if (!parse_feature_and_version(arg, strlen(arg), feature, version)) {
      return nullptr;
    }
  }

  if (param_list != nullptr && !parse_param_list(pos + 1, param_list)) {
    return nullptr;
  }

  static aiext_handle_t next_handle = 0;
  return new AIExtUnit(feature, version, param_list, next_handle++);
}

#undef MAX_UNIT_COMPONENT_LEN
#undef MAX_UNIT_PARAM_LIST_LEN

// Utility helper to load an AI-Extension unit library from the given path.
// Returns handle of loaded library, `nullptr` for failure.
static void* load_unit(const char* path, aiext_handle_t aiext_handle,
                       bool silent) {
  // Try to load the library.
  char ebuf[1024];
  void* handle = os::dll_load(path, ebuf, sizeof(ebuf));
  if (handle == nullptr) {
    if (!silent) {
      tty->print_cr("Error: Could not load AI-Extension unit `%s`", path);
      tty->print_cr("Error: %s", ebuf);
    }
    return nullptr;
  }

  // Get the entry point.
  aiext_init_t init = (aiext_init_t)os::dll_lookup(handle, "aiext_init");
  if (init == nullptr) {
    if (!silent) {
      tty->print_cr(
          "Error: Could not find `aiext_init` in AI-Extension unit `%s`", path);
    }
    return nullptr;
  }

  // Initialize the AI-Extension unit.
  aiext_result_t result = init(&GLOBAL_AIEXT_ENV, aiext_handle);
  if (result != AIEXT_OK) {
    if (!silent) {
      tty->print_cr("Error: Could not initialize AI-Extension unit `%s`", path);
    }
    return nullptr;
  }
  return handle;
}

bool AIExtUnit::load() {
#if defined(AMD64)
#define CPU_ARCH "x86-64"
#elif defined(AARCH64)
#define CPU_ARCH "aarch64"
#else
#error "Support only x86_64 and AArch64"
#endif
#define CPU_ARCH_LEN (sizeof(CPU_ARCH) - 1)

  const char* java_home = Arguments::get_java_home();

  // Check for `DRAGONWELL_AIEXT_HOME`, used for testing purpose.
  size_t lib_path_len;
  const char* aiext_home = ::getenv("DRAGONWELL_AIEXT_HOME");
  if (aiext_home != nullptr) {
    // Library path: `$DRAGONWELL_AIEXT_HOME/feature_ver_arch.so`.
    lib_path_len = strlen(aiext_home) + strlen(_feature) + strlen(_version) +
                   CPU_ARCH_LEN + sizeof("/__.so") - 1;
  } else {
    // Library path: `$JAVA_HOME/lib/ai-ext/feature_ver_arch.so`.
    lib_path_len = strlen(java_home) + strlen(_feature) + strlen(_version) +
                   CPU_ARCH_LEN + sizeof("/lib/ai-ext/__.so") - 1;
  }

  // Construct library path.
  char* buf = (char*)os::malloc(lib_path_len + 1, mtCompiler);
  assert(buf != nullptr, "OOM on native malloc");
  if (aiext_home != nullptr) {
    snprintf(buf, lib_path_len + 1, "%s/%s_%s_%s.so", aiext_home, _feature,
             _version, CPU_ARCH);
  } else {
    snprintf(buf, lib_path_len + 1, "%s/lib/ai-ext/%s_%s_%s.so", java_home,
             _feature, _version, CPU_ARCH);
  }

  // Length of library path not including the trailing `_arch.so`.
  size_t prefix_len = lib_path_len - CPU_ARCH_LEN - sizeof("_.so") + 1;

  // Try to load with `arch` in path.
  void* lib_handle = load_unit(buf, _aiext_handle, true);
  if (lib_handle == nullptr) {
    // Try without `arch`.
    buf[prefix_len] = 0;
    strcat(buf, ".so");
    lib_handle = load_unit(buf, _aiext_handle, false);
  }
  _handle = lib_handle;

  // Free the buffer.
  os::free(buf);
  return lib_handle != nullptr;

#undef CPU_ARCH
#undef CPU_ARCH_LEN
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

void* AccelCallEntry::get_native_func() const {
  if (_provider == nullptr) {
    return _func_or_data;
  } else {
    return _provider(&GLOBAL_AIEXT_ENV, _native_func_name, _func_or_data);
  }
}

// Adds the given native acceleration unit to table.
static bool add_unit(AIExtUnit* unit) {
  assert(loaded_units != nullptr, "must be initialized");
  bool found;
  int index =
      loaded_units->find_sorted<AIExtUnit*, AIExtUnit::compare>(unit, found);
  if (found) {
    tty->print_cr("Error: Duplicate AI-Extension unit `%s_%s`", unit->feature(),
                  unit->version());
    return false;
  }
  loaded_units->insert_before(index, unit);
  return true;
}

bool AIExt::init() {
  // Quit if AI extension is not enabled.
  if (!UseAIExtension) {
    return true;
  }

  // Create tables.
  // We can not initialize locks now, because mutex is initialized in
  // `os::init_2`, which is called after this. Just leave them null.
  assert(loaded_units == nullptr && accel_table == nullptr,
         "init should only be called once");
  loaded_units = new GrowableArrayCHeap<AIExtUnit*, mtCompiler>();
  accel_table = new GrowableArrayCHeap<AccelCallEntry*, mtCompiler>();

  // Parse AI-Extension units.
  char* args = os::strdup(AIExtensionUnit);
  size_t args_len = strlen(args);
  char* arg = args;
  while (arg < args + args_len) {
    // Find the next unit.
    char* p = arg;
    while (*p != '\n' && *p != '\0') {
      ++p;
    }
    *p = '\0';

    // Parse the current unit.
    AIExtUnit* unit = AIExtUnit::parse_from_arg(arg);
    if (unit == nullptr) {
      tty->print_cr("Error: Invalid AI-Extension option: %s", arg);
      os::free(args);
      return false;
    }

    // Add to the table.
    if (!add_unit(unit)) {
      delete unit;
      os::free(args);
      return false;
    }

    arg = p + 1;
  }
  os::free(args);

  // Check if there are any units.
  if (loaded_units->is_empty()) {
    warning("AI-Extension unit is not provided in JVM arguments");
    return true;
  }

  // Load AI-Extension units.
  for (const auto& e : *loaded_units) {
    if (!e->load()) {
      tty->print_cr("Error: Failed to load AI-Extension unit `%s_%s`",
                    e->_feature, e->_version);
      return false;
    };
  }
  return true;
}

bool AIExt::post_init() {
  if (!UseAIExtension) {
    return true;
  }

  // Create locks.
  assert(accel_table_lock == nullptr, "post init should only be called once");
  accel_table_lock =
      new Mutex(Mutex::oopstorage - 1 /* Higher than tty is enough. */,
                "Native acceleration table lock");

  // Invoke post initialization.
  for (const auto& e : *loaded_units) {
    assert(e->_handle != nullptr, "handle should be set");
    aiext_post_init_t post_init =
        (aiext_post_init_t)os::dll_lookup(e->_handle, "aiext_post_init");
    if (post_init != nullptr) {
      aiext_result_t result = post_init(&GLOBAL_AIEXT_ENV);
      if (result != AIEXT_OK) {
        tty->print_cr(
            "Error: Could not initialize AI-Extension unit after JVM "
            "initialization: `%s_%s`",
            e->_feature, e->_version);
        return false;
      }
    }
  }
  return true;
}

bool AIExt::add_entry(const char* klass, const char* method,
                      const char* signature, const char* native_func_name,
                      void* func_or_data, aiext_naccel_provider_t provider) {
  if (klass == nullptr || method == nullptr || signature == nullptr ||
      native_func_name == nullptr || native_func_name[0] == '\0' ||
      (func_or_data == nullptr && provider == nullptr)) {
    log_error(aiext)("Invalid entry information");
    return false;
  }

  // Create symbols.
  Symbol* klass_sym = SymbolTable::new_permanent_symbol(klass);
  Symbol* method_sym = SymbolTable::new_permanent_symbol(method);
  Symbol* sig_sym = SymbolTable::new_permanent_symbol(signature);

  // Lock the acceleration table.
  MutexLocker ml(accel_table_lock, Mutex::_no_safepoint_check_flag);

  // Check if the entry presents.
  bool found;
  AccelCallEntry key(klass_sym, method_sym, sig_sym);
  int index =
      accel_table->find_sorted<AccelCallEntry*, AccelCallEntry::compare>(&key,
                                                                         found);
  if (found) {
    tty->print_cr(
        "Error: Duplicate native acceleration entry found for %s::%s%s", klass,
        method, signature);
    return false;
  }

  // Create entry and add to table.
  AccelCallEntry* entry = new AccelCallEntry(
      klass_sym, method_sym, sig_sym, native_func_name, func_or_data, provider);
  accel_table->insert_before(index, entry);
  return true;
}

void AIExt::destroy() {
  if (!UseAIExtension) {
    return;
  }

  assert(loaded_units != nullptr && accel_table != nullptr &&
             accel_table_lock != nullptr,
         "should be initialized");

  // Close all loaded libraries and free related resources.
  for (const auto& u : *loaded_units) {
    assert(u->_handle != nullptr, "handle should be set");
    // Call the finalize function if present.
    aiext_finalize_t finalize =
        (aiext_finalize_t)os::dll_lookup(u->_handle, "aiext_finalize");
    if (finalize != nullptr) {
      finalize(&GLOBAL_AIEXT_ENV);
    }

    // Free the unit.
    delete u;
  }

  // Free entries.
  for (const auto& e : *accel_table) {
    delete e;
  }

  // Delete tables and locks.
  delete loaded_units;
  delete accel_table;
  delete accel_table_lock;
}

const AccelCallEntry* AIExt::find(Symbol* klass, Symbol* method,
                                  Symbol* signature) {
  if (!UseAIExtension) {
    return nullptr;
  }

  // Lock the acceleration table.
  MutexLocker ml(accel_table_lock, Mutex::_no_safepoint_check_flag);

  assert(accel_table != nullptr, "must be initialized");
  if (accel_table->is_empty()) {
    return nullptr;
  }

  bool found;
  AccelCallEntry key(klass, method, signature);
  int index =
      accel_table->find_sorted<AccelCallEntry*, AccelCallEntry::compare>(&key,
                                                                         found);

  if (found) {
    return accel_table->at(index);
  }
  return nullptr;
}

#ifdef ASSERT
bool AIExt::is_accel_native_call(CallNode* call) {
  if (!UseAIExtension) {
    return false;
  }

  assert(accel_table != nullptr, "must be initialized");
  if (accel_table->is_empty()) {
    return false;
  }

  CallLeafNode* cl = call->as_CallLeaf();
  if (cl == nullptr) {
    return false;
  }

  for (const auto& e : *accel_table) {
    if (strcmp(e->_native_func_name, cl->_name) == 0) {
      return true;
    }
  }
  return false;
}
#endif  // ASSERT

const AIExtUnit* AIExt::find_unit(aiext_handle_t handle) {
  if (!UseAIExtension) {
    return nullptr;
  }

  assert(loaded_units != nullptr, "must be initialized");
  for (const auto& u : *loaded_units) {
    if (u->_aiext_handle == handle) {
      return u;
    }
  }
  return nullptr;
}

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
  void* native_func = callee->accel_call_entry()->get_native_func();
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

#endif  // defined(INCLUDE_AIEXT) && INCLUDE_AIEXT
