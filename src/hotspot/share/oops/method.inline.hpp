/*
 * Copyright (c) 2018, 2023, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_OOPS_METHOD_INLINE_HPP
#define SHARE_OOPS_METHOD_INLINE_HPP

#include "oops/method.hpp"

#include "classfile/vmIntrinsics.hpp"
#include "code/codeCache.hpp"
#include "runtime/atomic.hpp"

inline address Method::from_compiled_entry() const {
#if INCLUDE_OPT_META_SIZE
  MethodEntry me = Atomic::load_acquire(&_from_compiled_entry);
  if (me == 0) {
    return nullptr;
  } else {
    return me + CodeCache::low_bound();
  }
#else
  return Atomic::load_acquire(&_from_compiled_entry);
#endif
}

inline void Method::set_from_compiled_entry(address entry) {
#if INCLUDE_OPT_META_SIZE
  assert(entry == nullptr || CodeCache::contains(entry), "sanity");
  MethodEntry me = entry == nullptr ? 0 : (MethodEntry)(entry - CodeCache::low_bound());
  _from_compiled_entry =  me;
#else
  _from_compiled_entry =  entry;
#endif
}

inline address Method::from_interpreted_entry() const {
#if INCLUDE_OPT_META_SIZE
  MethodEntry entry = Atomic::load_acquire(&_from_interpreted_entry);
  if (entry == 0) {
    return nullptr;
  } else {
    return entry + CodeCache::low_bound();
  }
#else
  return Atomic::load_acquire(&_from_interpreted_entry);
#endif
}

inline void Method::set_from_interpreted_entry(address entry) {
#if INCLUDE_OPT_META_SIZE
  assert(entry == nullptr || CodeCache::contains(entry), "sanity");
  MethodEntry offset = entry == nullptr ? (MethodEntry) 0 : entry - CodeCache::low_bound();
  _from_interpreted_entry = offset;
#else
  _from_interpreted_entry = entry;
#endif
}

inline address Method::interpreter_entry() const {
#if INCLUDE_OPT_META_SIZE
  return _i2i_entry == 0 ? nullptr : _i2i_entry + CodeCache::low_bound();
#else
  return _i2i_entry;
#endif
}

inline void Method::set_interpreter_entry(address entry) {
#if INCLUDE_OPT_META_SIZE
  assert(entry == nullptr || CodeCache::contains(entry), "sanity");
  MethodEntry offset = entry == nullptr ? (MethodEntry) 0 : entry - CodeCache::low_bound();
  if (_i2i_entry != offset) {
    _i2i_entry = offset;
  }
  if (_from_interpreted_entry != offset) {
    _from_interpreted_entry = offset;
  }
#else
  if (_i2i_entry != entry) {
    _i2i_entry = entry;
  }
  if (_from_interpreted_entry != entry) {
    _from_interpreted_entry = entry;
  }
#endif
}

inline address Method::i2i_entry() const {
  return interpreter_entry();
}

inline void Method::set_i2i_entry(address entry) {
#if INCLUDE_OPT_META_SIZE
  assert(entry == nullptr || CodeCache::contains(entry), "sanity");
  MethodEntry offset = entry == nullptr ? (MethodEntry) 0 : entry - CodeCache::low_bound();
  _i2i_entry = offset;
#else
  _i2i_entry = entry;
#endif
}

inline void Method::set_method_data(MethodData* data) {
  // The store into method must be released. On platforms without
  // total store order (TSO) the reference may become visible before
  // the initialization of data otherwise.
  Atomic::release_store(&_method_data, data);
}

inline CompiledMethod* volatile Method::code() const {
  assert( check_code(), "" );
  return Atomic::load_acquire(&_code);
}

// Write (bci, line number) pair to stream
inline void CompressedLineNumberWriteStream::write_pair_regular(int bci_delta, int line_delta) {
  // bci and line number does not compress into single byte.
  // Write out escape character and use regular compression for bci and line number.
  write_byte((jubyte)0xFF);
  write_signed_int(bci_delta);
  write_signed_int(line_delta);
}

inline void CompressedLineNumberWriteStream::write_pair_inline(int bci, int line) {
  int bci_delta = bci - _bci;
  int line_delta = line - _line;
  _bci = bci;
  _line = line;
  // Skip (0,0) deltas - they do not add information and conflict with terminator.
  if (bci_delta == 0 && line_delta == 0) return;
  // Check if bci is 5-bit and line number 3-bit unsigned.
  if (((bci_delta & ~0x1F) == 0) && ((line_delta & ~0x7) == 0)) {
    // Compress into single byte.
    jubyte value = (jubyte)((bci_delta << 3) | line_delta);
    // Check that value doesn't match escape character.
    if (value != 0xFF) {
      write_byte(value);
      return;
    }
  }
  write_pair_regular(bci_delta, line_delta);
}

inline void CompressedLineNumberWriteStream::write_pair(int bci, int line) {
  write_pair_inline(bci, line);
}

inline bool Method::has_compiled_code() const { return code() != nullptr; }

inline bool Method::is_empty_method() const {
  return  code_size() == 1
      && *code_base() == Bytecodes::_return;
}

inline bool Method::is_continuation_enter_intrinsic() const {
  return intrinsic_id() == vmIntrinsics::_Continuation_enterSpecial;
}

inline bool Method::is_continuation_yield_intrinsic() const {
  return intrinsic_id() == vmIntrinsics::_Continuation_doYield;
}

inline bool Method::is_continuation_native_intrinsic() const {
  return intrinsic_id() == vmIntrinsics::_Continuation_enterSpecial ||
         intrinsic_id() == vmIntrinsics::_Continuation_doYield;
}

inline bool Method::is_special_native_intrinsic() const {
  return is_method_handle_intrinsic() || is_continuation_native_intrinsic();
}

#endif // SHARE_OOPS_METHOD_INLINE_HPP
