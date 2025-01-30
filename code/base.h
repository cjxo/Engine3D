#if !defined(MY_BASE_H)
#define MY_BASE_H

#include <stdint.h>

typedef int8_t    s8;
typedef uint8_t   u8;
typedef int16_t  s16;
typedef uint16_t u16;
typedef int32_t  s32;
typedef uint32_t u32;
typedef int64_t  s64;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;
typedef s32      b32;

#define AssertBreak() __debugbreak()
#define Assert(cond) do{if(!(cond)){AssertBreak();}}while(0)
#define AssertTrue(x) Assert((!!(x))==true)
#define AssertHR(hr) Assert(SUCCEEDED(hr))
#define InvalidCodePath() AssertBreak()
#define ArrayCount(a) (sizeof(a)/sizeof((a)[0]))
#define true 1
#define false 0

#define Minimum(a,b) ((a)<(b))?(a):(b)
#define AlignAToB(a,b) (((a)+((b)-1))&(~((b)-1)))
#define KB(v) (1024llu*((u64)v))
#define MB(v) (1024llu*KB(v))
#define GB(v) (1024llu*MB(v))

#endif
