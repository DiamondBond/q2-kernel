#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>

#define EFUSE_BASE		0xB3540000
#define EFUSE_CTRL		EFUSE_BASE + 0x0
#define EFUSE_CFG		EFUSE_BASE + 0x4
#define EFUSE_STAT		EFUSE_BASE + 0x8
#define EFUSE_DATA(n)		(EFUSE_BASE + 0xC + (n) * 4)
#define EFUSE_CTRL_ADDR_POS     (21)
#define EFUSE_CTRL_LEN_POS      (16)
#define EFUSE_CTRL_PD           (1 << 8)
#define EFUSE_CTRL_RDEN         (1 << 0)
#define EFUSE_CFG_RD_ADJ_POS    (24)
#define EFUSE_CFG_RD_STROBE_POS (16)
#define EFUSE_STAT_RDDONE       (1 << 0)

#define CHIPID_WORD_ADDR	0x0
#define CHIPID_BYTE_ADDR        0x0
#define SOCINFO_WORD_ADDR       0x1A
#define SOCINFO_BYTE_ADDR       0x6B
#define SOCINFO_BITS            (32 + 8)
#define CHIPID_BITS		(128 + 8)

#define REG32(addr)		*(volatile unsigned int *)(addr)
#define WORD_ALIGNED(_val)      ((_val) & ~(sizeof(int) - 1))
#define BITS_TO_WORD(_bits)     (((_bits) + 31) / 32)

enum soc_type {
	SOC_M300  = 0x2,
	SOC_M300P = 0x6,
	SOC_M210  = 0x7,
};

struct soc_desc {
	const enum soc_type soc;
	const char *chip;
};

static const struct soc_desc desc[] = {
	{SOC_M300,  "M300" },
	{SOC_M300P, "M300P"},
	{SOC_M210,  "M210" },
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
	for (i = 0; i < 8; i++)
		REG32(EFUSE_DATA(i)) = 0;

	REG32(EFUSE_CFG) = (0x1 << EFUSE_CFG_RD_ADJ_POS) | (0x3 << EFUSE_CFG_RD_STROBE_POS);
	REG32(EFUSE_CTRL) = (word_addr << EFUSE_CTRL_ADDR_POS) | ((word_num - 1) << EFUSE_CTRL_LEN_POS);
	REG32(EFUSE_CTRL) |= EFUSE_CTRL_RDEN;

	while (!(REG32(EFUSE_STAT) & EFUSE_STAT_RDDONE));
	REG32(EFUSE_CTRL) = EFUSE_CTRL_PD;

	for (i = 0; i < word_num && bit_pos < bits; i++) {
		data = REG32(EFUSE_DATA(i));

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

void __init match_soc_type(char *socid, char *soctype)
{
	int i, data = 0;
	enum soc_type soc;
	char buf[SOCINFO_BITS / 4 / 2 + 1];
	size_t len = strlen(socid);

	if (!socid || !soctype || len != SOCINFO_BITS / 4) {
		pr_err("Invalid input parameters\n");
		goto unknown;
	}

	snprintf(buf, sizeof(buf), "%.*s", len / 2, socid);
	if (kstrtoul(buf, 16, (unsigned long *)&data)) {
		pr_err("Failed to convert string\n");
		goto unknown;
	}

	if (!data || strncmp(socid, socid + len / 2, len / 2) != 0){
		pr_err("invalid soc id %x\n",data);
		goto unknown;
	}

	soc = data >> 6 & 0x1f;

	for (i = 0; i < ARRAY_SIZE(desc); i++) {
		if (desc[i].soc == soc) {
			strscpy(soctype, desc[i].chip, strlen(desc[i].chip) + 1);
			printk("SOC: %s\n", desc[i].chip);
			return;
		}
	}

unknown:
	strscpy(soctype, "UNKNOWN", sizeof("UNKNOWN"));
	printk("SOC: UNKNOWN\n");
}

