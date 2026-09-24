#include <stdlib.h>
#include <new>

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

