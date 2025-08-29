/*
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

#if INCLUDE_AIEXT

#include "aiext.h"

#include "logging/log.hpp"
#include "opto/nativeAcceleration.hpp"
#include "precompiled.hpp"
#include "runtime/flags/jvmFlag.hpp"
#include "runtime/flags/jvmFlagAccess.hpp"

static int CurrentVersion = AIEXT_VERSION_1;

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
static int get_aiext_version() { return CurrentVersion; }

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
  // TODO: support provider.
  AccelCallEntry* entry = NativeAccelTable::add_entry(
      klass, method, sig, native_func_name, func_or_data);
  return entry == nullptr ? AIEXT_ERROR : AIEXT_OK;
}

// Gets field offset in a Java class, returns `-1` on failure.
static int64_t get_field_offset(const char* klass, const char* method,
                                const char* sig) {
  // Unimplemented.
  return -1;
}

// Gets unit info, including feature name, version and parameter list.
static aiext_result_t get_unit_info(const aiext_handle_t handle,
                                    char* feature_buf, size_t feature_buf_size,
                                    char* version_buf, size_t version_buf_size,
                                    char* param_list_buf,
                                    size_t param_list_buf_size) {
  // Find the given unit.
  const NativeAccelUnit* unit = NativeAccelTable::find_unit(handle);
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

extern const aiext_env_t GLOBAL_AIEXT_ENV = {
    get_jvm_version,
    get_aiext_version,
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
    register_naccel_provider,
    get_field_offset,
    get_unit_info,
};

#endif  // INCLUDE_AIEXT
