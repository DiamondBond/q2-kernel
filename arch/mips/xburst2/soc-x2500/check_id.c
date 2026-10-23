#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>

#define EFUSE_BASE		0xB3540000
#define EFUSE_CTRL		EFUSE_BASE + 0x0
#define EFUSE_CFG		EFUSE_BASE + 0x4
#define EFUSE_STAT		EFUSE_BASE + 0x8
#define EFUSE_DATA		(EFUSE_BASE + 0xC)
#define EFUSE_CTRL_ADDR_POS     (21)
#define EFUSE_CTRL_PD           (1 << 8)
#define EFUSE_CTRL_RDEN         (1 << 0)
#define EFUSE_CFG_RD_ADJ_POS    (23)
#define EFUSE_CFG_RD_STROBE_POS (18)
#define EFUSE_STAT_RDDONE       (1 << 0)

#define CHIPID_WORD_ADDR	0x0
#define CHIPID_BYTE_ADDR	0x0
#define SOCINFO_WORD_ADDR	0xa
#define SOCINFO_BYTE_ADDR	0x28
#define VERSION_WORD_ADDR       0x14
#define VERSION_BYTE_ADDR       0x52
#define SOCINFO_BITS            (16)
#define CHIPID_BITS		(96)
#define VERSION_BITS		(16)

#define REG32(addr)		*(volatile unsigned int *)(addr)
#define WORD_ALIGNED(_val)      ((_val) & ~(sizeof(int) - 1))
#define BITS_TO_WORD(_bits)     (((_bits) + 31) / 32)

struct soc_desc {
	const char *version;
	const char *chip;
};

static const struct soc_desc desc[] = {
	{"8888", "X2500"},
	{"9999", "X2500A"},
};

static void __init efuse_read_data(int word_addr, int byte_addr, int bits, char *buf)
{
	int word_num = BITS_TO_WORD(bits);
	int offset = (byte_addr - WORD_ALIGNED(byte_addr)) * 8;
	int i, j, bit_pos = 0, take_bits = 0;
	unsigned int data = 0;

	if (!buf) {
		printk(KERN_ERR "Buffer pointer is NULL\n");
		return;
	}

	memset(buf, 0, (bits / 4 + 1));

	REG32(0xb0000020) &= ~(1 << 1);
	REG32(EFUSE_CFG) = 0x7 << EFUSE_CFG_RD_ADJ_POS | 0 << EFUSE_CFG_RD_STROBE_POS;

	for (i = 0; i < word_num && bit_pos < bits; i++) {
		REG32(EFUSE_STAT) = 0;
		REG32(EFUSE_CTRL) = (word_addr << EFUSE_CTRL_ADDR_POS);
		REG32(EFUSE_CTRL) |= EFUSE_CTRL_RDEN;
		while (!(REG32(EFUSE_STAT) & EFUSE_STAT_RDDONE));
		data = REG32(EFUSE_DATA);

		if (i == 0 && offset > 0) {
			data >>= offset;
			take_bits = min(32 - offset, bits - bit_pos);
		} else {
			take_bits = min(32, bits - bit_pos);
		}

		for (j = 0; j < take_bits; j += 4) {
			int nibble = (data >> j) & 0xF;
			char hex_char = (nibble < 10) ? ('0' + nibble) : ('a' + (nibble - 10));
			buf[(bits / 4) - 1 - (bit_pos + j) / 4] = hex_char;
		}
		bit_pos += take_bits;
		word_addr++;
	}

	buf[bits / 4] = '\0';
}

void __init efuse_read_socid(char *buf)
{
	efuse_read_data(SOCINFO_WORD_ADDR, SOCINFO_BYTE_ADDR, SOCINFO_BITS, buf);
}

void __init efuse_read_chipid(char *buf)
{
	efuse_read_data(CHIPID_WORD_ADDR, CHIPID_BYTE_ADDR, CHIPID_BITS, buf);
}

void __init match_soc_type(char *soctype)
{
	int i;
	char version[VERSION_BITS / 4 + 1];

	if (!soctype) {
		pr_err("Invalid input parameters\n");
		goto unknown;
	}

	efuse_read_data(VERSION_WORD_ADDR, VERSION_BYTE_ADDR, VERSION_BITS, version);

	if (strlen(version) != VERSION_BITS / 4) {
		pr_err("Invalid version length\n");
		goto unknown;
	}

	for (i = 0; i < ARRAY_SIZE(desc); i++) {
		if (strcmp(version, desc[i].version) == 0) {
			strscpy(soctype, desc[i].chip, strlen(desc[i].chip) + 1);
			printk("SOC: %s\n", desc[i].chip);
			return;
		}
	}

unknown:
	strscpy(soctype, "UNKNOWN", sizeof("UNKNOWN"));
	printk("SOC: UNKNOWN\n");
}

