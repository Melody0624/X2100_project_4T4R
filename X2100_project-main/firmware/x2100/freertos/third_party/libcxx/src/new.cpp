//===--------------------------- new.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdlib.h>

#include "new"
#include "include/atomic_support.h"

#if defined(_LIBCPP_ABI_MICROSOFT)
#   if !defined(_LIBCPP_ABI_VCRUNTIME)
#       include "support/runtime/new_handler_fallback.ipp"
#   endif
#elif defined(LIBCXX_BUILDING_LIBCXXABI)
#   include <cxxabi.h>
#elif defined(LIBCXXRT)
#   include <cxxabi.h>
#   include "support/runtime/new_handler_fallback.ipp"
#elif defined(__GLIBCXX__)
    // nothing to do
#else
#   include "support/runtime/new_handler_fallback.ipp"
#endif

namespace std
{

#ifndef __GLIBCXX__
const nothrow_t nothrow{};
#endif

#ifndef LIBSTDCXX

void
__throw_bad_alloc()
{
#ifndef _LIBCPP_NO_EXCEPTIONS
    throw bad_alloc();
#else
    _VSTD::abort();
#endif
}

#endif // !LIBSTDCXX

}  // std

void* operator new(size_t blockSize) noexcept
{
	return malloc(blockSize);
}

void* operator new[](size_t blockSize)noexcept
{
	return malloc(blockSize);
}

void operator delete(void* ptr)noexcept
{
	return free(ptr);
}

void operator delete[](void* ptr)noexcept
{
	return free(ptr);
}

void operator delete(void* ptr, unsigned int size)noexcept
{
	return free(ptr);
}

void operator delete[](void* ptr, unsigned int size)noexcept
{
	return free(ptr);
}

void* operator new(size_t blockSize, const std::nothrow_t&)noexcept
{
	return malloc(blockSize);
}

void* operator new[](size_t blockSize, const std::nothrow_t&)noexcept
{
	return malloc(blockSize);
}

void operator delete(void* ptr, const std::nothrow_t&)noexcept
{
	free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&)noexcept
{
	free(ptr);
}

