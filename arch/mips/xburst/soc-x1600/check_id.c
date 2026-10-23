#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>

#define EFUSE_BASE		0xB3540000
#define EFUSE_CTRL		EFUSE_BASE + 0x0
#define EFUSE_CFG		EFUSE_BASE + 0x4
#define EFUSE_STAT		EFUSE_BASE + 0x8
#define EFUSE_DATA(n)		(EFUSE_BASE + 0xC + (n) * 4)

#define EFUSE_CTRL_ADDR_POS	(21)
#define EFUSE_CTRL_LEN_POS	(16)
#define EFUSE_CTRL_RDEN         (1 << 0)
#define EFUSE_STAT_RDDONE       (1 << 0)
#define EFUSE_CFG_RD_ADJ_POS	(20)
#define EFUSE_CFG_RD_STROBE_POS	(16)

#define SOCINFO_BITS            (16)
#define CHIPID_BITS		(128)

#define CHIPID_ADDR	        0x0
#define SOCINFO_ADDR	        0x2B
#define REG32(addr)		*(volatile unsigned int *)(addr)
#define BITS_TO_WORD(_bits)     (((_bits) + 31) / 32)
#define BITS_TO_BYTE(_bits)     (((_bits) + 7) / 8)

struct soc_desc {
	const char *socid;
	const char *chip;
};

static const struct soc_desc desc[] = {
	{"1b00", "X1600"  },
	{"0a00", "X1600E" },
	{"4b00", "X1600N" },
	{"4a00", "X1600EN"},
	{"4900", "X1600HN"},
	{"6500", "X1600MN"},
	{"1b01", "X1660"  },
	{"4600", "X1660L" },
};

static  void __init efuse_read_data(int addr, int bits, char *buf)
{
	int byte_num = BITS_TO_BYTE(bits);
	int word_num = BITS_TO_WORD(bits);
	int i, j, bit_pos = 0, take_bits = 0;
	unsigned int data = 0;

	if (!buf) {
		printk(KERN_ERR "Buffer pointer is NULL\n");
		return;
	}

	memset(buf, 0, (bits / 4 + 1));

	REG32(0xb0000020) &= ~(1 << 1);
	for(i = 0; i < 8; i++)
		REG32(EFUSE_DATA(i)) = 0;

	REG32(EFUSE_CFG) = 0x2 << EFUSE_CFG_RD_ADJ_POS | 0x5 << EFUSE_CFG_RD_STROBE_POS;
	REG32(EFUSE_CTRL) = addr << EFUSE_CTRL_ADDR_POS | ((byte_num - 1) << EFUSE_CTRL_LEN_POS) ;
	REG32(EFUSE_CTRL) |= EFUSE_CTRL_RDEN;

	while(!(REG32(EFUSE_STAT) & EFUSE_STAT_RDDONE));

	for (i = 0; i < word_num && bit_pos < bits; i++) {
		data = REG32(EFUSE_DATA(i));

		take_bits = min(32, bits - bit_pos);

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
	efuse_read_data(SOCINFO_ADDR, SOCINFO_BITS, buf);
}

void __init efuse_read_chipid(char *buf)
{
	efuse_read_data(CHIPID_ADDR, CHIPID_BITS, buf);
}

void __init match_soc_type(char *socid, char *soctype)
{
	int i;

	if (!soctype || strlen(socid) != SOCINFO_BITS / 4) {
		pr_err("Invalid input parameters\n");
		goto unknown;
	}

	for (i = 0; i < ARRAY_SIZE(desc); i++) {
		if (strcmp(socid, desc[i].socid) == 0) {
			strscpy(soctype, desc[i].chip, strlen(desc[i].chip) + 1);
			printk("SOC: %s\n", desc[i].chip);
			return;
		}
	}

unknown:
	strscpy(soctype, "UNKNOWN", sizeof("UNKNOWN"));
	printk("SOC: UNKNOWN\n");
}

