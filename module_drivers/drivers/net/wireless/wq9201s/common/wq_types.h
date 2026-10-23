#ifndef WQ_TYPES_H_
#define WQ_TYPES_H_

#undef __WQ_OS_LINUX__		/* Linux */
#undef __WQ_OS_WIN__		/* Windows */
#undef __WQ_OS_RT__		/* Real-time OS (Firmware) */

#undef __WQ_USR_SPACE__		/* user space */
#undef __WQ_DRV_SPACE__		/* kernel space */
#undef __WQ_FW_SPACE__		/* firmware space */

#if defined(__linux__) || defined(__KERNEL__)
#define __WQ_OS_LINUX__
#elif defined(_WIN32) || defined (_WIN64) || defined (_KERNEL_MODE)
#define __WQ_OS_WIN__
#else
#define __WQ_OS_RT__
#endif

#if defined(__KERNEL__) || defined(_KERNEL_MODE)

#define __WQ_DRV_SPACE__

#include <linux/types.h>

#elif defined(__linux__) || defined(__WQ_OS_WIN__)

#define __WQ_USR_SPACE__

#include <linux/types.h>

#else /* FW */

#define __WQ_FW_SPACE__

#include <stdint.h>

#ifndef BIT
#define BIT(x) (1UL << (x))
#endif

#ifndef BIT_ULL
#define BIT_ULL(nr)		(1ULL << (nr))
#endif

/*
 * anyway, the following linux defined types are still preferred in firmware.
 */
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* strongly recommend: don't use bool_l for any new API. */
/* typedef u8 bool_l; */

#ifndef __bitwise
#define __bitwise
#endif

typedef u16 __bitwise __le16;
typedef u16 __bitwise __be16;
typedef u32 __bitwise __le32;
typedef u32 __bitwise __be32;
typedef u64 __bitwise __le64;
typedef u64 __bitwise __be64;

#endif

#if defined(__WQ_DRV_SPACE__) || defined(__WQ_USR_SPACE__)

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
#include <linux/bits.h>
#else
#include <linux/bitops.h>
#endif

#endif

typedef uint32_t addr32;	/* 32bit address */

#endif /* WQ_TYPES_H_ */
