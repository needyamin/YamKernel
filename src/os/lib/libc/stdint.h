#ifndef _LIBC_STDINT_H
#define _LIBC_STDINT_H

#include <nexus/types.h>

typedef i8 int8_t;
typedef i16 int16_t;
typedef i32 int32_t;
typedef i64 int64_t;
typedef u8 uint8_t;
typedef u16 uint16_t;
typedef u32 uint32_t;
typedef u64 uint64_t;

typedef i8 int_least8_t;
typedef i16 int_least16_t;
typedef i32 int_least32_t;
typedef i64 int_least64_t;
typedef u8 uint_least8_t;
typedef u16 uint_least16_t;
typedef u32 uint_least32_t;
typedef u64 uint_least64_t;

typedef i8 int_fast8_t;
typedef i64 int_fast16_t;
typedef i64 int_fast32_t;
typedef i64 int_fast64_t;
typedef u8 uint_fast8_t;
typedef u64 uint_fast16_t;
typedef u64 uint_fast32_t;
typedef u64 uint_fast64_t;

typedef i64 intmax_t;
typedef u64 uintmax_t;

#define INT8_MIN   (-128)
#define INT16_MIN  (-32767-1)
#define INT32_MIN  (-2147483647-1)
#define INT64_MIN  (-9223372036854775807L-1)
#define INT8_MAX   127
#define INT16_MAX  32767
#define INT32_MAX  2147483647
#define INT64_MAX  9223372036854775807L
#define UINT8_MAX  255U
#define UINT16_MAX 65535U
#define UINT32_MAX 4294967295U
#define UINT64_MAX 18446744073709551615UL
#define SIZE_MAX UINT64_MAX
#define SSIZE_MAX INT64_MAX
#define PTRDIFF_MIN INT64_MIN
#define PTRDIFF_MAX INT64_MAX

#define INTPTR_MIN INT64_MIN
#define INTPTR_MAX INT64_MAX
#define UINTPTR_MAX UINT64_MAX
#define INTMAX_MIN INT64_MIN
#define INTMAX_MAX INT64_MAX
#define UINTMAX_MAX UINT64_MAX

#endif
