#ifndef _LIBC_MATH_H
#define _LIBC_MATH_H

#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4

#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf_sign(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)

static inline double fabs(double x) { return x < 0 ? -x : x; }
static inline float fabsf(float x) { return x < 0 ? -x : x; }
static inline double copysign(double x, double y) { return __builtin_copysign(x, y); }
static inline float copysignf(float x, float y) { return __builtin_copysignf(x, y); }
static inline double floor(double x) {
    long i = (long)x;
    return (double)(i - (x < (double)i));
}
static inline double ceil(double x) {
    long i = (long)x;
    return (double)(i + (x > (double)i));
}
static inline double trunc(double x) { return (double)((long)x); }
static inline double frexp(double x, int *exp) {
    int e = 0;
    if (x == 0.0) {
        if (exp) *exp = 0;
        return 0.0;
    }
    double ax = x < 0 ? -x : x;
    while (ax >= 1.0) { ax *= 0.5; e++; }
    while (ax < 0.5) { ax *= 2.0; e--; }
    if (exp) *exp = e;
    return x < 0 ? -ax : ax;
}
static inline double round(double x) {
    return x >= 0.0 ? floor(x + 0.5) : ceil(x - 0.5);
}
static inline double modf(double x, double *iptr) {
    double i = trunc(x);
    if (iptr) *iptr = i;
    return x - i;
}
static inline double pow(double x, double y) {
    int e = (int)y;
    double r = 1.0;
    if (e < 0) {
        while (e++) r /= x;
    } else {
        while (e--) r *= x;
    }
    return r;
}
static inline double sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double r = x;
    for (int i = 0; i < 12; i++) r = 0.5 * (r + x / r);
    return r;
}
static inline double hypot(double x, double y) {
    return sqrt(x * x + y * y);
}
static inline double sin(double x) {
    while (x > 3.141592653589793) x -= 6.283185307179586;
    while (x < -3.141592653589793) x += 6.283185307179586;
    double x2 = x * x;
    return x * (1.0 - x2 / 6.0 + (x2 * x2) / 120.0 - (x2 * x2 * x2) / 5040.0);
}
static inline double cos(double x) {
    while (x > 3.141592653589793) x -= 6.283185307179586;
    while (x < -3.141592653589793) x += 6.283185307179586;
    double x2 = x * x;
    return 1.0 - x2 / 2.0 + (x2 * x2) / 24.0 - (x2 * x2 * x2) / 720.0;
}
static inline double atan2(double y, double x) {
    if (x > 0.0) return y / (x + 0.28 * y * y / (x < 0 ? -x : x));
    if (x < 0.0 && y >= 0.0) return 3.141592653589793 + y / (x - 0.28 * y * y / x);
    if (x < 0.0 && y < 0.0) return -3.141592653589793 + y / (x - 0.28 * y * y / x);
    if (y > 0.0) return 1.5707963267948966;
    if (y < 0.0) return -1.5707963267948966;
    return 0.0;
}
static inline double exp(double x) {
    double term = 1.0;
    double sum = 1.0;
    for (int i = 1; i < 20; i++) {
        term *= x / (double)i;
        sum += term;
    }
    return sum;
}
static inline double log(double x) {
    if (x <= 0.0) return -HUGE_VAL;
    double y = (x - 1.0) / (x + 1.0);
    double y2 = y * y;
    double term = y;
    double sum = 0.0;
    for (int n = 1; n < 40; n += 2) {
        sum += term / (double)n;
        term *= y2;
    }
    return 2.0 * sum;
}
static inline double fmod(double x, double y) {
    if (y == 0.0) return NAN;
    long q = (long)(x / y);
    return x - (double)q * y;
}
static inline double ldexp(double x, int expn) {
    while (expn > 0) { x *= 2.0; expn--; }
    while (expn < 0) { x *= 0.5; expn++; }
    return x;
}

#endif
