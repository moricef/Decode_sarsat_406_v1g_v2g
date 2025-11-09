/*
 * Minimal tmwtypes.h for standalone MATLAB Coder compilation
 * This file provides basic type definitions normally provided by MATLAB
 */

#ifndef TMWTYPES_H
#define TMWTYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

/*=======================================================================*
 * Target hardware information
 *=======================================================================*/
#ifndef TMW_NAME_LENGTH_MAX
#define TMW_NAME_LENGTH_MAX 64
#endif

/*=======================================================================*
 * Fixed width word size data types
 *=======================================================================*/
typedef signed char int8_T;
typedef unsigned char uint8_T;
typedef short int16_T;
typedef unsigned short uint16_T;
typedef int int32_T;
typedef unsigned int uint32_T;

#if defined(__LP64__) || defined(_WIN64)
typedef long int64_T;
typedef unsigned long uint64_T;
#else
typedef long long int64_T;
typedef unsigned long long uint64_T;
#endif

/*=======================================================================*
 * Generic type definitions
 *=======================================================================*/
typedef double real_T;
typedef float real32_T;
typedef int8_T int_T;
typedef uint8_T uint_T;
typedef uint32_T ulong_T;
typedef char char_T;
typedef unsigned char uchar_T;
typedef char_T byte_T;
typedef unsigned char boolean_T;

/*=======================================================================*
 * Complex number structure
 *=======================================================================*/
typedef struct {
    real_T re;
    real_T im;
} creal_T;

typedef struct {
    real32_T re;
    real32_T im;
} creal32_T;

typedef struct {
    int8_T re;
    int8_T im;
} cint8_T;

typedef struct {
    uint8_T re;
    uint8_T im;
} cuint8_T;

typedef struct {
    int16_T re;
    int16_T im;
} cint16_T;

typedef struct {
    uint16_T re;
    uint16_T im;
} cuint16_T;

typedef struct {
    int32_T re;
    int32_T im;
} cint32_T;

typedef struct {
    uint32_T re;
    uint32_T im;
} cuint32_T;

typedef struct {
    int64_T re;
    int64_T im;
} cint64_T;

typedef struct {
    uint64_T re;
    uint64_T im;
} cuint64_T;

/*=======================================================================*
 * Definitions for function inlining
 *=======================================================================*/
#ifndef INLINE
# if defined(__GNUC__) && !defined(__STRICT_ANSI__)
#  define INLINE __inline__
# else
#  define INLINE
# endif
#endif

/*=======================================================================*
 * MIN/MAX macros
 *=======================================================================*/
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

/*=======================================================================*
 * Maximum values for fixed-width types
 *=======================================================================*/
#define MAX_int8_T   ((int8_T)(127))
#define MIN_int8_T   ((int8_T)(-128))
#define MAX_uint8_T  ((uint8_T)(255U))
#define MAX_int16_T  ((int16_T)(32767))
#define MIN_int16_T  ((int16_T)(-32768))
#define MAX_uint16_T ((uint16_T)(65535U))
#define MAX_int32_T  ((int32_T)(2147483647))
#define MIN_int32_T  ((int32_T)(-2147483647-1))
#define MAX_uint32_T ((uint32_T)(0xFFFFFFFFU))

#if defined(__LP64__) || defined(_WIN64)
#define MAX_int64_T  ((int64_T)(9223372036854775807L))
#define MIN_int64_T  ((int64_T)(-9223372036854775807L-1L))
#define MAX_uint64_T ((uint64_T)(0xFFFFFFFFFFFFFFFFUL))
#else
#define MAX_int64_T  ((int64_T)(9223372036854775807LL))
#define MIN_int64_T  ((int64_T)(-9223372036854775807LL-1LL))
#define MAX_uint64_T ((uint64_T)(0xFFFFFFFFFFFFFFFFULL))
#endif

#endif /* TMWTYPES_H */
