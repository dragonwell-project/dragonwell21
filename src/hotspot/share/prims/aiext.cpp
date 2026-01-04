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

static unsigned int CurrentVersion = AIEXT_VERSION_2;

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
static unsigned int get_aiext_version() { return CurrentVersion; }

#define DEF_GET_JVM_FLAG(n, t)                                         \
  static aiext_result_t get_jvm_flag_##n(const char* name, t* value) { \
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

// Gets field offset in a Java class, returns `-1` on failure.
static int get_field_offset(const char* klass, const char* field,
                            const char* sig) {
  // Get the current Java thread.
  Thread* thread = Thread::current();
  if (!thread->is_Java_thread()) {
    log_info(aiext)("Current thread is not a Java thread");
    return -1;
  }
  JavaThread* THREAD = JavaThread::cast(thread);

  // Get class name symbol.
  if (klass == nullptr || (int)strlen(klass) > Symbol::max_length()) {
    log_info(aiext)("Invalid class name %s",
                    klass == nullptr ? "<null>" : klass);
    return -1;
  }
  TempNewSymbol class_name = SymbolTable::new_symbol(klass);

  // Extract pending exception.
  struct PendingExceptionGuard {
    JavaThread* thread;
    Handle pending_exception;
    const char* exception_file;
    int exception_line;

    PendingExceptionGuard(TRAPS) {
      thread = THREAD;
      pending_exception = Handle(THREAD, PENDING_EXCEPTION);
      exception_file = THREAD->exception_file();
      exception_line = THREAD->exception_line();
      CLEAR_PENDING_EXCEPTION;
    }

    ~PendingExceptionGuard() {
      // Restore pending exception.
      if (pending_exception.not_null()) {
        thread->set_pending_exception(pending_exception(), exception_file,
                                      exception_line);
      }
    }

    void log(const char* op) const {
      if (log_is_enabled(Info, aiext)) {
        oop ex = thread->pending_exception();
        log_info(aiext)(
            "Exception while %s, %s: %s", op, ex->klass()->external_name(),
            java_lang_String::as_utf8_string(java_lang_Throwable::message(ex)));
      }
    }
  } except_guard(THREAD);

  // Transition thread state to VM.
  ThreadInVMfromNative state_guard(THREAD);

  // Get class loader.
  Handle protection_domain;
  Handle loader(THREAD, SystemDictionary::java_system_loader());
  Klass* k = THREAD->security_get_caller_class(0);
  if (k != nullptr) {
    loader = Handle(THREAD, k->class_loader());
  }

  // Find class from the class loader.
  k = SystemDictionary::resolve_or_null(class_name, loader, protection_domain,
                                        THREAD);
  if (HAS_PENDING_EXCEPTION) {
    except_guard.log("resolving class");
    k = nullptr;
    CLEAR_PENDING_EXCEPTION;
  }

  // Bail out if class not found, or not an instance class,
  // or is not initialized.
  if (k == nullptr) {
    log_info(aiext)("Class %s not found", klass);
    return -1;
  }
  if (!k->is_instance_klass()) {
    log_info(aiext)("Class %s is not an instance class", klass);
    return -1;
  }
  InstanceKlass* ik = InstanceKlass::cast(k);
  if (!ik->is_initialized()) {
    log_info(aiext)("Class %s is not initialized", klass);
    return -1;
  }

  // The class should have been loaded, so the field and signature
  // should already be in the symbol table.
  // If they're not there, the field doesn't exist.
  TempNewSymbol field_name = SymbolTable::probe(field, (int)strlen(field));
  TempNewSymbol sig_name = SymbolTable::probe(sig, (int)strlen(sig));
  fieldDescriptor fd;
  if (field_name == nullptr || sig_name == nullptr ||
      ik->find_field(field_name, sig_name, false, &fd) == nullptr) {
    log_info(aiext)("Non-static field %s.%s not found in class %s", field, sig,
                    klass);
    return -1;
  }

  return fd.offset();
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
    get_field_offset,

    // Unit information.
    get_unit_info,

    // JNI.
    get_jni_env,

    // Object/pointer layout.
    get_array_layout,
    get_narrow_oop_layout,
};
