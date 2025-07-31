/*
 * Copyright (c) 2015, 2023, Oracle and/or its affiliates. All rights reserved.
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
#include "runtime/flags/jvmFlag.hpp"
#include "runtime/flags/jvmFlagLimit.hpp"
#include "runtime/flags/jvmFlagConstraintsRuntime.hpp"
#include "runtime/globals.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/os.hpp"
#include "runtime/safepointMechanism.hpp"
#include "runtime/task.hpp"
#include "utilities/powerOfTwo.hpp"

JVMFlag::Error ObjectAlignmentInBytesConstraintFunc(int value, bool verbose) {
  bool verifyFailed = false;
  if (!is_power_of_2(value)) {
    if (VerifyFlagConstraints) {
      verifyFailed = true;
      value = round_down_power_of_2(value);
    } else {
      JVMFlag::printError(verbose,
                          "ObjectAlignmentInBytes (%d) must be "
                          "power of 2\n",
                          value);
      return JVMFlag::VIOLATES_CONSTRAINT;
    }
  }
  // In case page size is very small.
  if (value >= (intx)os::vm_page_size()) {
    if (VerifyFlagConstraints) {
      verifyFailed = true;
      value = (intx)(os::vm_page_size()/2);
    } else {
      JVMFlag::printError(verbose,
                        "ObjectAlignmentInBytes (%d) must be "
                        "less than page size (" SIZE_FORMAT ")\n",
                        value, (intx)os::vm_page_size());
      return JVMFlag::VIOLATES_CONSTRAINT;
    }
  }
  if (verifyFailed) {
    ObjectAlignmentInBytes = value;
    JVMFlag::printError(true, "ObjectAlignmentInBytes:%d\n", value);
  }
  return JVMFlag::SUCCESS;
}

// Need to enforce the padding not to break the existing field alignments.
// It is sufficient to check against the largest type size.
JVMFlag::Error ContendedPaddingWidthConstraintFunc(intx value, bool verbose) {
  if ((value % BytesPerLong) != 0) {
    if (VerifyFlagConstraints) {
      int remainder = value % BytesPerLong;
      ContendedPaddingWidth = value - remainder;
      JVMFlag::printError(true, "ContendedPaddingWidth:" INTX_FORMAT "\n", ContendedPaddingWidth);
      return JVMFlag::SUCCESS;
    }
    JVMFlag::printError(verbose,
                        "ContendedPaddingWidth (" INTX_FORMAT ") must be "
                        "a multiple of %d\n",
                        value, BytesPerLong);
    return JVMFlag::VIOLATES_CONSTRAINT;
  } else {
    return JVMFlag::SUCCESS;
  }
}

JVMFlag::Error PerfDataSamplingIntervalFunc(intx value, bool verbose) {
  if ((value % PeriodicTask::interval_gran != 0)) {
    if (VerifyFlagConstraints) {
      int remainder = value % PeriodicTask::interval_gran;
      PerfDataSamplingInterval = value - remainder;
      JVMFlag::printError(true, "PerfDataSamplingInterval:" INTX_FORMAT "\n", PerfDataSamplingInterval);
      return JVMFlag::SUCCESS;
    }
    JVMFlag::printError(verbose,
                        "PerfDataSamplingInterval (" INTX_FORMAT ") must be "
                        "evenly divisible by PeriodicTask::interval_gran (%d)\n",
                        value, PeriodicTask::interval_gran);
    return JVMFlag::VIOLATES_CONSTRAINT;
  } else {
    return JVMFlag::SUCCESS;
  }
}

JVMFlag::Error VMPageSizeConstraintFunc(uintx value, bool verbose) {
  uintx min = (uintx)os::vm_page_size();
  if (value < min) {
    if (VerifyFlagConstraints) {
      JVMFlagAccess::set_uintx(const_cast<JVMFlag*>(JVMFlagLimit::last_checked_flag()), &value, JVMFlagOrigin::ERGONOMIC);
      // JVMFlagLimit::last_checked_flag()->write(min);
      JVMFlag::printError(true, "%s:" UINTX_FORMAT "\n", JVMFlagLimit::last_checked_flag()->name(), JVMFlagLimit::last_checked_flag()->read<uintx>());
      return JVMFlag::SUCCESS;
    }

    JVMFlag::printError(verbose,
                        "%s %s=" UINTX_FORMAT " is outside the allowed range [ " UINTX_FORMAT
                        " ... " UINTX_FORMAT " ]\n",
                        JVMFlagLimit::last_checked_flag()->type_string(),
                        JVMFlagLimit::last_checked_flag()->name(),
                        value, min, max_uintx);
    return JVMFlag::VIOLATES_CONSTRAINT;
  }

  return JVMFlag::SUCCESS;
}

JVMFlag::Error NUMAInterleaveGranularityConstraintFunc(size_t value, bool verbose) {
  size_t min = os::vm_allocation_granularity();
  size_t max = NOT_LP64(2*G) LP64_ONLY(8192*G);

  if (value < min) {
    if (VerifyFlagConstraints) {
      NUMAInterleaveGranularity = min;
      JVMFlag::printError(true, "NUMAInterleaveGranularity:" UINTX_FORMAT "\n", min);
      return JVMFlag::SUCCESS;
    }
    JVMFlag::printError(verbose,
                        "size_t NUMAInterleaveGranularity=" UINTX_FORMAT " is outside the allowed range [ " UINTX_FORMAT
                        " ... " UINTX_FORMAT " ]\n", value, min, max);
    return JVMFlag::VIOLATES_CONSTRAINT;
  }

  if (value > max) {
    if (VerifyFlagConstraints) {
      NUMAInterleaveGranularity = max;
      JVMFlag::printError(true, "NUMAInterleaveGranularity:" UINTX_FORMAT "\n", max);
      return JVMFlag::SUCCESS;
    }
    JVMFlag::printError(verbose,
                        "size_t NUMAInterleaveGranularity=" UINTX_FORMAT " is outside the allowed range [ " UINTX_FORMAT
                        " ... " UINTX_FORMAT " ]\n", value, min, max);
    return JVMFlag::VIOLATES_CONSTRAINT;
  }

  return JVMFlag::SUCCESS;
}
