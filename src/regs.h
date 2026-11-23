/* CH32X033 / MSU2_MINI minimal register map.
 * Only the peripherals this firmware touches are described; offsets are taken
 * from the vendor header (ch32x035.h) and cross-checked against the original
 * firmware's register accesses.
 */
#ifndef REGS_H
#define REGS_H

#include <stdint.h>

#define PERIPH_BASE      0x40000000u
#define APB1PERIPH_BASE  (PERIPH_BASE + 0x00000u)
#define APB2PERIPH_BASE  (PERIPH_BASE + 0x10000u)
#define AHBPERIPH_BASE   (PERIPH_BASE + 0x20000u)

#define RCC_BASE    (AHBPERIPH_BASE + 0x1000u)
#define FLASH_BASE  (AHBPERIPH_BASE + 0x2000u)
#define DMA1_BASE   (AHBPERIPH_BASE)
#define GPIOA_BASE  (APB2PERIPH_BASE + 0x0800u)
#define GPIOB_BASE  (APB2PERIPH_BASE + 0x0C00u)
#define GPIOC_BASE  (APB2PERIPH_BASE + 0x1000u)
#define SPI1_BASE   (APB2PERIPH_BASE + 0x3000u)

typedef struct {
    volatile uint32_t CTLR;
    volatile uint32_t CFGR0;
    volatile uint32_t RESERVED0;
    volatile uint32_t APB2PRSTR;
    volatile uint32_t APB1PRSTR;
    volatile uint32_t AHBPCENR;
    volatile uint32_t APB2PCENR;
    volatile uint32_t APB1PCENR;
} RCC_TypeDef;

typedef struct {
    volatile uint32_t ACTLR;
    volatile uint32_t KEYR;
    volatile uint32_t OBKEYR;
    volatile uint32_t STATR;
    volatile uint32_t CTLR;
    volatile uint32_t ADDR;
} FLASH_TypeDef;

typedef struct {
    volatile uint32_t CFGLR;
    volatile uint32_t CFGHR;
    volatile uint32_t INDR;
    volatile uint32_t OUTDR;
    volatile uint32_t BSHR;
    volatile uint32_t BCR;
    volatile uint32_t LCKR;
    volatile uint32_t CFGXR;   /* pins 16..23, 4-bit mode each */
    volatile uint32_t BSXR;    /* pins 16..23 set/pull-up */
} GPIO_TypeDef;

typedef struct {
    volatile uint32_t CFGR;
    volatile uint32_t CNTR;
    volatile uint32_t PADDR;
    volatile uint32_t MADDR;
} DMA_Channel_TypeDef;

typedef struct {
    volatile uint32_t INTFR;
    volatile uint32_t INTFCR;
} DMA_TypeDef;

typedef struct {
    volatile uint32_t CTLR1;
    volatile uint32_t CTLR2;
    volatile uint32_t STATR;
    volatile uint32_t DATAR;
} SPI_TypeDef;

#define RCC    ((RCC_TypeDef *)RCC_BASE)
#define FLASH  ((FLASH_TypeDef *)FLASH_BASE)
#define DMA1   ((DMA_TypeDef *)DMA1_BASE)
#define GPIOA  ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB  ((GPIO_TypeDef *)GPIOB_BASE)
#define GPIOC  ((GPIO_TypeDef *)GPIOC_BASE)
#define SPI1   ((SPI_TypeDef *)SPI1_BASE)

/* DMA1 channel 3: SPI1 TX, base 0x40020030 */
#define DMA1_CH3 ((DMA_Channel_TypeDef *)(DMA1_BASE + 0x08u + 2u * 0x14u))

/* RCC_APB2PCENR bits */
#define RCC_APB2_AFIO   (1u << 0)
#define RCC_APB2_GPIOA  (1u << 2)
#define RCC_APB2_GPIOB  (1u << 3)
#define RCC_APB2_GPIOC  (1u << 4)
#define RCC_APB2_SPI1   (1u << 12)
#define RCC_APB2_USBFS  (1u << 4)

/* RCC_AHBPCENR bits */
#define RCC_AHB_DMA1    (1u << 0)
#define RCC_AHB_USBFS   (1u << 12)

/* GPIO mode nibbles (CFGLR encoding: CNF[1:0] << 2 | MODE[1:0]) */
#define GPIO_MODE_OUT_PP_10M  0x1u
#define GPIO_MODE_AF_PP_50M   0x9u
#define GPIO_MODE_IN_FLOAT    0x4u

/* SPI CTLR1 for master, 8-bit, mode 3, software NSS, fPCLK/2 (24 MHz) */
#define SPI_CTLR1_MASTER_8BIT_MODE3  (0x0307u)
#define SPI_CTLR1_SPE                (1u << 6)

/* DMA CFGR bits */
#define DMA_CFGR_EN    (1u << 0)
#define DMA_CFGR_DIR   (1u << 4)  /* 1 = read from memory */
#define DMA_CFGR_MINC  (1u << 7)
#define DMA_TCIF3      (1u << 9)

#endif /* REGS_H */

/* NVIC */
#define NVIC_ISER1  (*(volatile uint32_t *)0xE000E104u)
#define NVIC_ICER1  (*(volatile uint32_t *)0xE000E184u)

/* AFIO 控制寄存器：USB 需要在这里打开收发器 */
#define AFIO_CTLR (*(volatile uint32_t *)0x40010018u)
