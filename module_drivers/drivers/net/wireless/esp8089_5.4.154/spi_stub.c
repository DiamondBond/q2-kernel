/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 *  sdio stub code for allwinner
 */
#include <asm/io.h>
//#include <mach/irqs.h>
//#include <mach/io.h>
//#include <mach/iomux.h>
//#include <mach/pmu.h>
//#include <mach/gpio.h>
#include <linux/spi/spi.h>
#include <asm/gpio.h>
#include <asm/mach/irq.h>
#include "esp_sif.h"


//#define SPI_FREQ (20000000)                             //  1. 22.5Mhz     2. 45Mhz
#define SPI_FREQ (30000000)                             //  1. 22.5Mhz     2. 45Mhz

//Below are for spi HZ 22.5M
#if (SPI_FREQ == 30000000)

#define CMD_RESP_SIZE (10) //(50)    //Common respon wait time
#define DATA_RESP_SIZE_W  (1024*10)//   (1024*16)  //(398+400) // (1024*10)    //Only for Write bytes function, data write response.  max:(361+109) 
#define DATA_RESP_SIZE_R   (231+75) //  (340+102)  //(231+75)//(340+102)   //Only for Read bytes function, data write response    max:(340+102) 

#define BLOCK_W_DATA_RESP_SIZE_EACH          (15)//(20)           //For each data write resp size, in block write
#define BLOCK_W_DATA_RESP_SIZE_FINAL  (142+52)   //For final data write resp size, in block write ,max: 119

#define BLOCK_R_DATA_RESP_SIZE_1ST    (231+75)    //For each data read resp size, in block read  ,max: 134
#define BLOCK_R_DATA_RESP_SIZE_EACH    (20)  // (20)   //For each data read resp size, in block read 

#elif(SPI_FREQ == 20000000)

#define CMD_RESP_SIZE (10)    //Common respon wait time
#define DATA_RESP_SIZE_W (103+40)    //Only for Write bytes function, data write response.  max: 103
#define DATA_RESP_SIZE_R (118+40)    //Only for Read bytes function, data write response  max: 118
//w: oxFF : 218 clock.     oxFE : 214 clock.  

#define BLOCK_W_DATA_RESP_SIZE_EACH          (20)           //For each data write resp size, in block write 
#define BLOCK_W_DATA_RESP_SIZE_FINAL     (112+40)    //For final data write resp size, in block write ,max :112

#define BLOCK_R_DATA_RESP_SIZE_1ST          (123+40)   //For each data read resp size, in block read ,max: 123
#define BLOCK_R_DATA_RESP_SIZE_EACH       (20)   //For each data read resp size, in block read 

#endif

#define SPI_CS_LOW

//0xE5 ~0xFF  30us totoal 
//

extern int rockchip_wifi_power(int on);


struct spi_device_id esp_spi_id[] = {
        {"esp_spi_0", 0},
        {"esp_spi_1", 1},
        {},
};

#ifdef  REGISTER_SPI_BOARD_INFO
static struct dw_spi_chip spi_test_chip[] = {
{
        //.poll_mode = 1,
        .enable_dma = 1,
},
{
        //.poll_mode = 1,
        .enable_dma = 1,
},

};
static struct spi_board_info esp_board_spi_devices[] = {       
        {
                .modalias  = "esp_spi_0",
                .bus_num = 0,   //0 or 1
                .max_speed_hz  = 18*1000*1000,
                .chip_select   = 0,             
                .mode   = SPI_MODE_3,
                .controller_data = &spi_test_chip[0],
        },
};

void sif_platform_register_board_info(void) {
        spi_register_board_info(esp_board_spi_devices, ARRAY_SIZE(esp_board_spi_devices));
}
#endif  /*REGISTER_SPI_BOARD_INFO*/

#define RK30_GPIO0_BASE          1
#define GPIO_NO                  GPIO1_A5
#define GPIO_BASE_ADDR           1
#define GPIO_INT_MASK_OFFSET     1
#define GPIO_INT_STAT_OFFSET     1
#define RK30_PIN1_PB3            GPIO1_B3

int gpio_irq = 0;   //44

static int esp_reset_gpio = -1;     //36

static int esp_cs_gpio = -1;

extern int rockchip_wifi_get_oob_gpio(void);

int sif_platform_get_irq_no(void)
{
	return gpio_to_irq(gpio_irq);
}

int sif_platform_is_irq_occur(void)
{      
	return 1;
}

void sif_platform_irq_clear(void)
{
}

void sif_platform_irq_mask(int mask)
{
	if (mask)
		disable_irq_nosync(sif_platform_get_irq_no());
	else
		enable_irq(sif_platform_get_irq_no());
}

int sif_platform_irq_init(void)
{
	int ret;

	printk(KERN_ERR "%s enter\n", __func__);

	if ( (ret = gpio_request(gpio_irq, "esp_spi_int")) != 0) {
		printk(KERN_ERR "request gpio error\n");
		return ret;
	}

	gpio_direction_input(gpio_irq);

        sif_platform_irq_clear();
	sif_platform_irq_mask(1);

        udelay(1);

	return 0;
}


void sif_platform_irq_deinit(void)
{
	gpio_free(gpio_irq);
}


void sif_platform_reset_target(void)
{
    printk("ESP8089 reset %d\n", esp_reset_gpio);
    sif_platform_target_poweroff();
    sif_platform_target_poweron();
}

void sif_platform_target_poweroff(void)
{
    int ret = -1;
    ret = gpio_direction_output(esp_reset_gpio, 0);
    printk("ESP8089 power off %d, %d\n", esp_reset_gpio, ret);

    mdelay(200);

}

void sif_platform_target_poweron(void)
{
    int ret = -1;
    ret = gpio_direction_output(esp_reset_gpio, 1);
    printk("ESP8089 power on %d, %d\n", esp_reset_gpio, ret);

    mdelay(200);

}

void sif_platform_target_speed(int high_speed)
{
}

#ifdef ESP_ACK_INTERRUPT
void sif_platform_ack_interrupt(struct esp_pub *epub)
{
	sif_platform_irq_clear();
}
#endif //ESP_ACK_INTERRUPT

void sif_platform_init_gpio(void)
{
    int len;
    struct device_node *np = NULL;
    char prop_name[32];
    const char *prop_str;
    const __be32 *prop;

    np = of_find_node_by_name(NULL, "wifi_spi");
    if (np == NULL) {
        printk("%s  wifi_spi is null, init failed \n ", __func__);
        return;
    }

    snprintf(prop_name, sizeof(prop_name), "spi_wifi,wifi_name");
    if (of_property_read_string(np, prop_name, &prop_str)) {
        printk("%s error: no %s property\n", __func__, prop_name);
    } else {
        printk("%s: %s[%s]\n", __func__, prop_name, prop_str);
    }

    snprintf(prop_name, sizeof(prop_name), "spi_wifi,wifi_pwd");
    prop = of_get_property(np, prop_name, &len);
    if (!prop || (len != (sizeof(__be32)))) {
        printk("%s error: %s %s property\n",  __func__, prop ? "invalid" : "no", prop_name);
    } else {
        esp_reset_gpio = be32_to_cpup(&prop[0]);
        printk("%s: %s[%d]\n", __func__, prop_name, esp_reset_gpio);
    }

    snprintf(prop_name, sizeof(prop_name), "spi_wifi,oob_irq");
    prop = of_get_property(np, prop_name, &len);
    if (!prop || (len != (sizeof(__be32)))) {
        printk("%s error: %s %s property\n",  __func__, prop ? "invalid" : "no", prop_name);
    } else {
        gpio_irq = be32_to_cpup(&prop[0]);
        printk("%s: %s[%d]\n", __func__, prop_name, gpio_irq);
    }

    gpio_request(esp_reset_gpio, "esp_reset");

#ifdef SPI_CS_LOW
    snprintf(prop_name, sizeof(prop_name), "spi_wifi,cs");
    prop = of_get_property(np, prop_name, &len);
    if (!prop || (len != (sizeof(__be32)))) {
        printk("%s error: %s %s property\n",  __func__, prop ? "invalid" : "no", prop_name);
    } else {
        esp_cs_gpio = be32_to_cpup(&prop[0]);
        printk("%s: %s[%d]\n", __func__, prop_name, esp_cs_gpio);
    }

    gpio_request(esp_cs_gpio, "esp_cs");
    gpio_direction_output(esp_cs_gpio, 0);
#endif


}

void sif_platform_deinit_gpio(void)
{
    gpio_free(esp_reset_gpio);

#ifdef SPI_CS_LOW
    gpio_free(esp_cs_gpio);
#endif
}
