/* Ingenic PDMA request TSZ 策略接口。 */
#ifndef __INGENIC_DMA_REQUEST_POLICY_H__
#define __INGENIC_DMA_REQUEST_POLICY_H__

enum ingenic_dma_controller_bus {
	INGENIC_DMA_AHB2,
	INGENIC_DMA_MCU,
};

enum ingenic_dma_tsz_mode {
	INGENIC_DMA_TSZ_AUTONOMOUS,
	INGENIC_DMA_TSZ_FIXED,
	INGENIC_DMA_TSZ_EXTERNAL_DECIDE,
};

enum ingenic_dma_tsz_mode
ingenic_dma_get_tsz_mode(unsigned int controller_bus, unsigned int request_type);
#endif
