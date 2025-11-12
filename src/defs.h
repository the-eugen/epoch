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
#define ep_verify(_cond_) ((_cond_) ? (void)0 : _ep_abort(#_cond_, __FILE__, __LINE__, __func__))

#ifdef EP_DEBUG

#define ep_assert(_cond_) ep_verify(_cond_)

#else

#define ep_assert(_cond_)

#endif
