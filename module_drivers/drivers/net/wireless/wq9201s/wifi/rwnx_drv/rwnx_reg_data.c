
#include "rwnx_reg_data.h"

#define FAILURE    0
#define SUCCESS    1

WQ_REG_DATA wq_reg_data = {0};

extern char *reg_data_file;

int store_change_bin_format(signed char *buffer, int file_size)
{
    int channels;
    int chan = 0;
    signed char *ptr = buffer;
    if(buffer == NULL)
    {
        printk("Invalid buffer\n");
        return FAILURE;
    }

    memcpy(&wq_reg_data.header, buffer, SIZEOF_U16);
    buffer += SIZEOF_U16;
    memcpy(&wq_reg_data.version, buffer, SIZEOF_U16);
    buffer += 2*SIZEOF_U16;

    if (wq_reg_data.header != 0x125c || wq_reg_data.version != 0x0001)
    {
        printk("Invalid header or version, header 0x%x, version 0x%x\n", wq_reg_data.header, wq_reg_data.version);
        return FAILURE;
    }

    memcpy(&wq_reg_data.length, buffer, SIZEOF_U16);
    
    buffer += SIZEOF_U16;
    channels = wq_reg_data.length /PER_CHAN_SIZE;
    printk("header 0x%x, version %d, length %d, channels %d\n", wq_reg_data.header, wq_reg_data.version, wq_reg_data.length, channels);
    wq_reg_data.chan_pwr_tab = kmalloc(wq_reg_data.length, GFP_KERNEL);
    if (!wq_reg_data.chan_pwr_tab) {
        printk("Alloc mem failed\n");
        return FAILURE;
    }

    for(chan = 0; chan < channels && buffer; chan ++)
    {
        memcpy(&(wq_reg_data.chan_pwr_tab[chan].chan_num), buffer, SIZEOF_CHAN_NUM);  //get chan num
        buffer += SIZEOF_CHAN_NUM;
        memcpy(wq_reg_data.chan_pwr_tab[chan].rate_pwr_tab, buffer, PWR_TAB_LEN);  //get pwr data
        buffer += PWR_TAB_LEN;
    }

    memcpy(&(wq_reg_data.CRC32), buffer, 2*SIZEOF_CHAN_NUM);
    buffer += 2*SIZEOF_CHAN_NUM;
    
    if (buffer - ptr != file_size){
        printk("store file faild\n");
        return FAILURE;
    }
    return SUCCESS;
}

void free_pwr_tab_mem(void)
{
    kfree(wq_reg_data.chan_pwr_tab);
    return;
}

int get_pwr_data_from_bin_file(void)
{
    struct file *filePtr;
    signed char *buffer;
    int  file_size = 0;
    int bytes_read;
    loff_t offset = 0;

    filePtr = filp_open(reg_data_file, O_RDONLY, 0);
    if (IS_ERR(filePtr)) {
        printk("Error opening file, %ld\n", PTR_ERR(filePtr));
        return FAILURE;
    }

    file_size = i_size_read(file_inode(filePtr)); //get size of bin file
    printk("Size of bin file %d \n", file_size);
    buffer = kmalloc(file_size, GFP_KERNEL);
    if (!buffer) {
        printk("Alloc mem failed\n");
        filp_close(filePtr, NULL);
        return FAILURE;
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
    bytes_read = kernel_read(filePtr, (void *)buffer, (size_t)file_size, &offset);
#else
    bytes_read = kernel_read(filePtr, offset, buffer, (loff_t)file_size);
#endif
    if (bytes_read < 0)
    {
        printk("Read data fialed, close file\n");
        filp_close(filePtr, NULL);
        return FAILURE;
    }

    if(!store_change_bin_format(buffer, file_size))
    {
        printk("store data failed\n");
        kfree(buffer);
        filp_close(filePtr, NULL);
        return FAILURE;
    }

    kfree(buffer);
    filp_close(filePtr, NULL);
    return SUCCESS;
}
