// reg data bin format:
//[0~1] HEADER    0x5C, 0x12   
//[2~3] VERSION   0x01, 0x00
//[4~5] Reserved   0x00, 0x00
//[6~7] Length      0xA4, 0x1B  (从Length之后到最后CRC32之前的字节数: 7076, 若channel数有变化会改变)
//[8~7083] Data （7076 Bytes，若channel数有变化会改变)
//      7076 Bytes = 58 channels * 122 Bytes/channel
//      122 Bytes = 2 (channel number)  (e.g. ch100:   byte0 = 0x64, byte1 = 0x00)
//                  + 4 (11B, 1/2/5.5/11M) 
//                  + 8 (OFDM, 6/9/12/18/24/36/48/54M) 
//                  + 12 (BW20 HT/VHT, MCS0~11) 
//                  + 12 (BW40 HT/VHT, MCS0~11) 
//                  + 12 (BW80 VHT, MCS0~11) 
//                  + 12 * 6 (AX, RU26/52/106/242/484/996, MCS0~11)
//      Data中的每一个target power 为 1Byte 有符号数， 单位为0.5dB.
//      (e.g. 0x21 代表 16.5dBm,  0xFE代表 -1dBm)
//[7084~7087] CRC32 (Low word at first)

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/version.h>

#define PWR_TAB_LEN          120     //4 (11B, 1/2/5.5/11M) + 8 (OFDM, 6/9/12/18/24/36/48/54M) 
                                     //+ 12 (BW20 HT/VHT, MCS0~11) + 12 (BW40 HT/VHT, MCS0~11) 
                                     //+ 12 (BW80 VHT, MCS0~11) + 12 * 6 (AX, RU26/52/106/242/484/996, MCS0~11)
#define PER_CHAN_SIZE        122     // RATE_TAB_LEN + sizeof(chan_num)
#define SIZEOF_CHAN_NUM       2
#define SIZEOF_U16            sizeof(uint16_t)

struct chan_pwr_data{
        uint16_t chan_num;
        //struct rate_pwr_data rate_pwr_tab;
        signed char rate_pwr_tab[PWR_TAB_LEN];
    };

typedef struct _wq_reg_data{
    uint16_t header;     //0x5c,0x12
    uint16_t version;    //0x01, 0x00
    uint16_t reserved;    //0x00, 0x00
    uint16_t length;      //0xa4, 0x1b
    struct  chan_pwr_data *chan_pwr_tab;
    uint32_t CRC32;
}__packed WQ_REG_DATA;


extern WQ_REG_DATA wq_reg_data;
int get_pwr_data_from_bin_file(void);
void free_pwr_tab_mem(void);
extern bool gv_get_pwr_from_bin_flag;
