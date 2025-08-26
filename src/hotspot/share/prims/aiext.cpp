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

#include "precompiled.hpp"
#include "aiext.h"
#include "logging/log.hpp"
#include "runtime/flags/jvmFlag.hpp"
#include "runtime/flags/jvmFlagAccess.hpp"

#if INCLUDE_AIEXT
static jint CurrentVersion = AIEXT_VERSION_1;

// Return vm version string
aiext_result_t aiext_get_jvm_version(char* buf, int buf_size) {
  if (buf == nullptr || buf_size <= 0) {
    log_info(aiext)("No output buffer for return value");
    return AIEXT_ERROR;
  }
  snprintf(buf, buf_size, "%s", Abstract_VM_Version::vm_release());
  return AIEXT_OK;
}

// Return current aiext version
jint aiext_get_aiext_version() {
  return CurrentVersion;
}

// Find JVM flag and value, value is stored in value_buf,
aiext_result_t aiext_get_jvm_flag(const char* flag_name, char* value_buf, int buf_size) {
  if (value_buf == nullptr || buf_size <= 0) {
    log_info(aiext)("No output buffer for return value");
    return AIEXT_ERROR;
  }
  JVMFlag* flag = JVMFlag::find_flag(flag_name);
  if (flag == nullptr) {
    return AIEXT_ERROR;
  }
  switch((JVMFlag::FlagType)flag->type()) {
    case JVMFlag::TYPE_bool:
      snprintf(value_buf, buf_size, "%s", flag->get_bool() ? "true" : "false");
      break;
    case JVMFlag::TYPE_int:
      snprintf(value_buf, buf_size, INT32_FORMAT, flag->get_int());
      break;
    case JVMFlag::TYPE_uint:
      snprintf(value_buf, buf_size, UINT32_FORMAT, flag->get_uint());
      break;
    case JVMFlag::TYPE_intx:
      snprintf(value_buf, buf_size, INTX_FORMAT, flag->get_intx());
      break;
    case JVMFlag::TYPE_uintx:
      snprintf(value_buf, buf_size, UINTX_FORMAT, flag->get_uintx());
      break;
    case JVMFlag::TYPE_uint64_t:
      snprintf(value_buf, buf_size, UINTX_FORMAT, flag->get_uint64_t());
      break;
    case JVMFlag::TYPE_size_t:
      snprintf(value_buf, buf_size, SIZE_FORMAT, flag->get_size_t());
      break;
    case JVMFlag::TYPE_double:
      snprintf(value_buf, buf_size, "%.04f", flag->get_double());
      break;
    case JVMFlag::TYPE_ccstr:
      snprintf(value_buf, buf_size, "%s", flag->get_ccstr());
      break;
    case JVMFlag::TYPE_ccstrlist:
      // ccstrlist is seperated by '\n'
      snprintf(value_buf, buf_size, "%s", flag->get_ccstr());
      break;
    default:
      ShouldNotReachHere();
      return AIEXT_ERROR;
  }
  return AIEXT_OK;
}

// Set JVM flag with new value string
aiext_result_t aiext_set_jvm_flag(const char* flag_name, const char* value) {
  JVMFlag* flag = JVMFlag::find_flag(flag_name);
  if (flag == nullptr || value == nullptr) {
    return AIEXT_ERROR;
  }

  JVMFlag::Error result = JVMFlag::ERR_OTHER;
  switch((JVMFlag::FlagType)flag->type()) {
    case JVMFlag::TYPE_bool: {
      bool new_value = true;
      if (strcmp(value, "true") == 0) {
        new_value = true;
      } else if (strcmp(value, "false") == 0) {
        new_value = false;
      } else {
        log_error(aiext)("Not a valid boolean value : %s", value);
        return AIEXT_ERROR;
      }
      result = JVMFlagAccess::set_bool(flag, &new_value, JVMFlagOrigin::INTERNAL);
      break;
    }
    case JVMFlag::TYPE_int: {
      int new_value;
      int read = sscanf(value, INT32_FORMAT, &new_value);
      if (read != 1) {
        log_error(aiext)("Not a valid int value : %s", value);
        return AIEXT_ERROR;
      }
      result = JVMFlagAccess::set_int(flag, &new_value, JVMFlagOrigin::INTERNAL);
      break;
    }
    case JVMFlag::TYPE_uint: {
      uint new_value;
      int read = sscanf(value, UINT32_FORMAT, &new_value);
      if (read != 1) {
        log_error(aiext)("Not a valid uint value : %s", value);
        return AIEXT_ERROR;
      }
      result = JVMFlagAccess::set_uint(flag, &new_value, JVMFlagOrigin::INTERNAL);
      break;
    }
    case JVMFlag::TYPE_intx: {
      intx new_value;
      int read = sscanf(value, INTX_FORMAT, &new_value);
      if (read != 1) {
        log_error(aiext)("Not a valid intx value : %s", value);
        return AIEXT_ERROR;
      }
      result = JVMFlagAccess::set_intx(flag, &new_value, JVMFlagOrigin::INTERNAL);
      break;
    }
    case JVMFlag::TYPE_uintx: {
      uintx new_value;
      int read = sscanf(value, UINTX_FORMAT, &new_value);
      if (read != 1) {
        log_error(aiext)("Not a valid uintx value : %s", value);
        return AIEXT_ERROR;
      }
      result = JVMFlagAccess::set_uintx(flag, &new_value, JVMFlagOrigin::INTERNAL);
      break;
    }
    case JVMFlag::TYPE_uint64_t: {
      uint64_t new_value;
      int read = sscanf(value, UINT64_FORMAT, &new_value);
      if (read != 1) {
        log_error(aiext)("Not a valid uint64 value : %s", value);
        return AIEXT_ERROR;
      }
      result = JVMFlagAccess::set_uint64_t(flag, &new_value, JVMFlagOrigin::INTERNAL);
      break;
    }
    case JVMFlag::TYPE_size_t: {
      size_t new_value;
      int read = sscanf(value, SIZE_FORMAT, &new_value);
      if (read != 1) {
        log_error(aiext)("Not a valid size_t value : %s", value);
        return AIEXT_ERROR;
      }
      result = JVMFlagAccess::set_size_t(flag, &new_value, JVMFlagOrigin::INTERNAL);
      break;
    }
    case JVMFlag::TYPE_double: {
      double new_value;
      int read = sscanf(value, "%lg", &new_value);
      if (read != 1) {
        log_error(aiext)("Not a valid double value : %s", value);
        return AIEXT_ERROR;
      }
      result = JVMFlagAccess::set_double(flag, &new_value, JVMFlagOrigin::INTERNAL);
      break;
    }
    case JVMFlag::TYPE_ccstr:
    case JVMFlag::TYPE_ccstrlist: {
        result = JVMFlagAccess::set_ccstr(flag, &value, JVMFlagOrigin::INTERNAL);
      break;
    }
    default:
      ShouldNotReachHere();
      return AIEXT_ERROR;
  }
  return result == JVMFlag::SUCCESS ? AIEXT_OK : AIEXT_ERROR;
}

// Check if cpu feature is supported
jboolean aiext_support_cpu_feature(const char* feature) {
  const char* cpu_feature = Abstract_VM_Version::features_string();
  log_debug(aiext)("cpu features:%s", cpu_feature);
  const char* loc = strstr(cpu_feature, feature);
  jboolean result = false;
  if (loc !=  nullptr) {
    // check if it is surrounded by space or is the last feature
    if (*(loc-1) == ' ' && (*(loc + strlen(feature)) == ','
                         || *(loc + strlen(feature)) == 0 )) {
      result = true;
    }
  }
  return result;
}

// Register native accel function
aiext_result_t aiext_register_native_accel_provider(const char* klass, const char* method, const char* sig, void* (*provider)(const AIEXT_ENV* env)) {
  return AIEXT_ERROR;
}

// Get field offset, return -1 for failure
jlong aiext_get_field_offset(const char* klass, const char* method, const char* sig) {
  // Unimplemented
  return -1;
}

// Get extension info (name, version, param_list)
aiext_result_t aiext_get_extension_info(const aiext_handle_t handle, char* name_buf, int name_buf_size, char* version_info, int version_buf_size, char* param_list_buf, int param_list_buf_size) {
  return AIEXT_ERROR;
}

extern const AIEXT_ENV _global_aiext_env = {
    aiext_get_jvm_version,
    aiext_get_aiext_version,
    aiext_get_jvm_flag,
    aiext_set_jvm_flag,
    aiext_support_cpu_feature,
    aiext_register_native_accel_provider,
    aiext_get_field_offset,
    aiext_get_extension_info
};

#endif // INCLUDE_AIEXT