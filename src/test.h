#pragma once

#ifdef EP_CONFIG_TEST

typedef void(*ep_test_fptr)(void);

struct ep_test_entry
{
    const char* name;
    ep_test_fptr runner;
};

#define ep_test_decl(_func_) \
    static void _func_(void); \
    static struct ep_test_entry _func_ ##_entry __attribute__((__section__(".ep_test"))) __attribute__((used)) = { \
        .name = #_func_, \
        .runner = _func_, \
    };

#define ep_test(_func_) \
    ep_test_decl(_func_); \
    __attribute__((unused)) static void _func_(void)

extern struct ep_test_entry _ep_test_start[];
extern struct ep_test_entry _ep_test_end[];

extern ep_noreturn void _ep_test_fail(const char* fmt, ...);

#define _ep_apply_fmt_spec(_fmtspec_, _val_, ...) \
    _Generic((_val_), \
        char:               _fmtspec_("%c", __VA_ARGS__), \
        signed char:        _fmtspec_("%hhd", __VA_ARGS__), \
        unsigned char:      _fmtspec_("%hhu", __VA_ARGS__), \
        short:              _fmtspec_("%hd", __VA_ARGS__), \
        unsigned short:     _fmtspec_("%hu", __VA_ARGS__), \
        int:                _fmtspec_("%d", __VA_ARGS__), \
        unsigned int:       _fmtspec_("%u", __VA_ARGS__), \
        long:               _fmtspec_("%ld", __VA_ARGS__), \
        unsigned long:      _fmtspec_("%lu", __VA_ARGS__), \
        long long:          _fmtspec_("%lld", __VA_ARGS__), \
        unsigned long long: _fmtspec_("%llu", __VA_ARGS__), \
        float:              _fmtspec_("%f", __VA_ARGS__), \
        double:             _fmtspec_("%f", __VA_ARGS__), \
        long double:        _fmtspec_("%Lf", __VA_ARGS__), \
        default:            _fmtspec_("%p", __VA_ARGS__))

#define ep_verify_equal_fmt(_spec_, _lhv_, _rhv_) \
    "ep_verify_equal(" _lhv_ ", " _rhv_ ") failed: " _spec_ " != " _spec_ "\n"

#define ep_verify_equal(_lhv_, _rhv_) \
    do { \
        __typeof__(_lhv_) lval = (_lhv_); \
        __typeof__(_rhv_) rval = (_rhv_); \
        \
        if(lval != rval) { \
            _ep_test_fail( \
                _ep_apply_fmt_spec(ep_verify_equal_fmt, lval, #_lhv_, #_rhv_), \
                lval, \
                rval); \
        } \
    } while (0)

#define ep_verify_not_equal_fmt(_spec_, _lhv_, _rhv_) \
    "ep_verify_not_equal(" _lhv_ ", " _rhv_ ") failed: " _spec_ " == " _spec_ "\n"

#define ep_verify_not_equal(_lhv_, _rhv_) \
    do { \
        __typeof__(_lhv_) lval = (_lhv_); \
        __typeof__(_rhv_) rval = (_rhv_); \
        \
        if(lval == rval) { \
            _ep_test_fail( \
                _ep_apply_fmt_spec(ep_verify_not_equal_fmt, lval, #_lhv_, #_rhv_), \
                lval, \
                rval); \
        } \
    } while (0)

#else

#define ep_test(_func_)
#define ep_test_decl(_func_)
#define ep_verify_equal(_lhv_, _rhv_)
#define ep_verify_not_equal(_lhv_, _rhv_)

#endif
