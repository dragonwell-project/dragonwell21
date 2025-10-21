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

#include "aiext.h"

#include <string.h>

#include "precompiled.hpp"
#include "classfile/classLoaderData.hpp"
#include "classfile/classLoaderDataGraph.inline.hpp"
#include "classfile/dictionary.hpp"
#include "classfile/javaClasses.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "logging/log.hpp"
#include "oops/compressedOops.hpp"
#include "oops/oop.inline.hpp"
#include "oops/symbolHandle.hpp"
#include "opto/aiExtension.hpp"
#include "runtime/fieldDescriptor.inline.hpp"
#include "runtime/flags/jvmFlag.hpp"
#include "runtime/flags/jvmFlagAccess.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/signature.hpp"

static const unsigned int CURRENT_VERSION = AIEXT_VERSION_2;

// Returns JVM version string.
static aiext_result_t get_jvm_version(char* buf, size_t buf_size) {
  if (buf == nullptr || buf_size == 0) {
    log_info(aiext)("No output buffer for return value");
    return AIEXT_ERROR;
  }
  snprintf(buf, buf_size, "%s", Abstract_VM_Version::vm_release());
  return AIEXT_OK;
}

// Returns current AI-Extension version.
static unsigned int get_aiext_version() { return CURRENT_VERSION; }

#define DEF_GET_JVM_FLAG(n, t)                                         \
  static aiext_result_t get_jvm_flag_##n(const char* name, t* value) { \
    if (name == nullptr) {                                             \
      log_info(aiext)("Invalid flag name");                            \
      return AIEXT_ERROR;                                              \
    }                                                                  \
    JVMFlag* flag = JVMFlag::find_flag(name);                          \
    if (flag == nullptr || flag->type() != JVMFlag::TYPE_##n) {        \
      log_info(aiext)("Flag %s not found or type mismatch", name);     \
      return AIEXT_ERROR;                                              \
    }                                                                  \
    if (value == nullptr) {                                            \
      log_info(aiext)("Invalid value pointer");                        \
      return AIEXT_ERROR;                                              \
    }                                                                  \
    *value = flag->get_##n();                                          \
    return AIEXT_OK;                                                   \
  }

DEF_GET_JVM_FLAG(bool, int)
DEF_GET_JVM_FLAG(int, int)
DEF_GET_JVM_FLAG(uint, unsigned int)
DEF_GET_JVM_FLAG(intx, intptr_t)
DEF_GET_JVM_FLAG(uintx, uintptr_t)
DEF_GET_JVM_FLAG(uint64_t, uint64_t)
DEF_GET_JVM_FLAG(size_t, size_t)
DEF_GET_JVM_FLAG(double, double)

static aiext_result_t get_jvm_flag_ccstr(const char* name, char* buf,
                                         size_t buf_size) {
  if (name == nullptr) {
    log_info(aiext)("Invalid flag name");
    return AIEXT_ERROR;
  }
  JVMFlag* flag = JVMFlag::find_flag(name);
  if (flag == nullptr || (flag->type() != JVMFlag::TYPE_ccstr &&
                          flag->type() != JVMFlag::TYPE_ccstrlist)) {
    log_info(aiext)("Flag %s not found or type mismatch", name);
    return AIEXT_ERROR;
  }
  if (buf == nullptr || buf_size == 0) {
    log_info(aiext)("No output buffer for return value");
    return AIEXT_ERROR;
  }
  snprintf(buf, buf_size, "%s", flag->get_ccstr());
  return AIEXT_OK;
}

#undef DEF_GET_JVM_FLAG

#define DEF_SET_JVM_FLAG(n, t)                                         \
  static aiext_result_t set_jvm_flag_##n(const char* name, t value) {  \
    if (name == nullptr) {                                             \
      log_info(aiext)("Invalid flag name");                            \
      return AIEXT_ERROR;                                              \
    }                                                                  \
    JVMFlag* flag = JVMFlag::find_flag(name);                          \
    if (flag == nullptr || flag->type() != JVMFlag::TYPE_##n) {        \
      log_info(aiext)("Flag %s not found or type mismatch", name);     \
      return AIEXT_ERROR;                                              \
    }                                                                  \
    JVMFlag::Error result =                                            \
        JVMFlagAccess::set_##n(flag, &value, JVMFlagOrigin::INTERNAL); \
    return result == JVMFlag::SUCCESS ? AIEXT_OK : AIEXT_ERROR;        \
  }

static aiext_result_t set_jvm_flag_bool(const char* name, int value) {
  if (name == nullptr) {
    log_info(aiext)("Invalid flag name");
    return AIEXT_ERROR;
  }
  JVMFlag* flag = JVMFlag::find_flag(name);
  if (flag == nullptr || flag->type() != JVMFlag::TYPE_bool) {
    log_info(aiext)("Flag %s not found or type mismatch", name);
    return AIEXT_ERROR;
  }
  bool b = !!value;
  JVMFlag::Error result =
      JVMFlagAccess::set_bool(flag, &b, JVMFlagOrigin::INTERNAL);
  return result == JVMFlag::SUCCESS ? AIEXT_OK : AIEXT_ERROR;
}

DEF_SET_JVM_FLAG(int, int)
DEF_SET_JVM_FLAG(uint, unsigned int)
DEF_SET_JVM_FLAG(intx, intptr_t)
DEF_SET_JVM_FLAG(uintx, uintptr_t)
DEF_SET_JVM_FLAG(uint64_t, uint64_t)
DEF_SET_JVM_FLAG(size_t, size_t)
DEF_SET_JVM_FLAG(double, double)

static aiext_result_t set_jvm_flag_ccstr(const char* name, const char* value) {
  if (name == nullptr) {
    log_info(aiext)("Invalid flag name");
    return AIEXT_ERROR;
  }
  JVMFlag* flag = JVMFlag::find_flag(name);
  if (flag == nullptr || (flag->type() != JVMFlag::TYPE_ccstr &&
                          flag->type() != JVMFlag::TYPE_ccstrlist)) {
    log_info(aiext)("Flag %s not found or type mismatch", name);
    return AIEXT_ERROR;
  }
  JVMFlag::Error result =
      JVMFlagAccess::set_ccstr(flag, &value, JVMFlagOrigin::INTERNAL);
  return result == JVMFlag::SUCCESS ? AIEXT_OK : AIEXT_ERROR;
}

#undef DEF_SET_JVM_FLAG

// Registers native acceleration provider for specific Java method.
static aiext_result_t register_naccel_provider(
    const char* klass, const char* method, const char* sig,
    const char* native_func_name, void* func_or_data,
    aiext_naccel_provider_t provider) {
  bool result = AIExt::add_entry(klass, method, sig, native_func_name,
                                 func_or_data, provider);
  return result ? AIEXT_OK : AIEXT_ERROR;
}

// Gets unit info, including feature name, version and parameter list.
static aiext_result_t get_unit_info(aiext_handle_t handle, char* feature_buf,
                                    size_t feature_buf_size, char* version_buf,
                                    size_t version_buf_size,
                                    char* param_list_buf,
                                    size_t param_list_buf_size) {
  // Find the given unit.
  const AIExtUnit* unit = AIExt::find_unit(handle);
  if (unit == nullptr) {
    return AIEXT_ERROR;
  }

  // Copy to buffers.
  if (feature_buf != nullptr && feature_buf_size > 0) {
    snprintf(feature_buf, feature_buf_size, "%s", unit->feature());
  }
  if (version_buf != nullptr && version_buf_size > 0) {
    snprintf(version_buf, version_buf_size, "%s", unit->version());
  }
  if (param_list_buf != nullptr && param_list_buf_size > 0) {
    const char* param_list = unit->param_list();
    if (param_list == nullptr) {
      param_list = "";
    }
    snprintf(param_list_buf, param_list_buf_size, "%s", param_list);
  }
  return AIEXT_OK;
}

// Gets JNI environment.
static JNIEnv* get_jni_env() {
  return JavaThread::current()->jni_environment();
}

// Converts `aiext_value_type_t` to `BasicType`.
static aiext_result_t to_basic_type(aiext_value_type_t type, BasicType& bt) {
  switch (type) {
    case AIEXT_TYPE_BOOLEAN:
      bt = T_BOOLEAN;
      break;
    case AIEXT_TYPE_CHAR:
      bt = T_CHAR;
      break;
    case AIEXT_TYPE_FLOAT:
      bt = T_FLOAT;
      break;
    case AIEXT_TYPE_DOUBLE:
      bt = T_DOUBLE;
      break;
    case AIEXT_TYPE_BYTE:
      bt = T_BYTE;
      break;
    case AIEXT_TYPE_SHORT:
      bt = T_SHORT;
      break;
    case AIEXT_TYPE_INT:
      bt = T_INT;
      break;
    case AIEXT_TYPE_LONG:
      bt = T_LONG;
      break;
    case AIEXT_TYPE_OBJECT:
      bt = T_OBJECT;
      break;
    case AIEXT_TYPE_ARRAY:
      bt = T_ARRAY;
      break;
    default:
      log_info(aiext)("Invalid value type %d", type);
      return AIEXT_ERROR;
  }
  return AIEXT_OK;
}

// Gets Java array layout.
static aiext_result_t get_array_layout(aiext_value_type_t elem_type,
                                       size_t* length_offset,
                                       size_t* data_offset, size_t* elem_size) {
  BasicType bt;
  aiext_result_t result = to_basic_type(elem_type, bt);
  if (result != AIEXT_OK) {
    return result;
  }

  if (length_offset != nullptr) {
    *length_offset = arrayOopDesc::length_offset_in_bytes();
  }
  if (data_offset != nullptr) {
    *data_offset = arrayOopDesc::base_offset_in_bytes(bt);
  }
  if (elem_size != nullptr) {
    *elem_size = type2aelembytes(bt);
  }

  return AIEXT_OK;
}

// Gets the layout of narrow oop.
static aiext_result_t get_narrow_oop_layout(uint32_t* null, uintptr_t* base,
                                            size_t* shift) {
  if (null != nullptr) {
    *null = (uint32_t)narrowOop::null;
  }
  if (base != nullptr) {
    *base = (uintptr_t)CompressedOops::base();
  }
  if (shift != nullptr) {
    *shift = CompressedOops::shift();
  }
  return AIEXT_OK;
}

// Gets the current Java thread.
static JavaThread* get_current_java_thread() {
  Thread* thread = Thread::current();
  if (!thread->is_Java_thread()) {
    log_info(aiext)("Current thread is not a Java thread");
    return nullptr;
  }
  return JavaThread::cast(thread);
}

// Finds the given class in the given class loader.
static Klass* find_class(Symbol* class_name, Handle class_loader,
                         Handle protection_domain, TRAPS) {
  if (Signature::is_array(class_name) || Signature::has_envelope(class_name)) {
    return nullptr;
  }

  class_loader = Handle(
      THREAD,
      java_lang_ClassLoader::non_reflection_class_loader(class_loader()));
  ClassLoaderData* loader_data =
      class_loader() == nullptr
          ? ClassLoaderData::the_null_class_loader_data()
          : ClassLoaderDataGraph::find_or_create(class_loader);

  Dictionary* dictionary = loader_data->dictionary();
  return dictionary->find(THREAD, class_name, protection_domain);
}

// Gets the field descriptor of the given field.
// Returns `false` on failure.
static bool get_field_descriptor(const char* klass, const char* field,
                                 const char* sig, bool is_static,
                                 fieldDescriptor& fd, TRAPS) {
  // Get class name symbol.
  if (klass == nullptr || (int)strlen(klass) > Symbol::max_length()) {
    log_info(aiext)("Invalid class name %s",
                    klass == nullptr ? "<null>" : klass);
    return false;
  }
  TempNewSymbol class_name = SymbolTable::new_symbol(klass);

  // Get class loader.
  Handle protection_domain;
  Handle loader(THREAD, SystemDictionary::java_system_loader());
  Klass* k = THREAD->security_get_caller_class(0);
  if (k != nullptr) {
    loader = Handle(THREAD, k->class_loader());
  }

  // Find class from the class loader.
  k = find_class(class_name, loader, protection_domain, THREAD);
  if (k == nullptr) {
    log_info(aiext)("Class %s not found", klass);
    return false;
  }
  if (!k->is_instance_klass()) {
    log_info(aiext)("Class %s is not an instance class", klass);
    return false;
  }
  InstanceKlass* ik = InstanceKlass::cast(k);
  if (!ik->is_initialized()) {
    log_info(aiext)("Class %s is not initialized", klass);
    return false;
  }

  // The class should have been loaded, so the field and signature
  // should already be in the symbol table.
  // If they're not there, the field doesn't exist.
  TempNewSymbol field_name = SymbolTable::probe(field, (int)strlen(field));
  TempNewSymbol sig_name = SymbolTable::probe(sig, (int)strlen(sig));
  if (field_name == nullptr || sig_name == nullptr ||
      ik->find_field(field_name, sig_name, is_static, &fd) == nullptr) {
    log_info(aiext)("Field %s.%s not found in class %s", field, sig, klass);
    return false;
  }

  // Done.
  return true;
}

// Gets field offset in a Java class, returns `-1` on failure.
static int get_field_offset(const char* klass, const char* field,
                            const char* sig) {
  // Get the current Java thread.
  JavaThread* THREAD = get_current_java_thread();
  if (THREAD == nullptr) {
    return -1;
  }

  // Transition thread state to VM.
  ThreadInVMfromNative state_guard(THREAD);
  ResetNoHandleMark rnhm;
  HandleMark hm(THREAD);

  fieldDescriptor fd;
  if (!get_field_descriptor(klass, field, sig, false, fd, THREAD)) {
    return -1;
  }
  return fd.offset();
}

// Gets address of the given static field in a Java class,
// returns `nullptr` on failure.
static void* get_static_field_addr(const char* klass, const char* field,
                                   const char* sig) {
  // Get the current Java thread.
  JavaThread* THREAD = get_current_java_thread();
  if (THREAD == nullptr) {
    return nullptr;
  }

  // Transition thread state to VM.
  ThreadInVMfromNative state_guard(THREAD);
  ResetNoHandleMark rnhm;
  HandleMark hm(THREAD);

  fieldDescriptor fd;
  if (!get_field_descriptor(klass, field, sig, true, fd, THREAD)) {
    return nullptr;
  }
  return fd.field_holder()->java_mirror()->field_addr<void>(fd.offset());
}

extern const aiext_env_t GLOBAL_AIEXT_ENV = {
    // Version.
    get_jvm_version,
    get_aiext_version,

    // JVM flag access.
    get_jvm_flag_bool,
    get_jvm_flag_int,
    get_jvm_flag_uint,
    get_jvm_flag_intx,
    get_jvm_flag_uintx,
    get_jvm_flag_uint64_t,
    get_jvm_flag_size_t,
    get_jvm_flag_double,
    get_jvm_flag_ccstr,
    set_jvm_flag_bool,
    set_jvm_flag_int,
    set_jvm_flag_uint,
    set_jvm_flag_intx,
    set_jvm_flag_uintx,
    set_jvm_flag_uint64_t,
    set_jvm_flag_size_t,
    set_jvm_flag_double,
    set_jvm_flag_ccstr,

    // Native acceleration.
    register_naccel_provider,

    // Unit information.
    get_unit_info,

    // JNI.
    get_jni_env,

    // Object/pointer layout.
    get_array_layout,
    get_narrow_oop_layout,
    get_field_offset,
    get_static_field_addr,
};
