#pragma once

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 201112L)
    #error "C11 or later compiler is required"
#endif

#define _X(x) x

#define ep_likely(x)   __builtin_expect(!!(x), 1)
#define ep_unlikely(x) __builtin_expect(!!(x), 0)
#define ep_noreturn    _Noreturn

ep_noreturn void _ep_abort(const char* cond, const char* file, unsigned int line, const char* func);

#define ep_static_assert(_cond_) _Static_assert(_cond_, #_cond_)
#define ep_verify(_cond_) (ep_likely(_cond_) ? (void)0 : _ep_abort(#_cond_, __FILE__, __LINE__, __func__))

#ifdef EP_DEBUG
#include <stdio.h>
#define ep_assert(_cond_) ep_verify(_cond_)
#define ep_trace(fmt, ...) fprintf(stderr, "%s:%u: " fmt "\n", __FILE__, __LINE__, ## __VA_ARGS__)
#else
#define ep_assert(_cond_)
#define ep_trace(...)
#endif

static inline void* ep_alloc(size_t size)
{
    return malloc(size);
}

static inline void ep_free(void* p)
{
    free(p);
}

static inline void* ep_zalloc(size_t size)
{
    return calloc(size, 1);
}
