#if defined __x86_64__ && !defined __ILP32__
#define __WORDSIZE 64
#else
#define __WORDSIZE 32
#endif

typedef signed char i8;
typedef unsigned char u8;
typedef signed short int i16;
typedef unsigned short int u16;
typedef signed int i32;
typedef unsigned int u32;
#if __WORDSIZE == 64
typedef signed long int i64;
typedef unsigned long int u64;
#else
__extension__ typedef signed long long int i64;
__extension__ typedef unsigned long long int u64;
#endif

/* Types for `void *' pointers.  */
#if __WORDSIZE == 64
/// Size of a signed pointer
typedef long int isize;
/// Size of a unsigned pointer
typedef unsigned long int usize;
#else
/// Size of a signed pointer
typedef int isize;
/// Size of a unsigned pointer
typedef unsigned int usize;
#endif

typedef float f32;
typedef double f64;
typedef long double f80;

typedef struct {
  u32 len;
  u8 *head;
} String;
