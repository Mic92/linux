#ifndef _ASM_LKL_UACCESS_H
#define _ASM_LKL_UACCESS_H

/* copied from old include/asm-generic/uaccess.h */
static inline __must_check long raw_copy_from_user(void *to,
		const void __user *from, unsigned long n)
{
	if (__builtin_constant_p(n)) {
		switch (n) {
		case 1:
			*(u8 *)to = *(u8 __force *)from;
			return 0;
		case 2:
			*(u16 *)to = *(u16 __force *)from;
			return 0;
		case 4:
			*(u32 *)to = *(u32 __force *)from;
			return 0;
#ifdef CONFIG_64BIT
		case 8:
			*(u64 *)to = *(u64 __force *)from;
			return 0;
#endif
		default:
			break;
		}
	}

	memcpy(to, (const void __force *)from, n);
	return 0;
}

static inline __must_check long raw_copy_to_user(void __user *to,
		const void *from, unsigned long n)
{
	if (__builtin_constant_p(n)) {
		switch (n) {
		case 1:
			*(u8 __force *)to = *(u8 *)from;
			return 0;
		case 2:
			*(u16 __force *)to = *(u16 *)from;
			return 0;
		case 4:
			*(u32 __force *)to = *(u32 *)from;
			return 0;
#ifdef CONFIG_64BIT
		case 8:
			*(u64 __force *)to = *(u64 *)from;
			return 0;
#endif
		default:
			break;
		}
	}

	memcpy((void __force *)to, from, n);
	return 0;
}

extern unsigned long sgxlkl_heap_start;
extern unsigned long sgxlkl_heap_end;
static inline int __access_ok(unsigned long addr, unsigned long size)
{
    // FIXME: Figure out the upper bound. Hint: the binary is mapped beyond sgxlkl_heap_end
    //return !sgxlkl_heap_start || !sgxlkl_heap_end || (sgxlkl_heap_start <= addr && addr < sgxlkl_heap_end);
    //return !sgxlkl_heap_start || sgxlkl_heap_start <= addr;
    // since we use kernel modules this no longer works
    return 1;
}

#define __access_ok __access_ok
#include <asm-generic/uaccess.h>

#endif
