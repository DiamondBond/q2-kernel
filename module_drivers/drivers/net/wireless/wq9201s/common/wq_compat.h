#ifndef WQ_COMPAT_H_
#define WQ_COMPAT_H_

#include "wq_types.h"

#ifdef __WQ_OS_LINUX__
#include <linux/compiler.h>
#endif

#ifndef __has_attribute
#define __has_attribute(x)		0
#endif

/* earlier version of linux/compiler_attributes.h may not define the following */
#ifndef fallthrough
#if __has_attribute(__fallthrough__)
# define fallthrough			__attribute__((__fallthrough__))
#else
# define fallthrough			do {} while (0)  /* fallthrough */
#endif
#endif

#ifndef __maybe_unused
#define __maybe_unused			__attribute__((__unused__))
#endif

#ifndef __packed
#define __packed			__attribute__((__packed__))
#endif


/*
 * high frequently structures and constants related to ethernet
 */
#ifdef __WQ_OS_LINUX__
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#else

/*
 *	IEEE 802.3 Ethernet magic constants.  The frame sizes omit the preamble
 *	and FCS/CRC (frame check sequence).
 */

#define ETH_ALEN	6		/* Octets in one ethernet addr	 */
#define ETH_TLEN	2		/* Octets in ethernet type field */
#define ETH_HLEN	14		/* Total octets in header.	 */
#define ETH_ZLEN	60		/* Min. octets in frame sans FCS */
#define ETH_DATA_LEN	1500		/* Max. octets in payload	 */
#define ETH_FRAME_LEN	1514		/* Max. octets in frame sans FCS */
#define ETH_FCS_LEN	4		/* Octets in the FCS		 */

#define ETH_MIN_MTU	68		/* Min IPv4 MTU per RFC791	*/
#define ETH_MAX_MTU	0xFFFFU		/* 65535, same as IP_MAX_MTU	*/

#ifndef __UAPI_DEF_ETHHDR
#define __UAPI_DEF_ETHHDR		1
#endif

#if __UAPI_DEF_ETHHDR
struct ethhdr {
	unsigned char	h_dest[ETH_ALEN];	/* destination eth addr	*/
	unsigned char	h_source[ETH_ALEN];	/* source ether addr	*/
	__be16		h_proto;		/* packet type ID field	*/
} __packed;
#endif

#define MACADDR_FORMAT			"%02x:%02x:%02x:%02x:%02x:%02x"
#define MACADDR_ARGS(mac)		MACADDR_ARGS_U8((const u8*)mac)
#define MACADDR_ARGS_U8(u)		(u)[0], (u)[1], (u)[2], (u)[3], (u)[4], (u)[5]

/*
 * the following useful methods in linux/etherdevice.h is strongly recommended
 */

/**
 * ether_addr_copy - Copy an Ethernet address
 * @dst: Pointer to a six-byte array Ethernet address destination
 * @src: Pointer to a six-byte array Ethernet address source
 *
 * Please note: dst & src must both be aligned to u16.
 */
static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	*(u32 *)dst = *(const u32 *)src;
	*(u16 *)(dst + 4) = *(const u16 *)(src + 4);
#else
	u16 *a = (u16 *)dst;
	const u16 *b = (const u16 *)src;

	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];
#endif
}

/**
 * ether_addr_equal - Compare two Ethernet addresses
 * @addr1: Pointer to a six-byte array containing the Ethernet address
 * @addr2: Pointer other six-byte array containing the Ethernet address
 *
 * Compare two Ethernet addresses, returns true if equal
 *
 * Please note: addr1 & addr2 must both be aligned to u16.
 */
static inline bool ether_addr_equal(const u8 *addr1, const u8 *addr2)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	u32 fold = ((*(const u32 *)addr1) ^ (*(const u32 *)addr2)) |
		   ((*(const u16 *)(addr1 + 4)) ^ (*(const u16 *)(addr2 + 4)));

	return fold == 0;
#else
	const u16 *a = (const u16 *)addr1;
	const u16 *b = (const u16 *)addr2;

	return ((a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2])) == 0;
#endif
}

#endif

/*
 * it's preferred to directly replace "struct mac_addr xxx;" as "u8 xxx[ETH_ALEN];"
 */
struct mac_addr {
	u16 array[ETH_ALEN / sizeof(u16)];
} /* NB: don't add __attribute__((packed)), otherwise, mis-aligned exception happens */;

#endif /* WQ_COMPAT_H_ */
