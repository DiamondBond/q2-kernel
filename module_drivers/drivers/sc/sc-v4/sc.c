#include <linux/module.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

#include "secall.h"
#include "jz_pdma.h"
#include "otp.h"
#include "sc.h"
#include "rsa.h"

/*
 * Compile-time switch for in-kernel SCBOOT self-test code/data.
 * Default is off to keep production builds minimal and avoid embedding test vectors.
 */
#ifndef SC_ENABLE_SELFTEST
#define SC_ENABLE_SELFTEST 0
#endif

#if SC_ENABLE_SELFTEST
#include "scboot_test_data.h"
#endif

/*
 * security decrypted.
 * 1. prepare a bin encrypted.
 *      |---------------|-------------------|                   |
 *      | SC KEY(2048)  |   CODE encrypted  |
 *      |---------------|-------------------|                   |
 *
 * */

#undef TCSM_CODE_ADDR
#undef TCSM_SC_KEY_ADDR
#undef MCU_TCSM_RETVAL
#undef MCU_TCSM_SECALL_MSG

#define TCSM_CODE_ADDR           (TCSM_BANK(1) + 0)
#define TCSM_SC_KEY_ADDR         (TCSM_BANK(1) + 2048)
#define MCU_TCSM_RETVAL          (TCSM_BANK(0) + 2048 + 1108) /* cal from sc_interface. */
#define MCU_TCSM_SECALL_MSG      (TCSM_BANK(0) + 2048 + 128)  /* MCU_TCSM_SECALL_MSG */

/* size per round must be align with 64,
 * it doesn't matter when more than 2048 or less */
#define SC_MAX_SIZE_PERROUND     (2048)
/* should not change, depends on the signature tool */
#define SC_SIGNATURE_SIZE        (2048)

#define SC_MAGIC_SIZE            (512)
#define SC_KEY_SIZE              (1536)

#define SC_MAGIC_TAG             0x54424353

#define SC_TRACE(fmt, ...) printk(KERN_ERR "SCDBG: " fmt "\n", ##__VA_ARGS__)

/* rsa key */
#define NKU_NKEY_WORD_OFF        2
#define NKU_KUKEY_WORD_OFF       (2+64)
#define NKU_KEY_LEN              (64)

/* sc key */
#define SC_KEY_WORD_SIZE         (64 * 4)
#define SC_KEY_INFO_WORD_SIZE    (64)
#define SC_KEY_N_WORD_SIZE       (64)
#define SC_KEY_KU_WORD_SIZE      (64)
#define SC_KEY_CODESIG_WORD_SIZE (64)
#define SC_KEY_INFO_WORD_OFF     (0)
#define SC_KEY_N_WORD_OFF        (SC_KEY_INFO_WORD_OFF + SC_KEY_INFO_WORD_SIZE)
#define SC_KEY_KU_WORD_OFF       (SC_KEY_N_WORD_OFF + SC_KEY_N_WORD_SIZE)
#define SC_KEY_CODESIG_WORD_OFF  (SC_KEY_KU_WORD_OFF + SC_KEY_KU_WORD_SIZE)

struct sckey {
    unsigned int code_len;
    unsigned int code_encrypt;
    unsigned int kn_bit;
    unsigned int ku_bit;
    unsigned int key_encrypt;
    unsigned int pad_len;
    unsigned int is_ckey_aes;
    unsigned int is_burn_uboot;
    unsigned int reserved_0[56];
};

struct sc_info {
    unsigned char *src;
    unsigned char *dst;
    unsigned char *keybuf;
    int binlen;
    int newround;
    int endround;
    int is_open;
    int is_setup;
    int total_size;
    int dmamode;
    size_t buf_size;
    int stream_active;
    unsigned int stream_total_size;
    unsigned int stream_done_size;
    int secall_timedout;

    struct clk *ddr_clk;
    struct clk *aes_clk;
    struct clk *pdma_clk;
    struct clk *efuse_clk;
    struct clk *dtrng_clk;
    struct clk *hash_clk;

    struct mutex mutex;
};
static struct sc_info info;

static void sc_recover_after_timeout(void);

static void secure_check(void *addr, int *issig)
{
    int *ddrptr = (int *)(addr);
    int p = (unsigned long)SC_MAGIC_TAG;

    if (*ddrptr++ == p)
        *issig = 1;
}

static int setup_sckeys(void *addr, unsigned int *len)
{
    volatile struct sc_args *args = (volatile struct sc_args *)(MCU_TCSM_SECALL_MSG);
    unsigned int *tcsmptr = (unsigned int *)(TCSM_SC_KEY_ADDR);
    unsigned int *rsa_key = (unsigned int *)(TCSM_SC_KEY_ADDR + 1024);
    int *ddrptr = (int *)(addr + SC_MAGIC_SIZE);
    int iLoop = 0;
    unsigned int ret;

    /* parsing sc_key: info */
    for (iLoop = 0; iLoop < SC_KEY_INFO_WORD_SIZE; iLoop++)
        tcsmptr[SC_KEY_INFO_WORD_OFF + iLoop] = ddrptr[SC_KEY_INFO_WORD_OFF + iLoop];

    /* parsing sc_key: codesig; (Note:Soft RSA needs to switch word between big and small end !) */
    for (iLoop = 0; iLoop < SC_KEY_CODESIG_WORD_SIZE; iLoop++)
        tcsmptr[SC_KEY_CODESIG_WORD_OFF + iLoop] = ddrptr[SC_KEY_CODESIG_WORD_OFF + (SC_KEY_CODESIG_WORD_SIZE - 1) - iLoop];

    /* parsing sc_key: n + ku */
    rsa_key[0] = tcsmptr[2];
    rsa_key[1] = tcsmptr[3];

    for (iLoop = 0; iLoop < SC_KEY_N_WORD_SIZE; iLoop++)
        rsa_key[NKU_NKEY_WORD_OFF + iLoop] = ddrptr[SC_KEY_N_WORD_OFF + iLoop];

    for (iLoop = 0; iLoop < SC_KEY_KU_WORD_SIZE; iLoop++)
        rsa_key[NKU_KUKEY_WORD_OFF + iLoop] = ddrptr[SC_KEY_KU_WORD_OFF + iLoop];

    *len = tcsmptr[0]; /* len in spl structure */

    /* len must 4 wrod align */
    if((*len) == 0 || (*len) % 16)
        return -1;

    /*
     * security init:
     * 1.clear scram KEYDONE segment
     * 2.nku verify
     */
    args->arg[0] = tcsmptr[4];
    args->arg[1] = MCU_TCSM_PADDR(rsa_key);
    secall(args, SC_FUNC_INIT, 0, 1);
    ret = *(volatile unsigned int *)MCU_TCSM_RETVAL;
    ret &= 0xFFFF;
    SC_TRACE("sc_setup leave secall_ret=0x%x",ret);
    if (ret) {
	    info.secall_timedout = 1;
	    return -ETIMEDOUT;
    }

    /* rsa public decrtpt codesig: */
    {
        /* for rsa n; (Note:Soft RSA needs to switch word between big and small end !) */
        for (iLoop = 0; iLoop < SC_KEY_N_WORD_SIZE; iLoop++)
            tcsmptr[SC_KEY_N_WORD_OFF + iLoop] = rsa_key[NKU_NKEY_WORD_OFF + (NKU_KEY_LEN - 1) - iLoop];

        /* for rsa ku; (Note:Soft RSA needs to switch word between big and small end !) */
        tcsmptr[SC_KEY_KU_WORD_OFF] = rsa_key[NKU_KUKEY_WORD_OFF + (NKU_KEY_LEN - 1)];

        f_rsa_public_decrypt(tcsmptr + SC_KEY_CODESIG_WORD_OFF,
            tcsmptr + SC_KEY_CODESIG_WORD_OFF,
            SC_KEY_CODESIG_WORD_SIZE,
            tcsmptr + SC_KEY_N_WORD_OFF,
            tcsmptr + SC_KEY_KU_WORD_OFF,
            SC_KEY_N_WORD_SIZE);
    }

    info.dmamode = ((struct sckey *)ddrptr)->pad_len > 0 ? 1 : 0;
    printk(KERN_ERR "SC: decrypt mode=%s (pad_len=%u)\n",
           info.dmamode ? "dma" : "cpu",
           ((struct sckey *)ddrptr)->pad_len);

    return 0;
}

static int sc_decrypt(struct sc_info *p)
{
    struct sc_args *args = (struct sc_args *)(MCU_TCSM_SECALL_MSG);
    unsigned int ret;
    int *srcptr = (int *)(p->src);
    int *dstptr = (int *)(p->dst);
    int binlen = p->binlen;

    unsigned int *tcsmptr = (unsigned int *)(TCSM_CODE_ADDR);
    int iLoop = 0;
    int pos = 0;

    args->arg[0] = p->endround << 1 | p->newround;
    args->arg[1] = binlen;

    for (iLoop = 0; iLoop < binlen / 4; iLoop++)
        tcsmptr[iLoop] = srcptr[pos++];

    SC_TRACE("sc_decrypt enter pid=%d new=%d end=%d binlen=%d arg0=0x%x",
             current->pid, p->newround, p->endround, binlen, args->arg[0]);
    wmb();
    secall(args, SC_FUNC_SCBOOT, 0, 1);
    rmb();

    ret = *(volatile unsigned int *)MCU_TCSM_RETVAL;
    ret &= 0xFFFF;
    SC_TRACE("sc_decrypt leave secall_ret=0x%x",ret);
    if (ret) {
	    info.secall_timedout = 1;
	    return -ETIMEDOUT;
    }

    for (iLoop = 0; iLoop < binlen / 4; iLoop++)
        dstptr[pos - binlen / 4 + iLoop] = tcsmptr[iLoop];


    return ret;
}

static int sc_decrypt_dma(struct sc_info *p)
{
    struct sc_args *args = (struct sc_args *)(MCU_TCSM_SECALL_MSG);
    unsigned int ret;
    int *srcptr = (int *)(p->src);
    int *dstptr = (int *)(p->dst);
    int binlen = p->binlen;

    args->arg[0] = (p->newround & 0x1) | ((p->endround & 0x1) << 1) | (1 << 2); /* bit 0:newround bit 1:endround bit 2:dmamode */
    args->arg[1] = binlen;
    args->arg[2] = virt_to_phys(srcptr);

    SC_TRACE("sc_decrypt_dma enter pid=%d new=%d end=%d binlen=%d arg0=0x%x src_pa=0x%x",
             current->pid, p->newround, p->endround, binlen, args->arg[0], args->arg[2]);
    wmb();
    flush_cache_all();
    secall(args, SC_FUNC_SCBOOT, 0, 1);
    rmb();
    flush_cache_all();

    ret = *(volatile unsigned int *)MCU_TCSM_RETVAL;
    ret &= 0xFFFF;
    SC_TRACE("sc_decrypt leave secall_ret=0x%x",ret);
    if (ret) {
	    info.secall_timedout = 1;
	    return -ETIMEDOUT;
    }

    if (dstptr != srcptr) {
        if (dstptr < srcptr)
            memmove(dstptr, srcptr, binlen);
        else
            memcpy(dstptr, srcptr, binlen);
    }

    return ret;
}

static void sc_free_bufs(void)
{
    kfree(info.src);
    info.src = NULL;
    kfree(info.dst);
    info.dst = NULL;
    info.buf_size = 0;
}

static int sc_alloc_bufs(size_t size)
{
    if (info.buf_size >= size)
        return 0;

    sc_free_bufs();

    info.src = kmalloc(size, GFP_KERNEL);
    if (!info.src) {
        printk(KERN_ERR "SC: kmalloc src failed, size: %zu!!\n", size);
        return -ENOMEM;
    }

    info.dst = kmalloc(size, GFP_KERNEL);
    if (!info.dst) {
        printk(KERN_ERR "SC: kmalloc dst failed, size: %zu!!\n", size);
        kfree(info.src);
        info.src = NULL;
        return -ENOMEM;
    }

    info.buf_size = size;
    return 0;
}

static void sc_clean(void)
{
    struct sc_args *args = (struct sc_args *)(MCU_TCSM_SECALL_MSG);
    secall(args, SC_FUNC_INIT_SCRAM, 0, 1);

    info.is_setup = 0;
    info.newround = 0;
    info.endround = 0;
    info.stream_active = 0;
    info.stream_total_size = 0;
    info.stream_done_size = 0;
    info.secall_timedout = 0;
}

static void pdma_wait(void)
{
    __asm__ volatile (
        "    .set    push        \n\t"
        "    .set    noreorder   \n\t"
        "    .set    mips32      \n\t"
        "    li    $26, 0        \n\t"
        "    mtc0    $26, $12    \n\t"
        "    nop                 \n\t"
        "1:                      \n\t"
        "    wait                \n\t"
        "    b    1b             \n\t"
        "    nop                 \n\t"
        "    .set    reorder     \n\t"
        "    .set    pop         \n\t"
        );
}

static void sc_recover_after_timeout(void)
{
    int i;
    volatile unsigned int *pdma_bank0_off = (unsigned int *)TCSM_BANK0;
    unsigned int *pdma_ins = (unsigned int *)pdma_wait;

    printk(KERN_ERR "SC: secall timeout detected, reset MCU for recovery\n");
    reset_mcu();

    for (i = 0; i < 6; i++)
        pdma_bank0_off[i] = pdma_ins[i];

    boot_up_mcu();
    info.is_setup = 0;
    info.newround = 0;
    info.endround = 0;
    info.stream_active = 0;
    info.stream_total_size = 0;
    info.stream_done_size = 0;
    info.secall_timedout = 0;
}

static int sc_open(struct inode *inode, struct file *filp)
{
    int i;

    if (info.is_open) {
        printk(KERN_ERR "SC: it already be opened, please wait for closed\n");
        return -1;
    }

    reset_mcu();
    volatile unsigned int *pdma_bank0_off = (unsigned int *)TCSM_BANK0;
    unsigned int *pdma_ins = (unsigned int *)pdma_wait;

    for (i = 0; i < 6; i++)
        pdma_bank0_off[i] = pdma_ins[i];
    boot_up_mcu();

    info.is_open = 1;

    return 0;
}

static int sc_release(struct inode *inode, struct file *filp)
{
    sc_clean();
    sc_free_bufs();
    info.is_open = 0;
    reset_mcu();

    return 0;
}

#define CMD_sc_setup    _IOWR('s', 0, void *)
#define CMD_sc_decrypt  _IOWR('s', 1, void *)

#if SC_ENABLE_SELFTEST

#define CMD_sc_scboot_test _IO('s', 2)

/*
 * Self-test vector format:
 * - scboot_test_enc: [2048-byte security header][cipher payload]
 * - scboot_test_plain: plain payload only (no 2048-byte header)
 */
static int sc_scboot_selftest(void)
{
    const unsigned char *src = (const unsigned char *)scboot_test_enc + SC_SIGNATURE_SIZE;
    const unsigned char *expect = (const unsigned char *)scboot_test_plain;
    unsigned int size;
    unsigned int total_size;
    int ret;

    sc_clean();

    if (SCBOOT_TEST_ENC_BYTES <= SC_SIGNATURE_SIZE ||
        SCBOOT_TEST_PLAIN_BYTES == 0) {
        printk(KERN_ERR "SC: scboot selftest vectors too small enc=%u plain=%u\n",
               SCBOOT_TEST_ENC_BYTES, SCBOOT_TEST_PLAIN_BYTES);
        return -EINVAL;
    }

    ret = setup_sckeys((void *)scboot_test_enc, &total_size);
    if (ret) {
        printk(KERN_ERR "SC: scboot selftest setup failed, ret=0x%x\n", ret);
        return (ret == -ETIMEDOUT) ? -ETIMEDOUT : -EINVAL;
    }

    if (total_size == 0 || total_size > (SCBOOT_TEST_ENC_BYTES - SC_SIGNATURE_SIZE) ||
        total_size > SCBOOT_TEST_PLAIN_BYTES) {
        printk(KERN_ERR "SC: scboot selftest invalid total_size=%u enc_payload=%u plain_payload=%u\n",
               total_size,
               SCBOOT_TEST_ENC_BYTES - SC_SIGNATURE_SIZE,
               SCBOOT_TEST_PLAIN_BYTES);
        return -EINVAL;
    }

    ret = sc_alloc_bufs(SC_MAX_SIZE_PERROUND);
    if (ret)
        return ret;

    info.total_size = total_size;
    info.newround = 1;
    info.endround = 0;
    info.is_setup = 1;

    size = total_size;
    while (size > 0) {
        unsigned int len = size > SC_MAX_SIZE_PERROUND ? SC_MAX_SIZE_PERROUND : size;

        memcpy(info.src, src, len);

        if (size <= SC_MAX_SIZE_PERROUND)
            info.endround = 1;

        info.binlen = len;
        info.total_size -= info.binlen;

        ret = info.dmamode ? sc_decrypt_dma(&info) : sc_decrypt(&info);
        if (ret) {
            printk(KERN_ERR "SC: scboot selftest decrypt failed, ret=0x%x\n", ret);
            ret = (ret == -ETIMEDOUT) ? -ETIMEDOUT : -EIO;
            goto out;
        }

        if (memcmp(info.dst, expect, len)) {
            printk(KERN_ERR "SC: scboot selftest compare failed, len=%u\n", len);
            ret = -EIO;
            goto out;
        }

        src += len;
        expect += len;
        size -= len;
        info.newround = 0;
        info.endround = 0;
    }

    ret = 0;
out:
    if (ret && info.secall_timedout)
        sc_recover_after_timeout();

    info.is_setup = 0;
    return ret;
}
#endif /* SC_ENABLE_SELFTEST */

static long sc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret;

    switch (cmd) {
    case CMD_sc_setup: {
        unsigned long array[2];
        unsigned int __user *src;
        unsigned int __user *size;
        int issig = 0;

        if (copy_from_user(array, (void __user *)arg, sizeof(array)))
            return -EFAULT;

        src = (unsigned int __user *)array[0];
        size = (unsigned int __user *)array[1];

        mutex_lock(&info.mutex);

        sc_clean();

        if (copy_from_user(info.keybuf, src, SC_SIGNATURE_SIZE)) {
            printk(KERN_ERR "SC: copy_from_user err!!\n");
            ret = -EFAULT;
            goto out_setup;
        }

        secure_check(info.keybuf, &issig);
        if (EFUSTATE_UK_PRT == 0 || EFUSTATE_UK1_PRT == 0 || EFUSTATE_NKU_PRT == 0 || issig == 0) {
            printk(KERN_ERR "SC: error! please sign your file or burn key!!\n");
            ret = -EPERM;
            goto out_setup;
        }

        ret = setup_sckeys(info.keybuf, &info.total_size);
        if (ret) {
            printk(KERN_ERR "SC: error! please check image size, ret = 0x%x !!\n", ret);
            ret = (ret == -ETIMEDOUT) ? -ETIMEDOUT : -EINVAL;
            goto out_setup;
        }

        if (copy_to_user(size, &info.total_size, sizeof(int))) {
            printk(KERN_ERR "SC: copy_to_user err!!\n");
            ret = -EFAULT;
            goto out_setup;
        }

        info.newround = 1;
        info.is_setup = 1;

        ret = 0;

out_setup:
        if (ret && info.secall_timedout)
            sc_recover_after_timeout();
        mutex_unlock(&info.mutex);

        break;
    }

    case CMD_sc_decrypt: {
        unsigned long array[3];
        unsigned char __user *src;
        unsigned char __user *dst;
        unsigned int size;
        unsigned int chunk;

        if (copy_from_user(array, (void __user *)arg, sizeof(array)))
            return -EFAULT;

        src = (unsigned char __user *)array[0];
        dst = (unsigned char __user *)array[1];
        size = (unsigned int)array[2];

        mutex_lock(&info.mutex);

        if (!info.is_setup) {
            printk(KERN_ERR "SC: decrypt called before setup\n");
            ret = -EINVAL;
            goto out_decrypt;
        }

        if (size == 0 || size > (unsigned int)INT_MAX) {
            printk(KERN_ERR "SC: invalid decrypt size: %u\n", size);
            ret = -EINVAL;
            goto out_decrypt;
        }

        if (info.total_size <= 0 || size > (unsigned int)info.total_size) {
            printk(KERN_ERR "SC: decrypt size overflow: req=%u remain=%d\n",
                   size, info.total_size);
            ret = -EINVAL;
            goto out_decrypt;
        }

        chunk = info.dmamode ? size :
                (size > SC_MAX_SIZE_PERROUND ? SC_MAX_SIZE_PERROUND : size);

        ret = sc_alloc_bufs(chunk);
        if (ret) {
            printk(KERN_ERR "SC: alloc buf failed, size: %u!!\n", chunk);
            goto out_decrypt;
        }

        while (size > 0) {
            unsigned int len = info.dmamode ? size :
                               (size > SC_MAX_SIZE_PERROUND ? SC_MAX_SIZE_PERROUND : size);
            if (copy_from_user(info.src, src, len)) {
                printk(KERN_ERR "SC: copy_from_user err!\n");
                ret = -EFAULT;
                goto out_decrypt;
            }

            /* Clear stale flag first, then mark last round by remaining bytes. */
            info.endround = 0;
            if (info.total_size > 0 && (unsigned int)info.total_size <= len)
                info.endround = 1;

            SC_TRACE("decrypt loop remain=%d req=%u len=%u end=%d",
                     info.total_size, size, len, info.endround);

            info.binlen = len;
            info.total_size -= info.binlen;

            ret = info.dmamode ? sc_decrypt_dma(&info) : sc_decrypt(&info);
            if (ret) {
                printk(KERN_ERR "SC: error! please check your image, ret = 0x%x!!\n", ret);
                ret = (ret == -ETIMEDOUT) ? -ETIMEDOUT : -EIO;
                goto out_decrypt;
            }

            if (copy_to_user(dst, info.dst, len)) {
                printk(KERN_ERR "SC: copy_to_user err!!\n");
                ret = -EFAULT;
                goto out_decrypt;
            }

            src += len;
            dst += len;
            size -= len;
            if (info.is_open && info.is_setup && info.endround)
                info.is_setup = 0;
            info.newround = 0;
            info.endround = 0;
        }

        ret = 0;

out_decrypt:
        if (ret && info.secall_timedout)
            sc_recover_after_timeout();
        mutex_unlock(&info.mutex);
        break;
    }

#if SC_ENABLE_SELFTEST
    case CMD_sc_scboot_test:
        mutex_lock(&info.mutex);
        ret = sc_scboot_selftest();
        mutex_unlock(&info.mutex);
        break;
#endif

    default:
        printk(KERN_ERR "SC: do not support this cmd: %x\n", cmd);
        return -EINVAL;
    }

    return ret;
}

static struct file_operations sc_fops = {
    .owner          = THIS_MODULE,
    .open           = sc_open,
    .release        = sc_release,
    .unlocked_ioctl = sc_ioctl,
};

static struct miscdevice mdev = {
    .minor  = MISC_DYNAMIC_MINOR,
    .name   = "sc",
    .fops   = &sc_fops,
};

static int __init sc_init(void)
{
    int ret;

    info.pdma_clk = clk_get(NULL, "gate_pdma");
    BUG_ON(IS_ERR(info.pdma_clk));
    clk_prepare_enable(info.pdma_clk);

    info.efuse_clk = clk_get(NULL, "gate_efuse");
    BUG_ON(IS_ERR(info.efuse_clk));
    clk_prepare_enable(info.efuse_clk);

    info.dtrng_clk = clk_get(NULL, "gate_dtrng");
    BUG_ON(IS_ERR(info.dtrng_clk));
    clk_prepare_enable(info.dtrng_clk);

    info.hash_clk = clk_get(NULL, "gate_hash");
    BUG_ON(IS_ERR(info.hash_clk));
    clk_prepare_enable(info.hash_clk);

    info.aes_clk = clk_get(NULL, "gate_aes");
    BUG_ON(IS_ERR(info.aes_clk));
    clk_prepare_enable(info.aes_clk);

    ret = misc_register(&mdev);
    if (ret < 0) {
        printk(KERN_ERR "SC: %s register err!\n", mdev.name);
        goto err_misc;
    }

    mutex_init(&info.mutex);

    info.keybuf = kmalloc(SC_SIGNATURE_SIZE * sizeof(unsigned char), GFP_KERNEL);
    if (info.keybuf == NULL) {
        printk(KERN_ERR "apk proc wirte buf zalloc fail");
        ret = -ENOMEM;
        goto err_keybuf;
    }

    return 0;

err_keybuf:
    misc_deregister(&mdev);
err_misc:
    clk_disable_unprepare(info.aes_clk);
    clk_put(info.aes_clk);
    clk_disable_unprepare(info.hash_clk);
    clk_put(info.hash_clk);
    clk_disable_unprepare(info.dtrng_clk);
    clk_put(info.dtrng_clk);
    clk_disable_unprepare(info.efuse_clk);
    clk_put(info.efuse_clk);
    clk_disable_unprepare(info.pdma_clk);
    clk_put(info.pdma_clk);

    return ret;
}

static void __exit sc_exit(void)
{
    kfree(info.keybuf);
    sc_free_bufs();
    misc_deregister(&mdev);

    clk_disable_unprepare(info.pdma_clk);
    clk_put(info.pdma_clk);
    clk_disable_unprepare(info.efuse_clk);
    clk_put(info.efuse_clk);
    clk_disable_unprepare(info.dtrng_clk);
    clk_put(info.dtrng_clk);
    clk_disable_unprepare(info.hash_clk);
    clk_put(info.hash_clk);
    clk_disable_unprepare(info.aes_clk);
    clk_put(info.aes_clk);
}

module_init(sc_init);
module_exit(sc_exit);

MODULE_DESCRIPTION("INGENIC x2600 SC support");
MODULE_LICENSE("GPL");
