// ADC / 触摸（CH32X035 的 TouchKey 复用 ADC1 寄存器）
// 完全按原厂 adc_init @0x2BCC 与 adc_read @0x2C58 的做法：
//   CTLR1 的 bit24 是 TKENABLE（触摸键模式使能，不是普通 ADC 位），
//   转换不是靠 SWSTART，而是写 IDATAR1=0x70、RDATAR=0x40 后轮询 STATR。
//   注意：原厂 sub_3D5C(ADC1->STATR, 2) 测的是 bit1 = EOC，不是 bit2 = JEOC；
//   只等 JEOC 会一直等到超时，读到的是没更新的旧值（表现为基准恒定、触摸无反应）。
#include "fw.h"

#define ADC1_BASE  0x40012400u
#define ADCR(n)    (*(volatile uint32_t *)(ADC1_BASE + (n)))

#define ADC_STATR    0x00u
#define ADC_CTLR1    0x04u
#define ADC_CTLR2    0x08u
#define ADC_SAMPTR1  0x0Cu
#define ADC_SAMPTR2  0x10u
#define ADC_RSQR1    0x2Cu
#define ADC_RSQR3    0x34u
#define ADC_IDATAR1  0x3Cu
#define ADC_RDATAR   0x4Cu

#define ADC_CTLR2_ADON  (1u << 0)
#define ADC_CTLR1_TKEN  (1u << 24)   /* TKEN mode enable */
#define ADC_STATR_EOC   (1u << 1)    /* 规则通道转换完成（原厂等的就是这个） */
#define ADC_STATR_JEOC  (1u << 2)    /* 注入通道转换完成 */
#define ADC_STATR_DONE  (ADC_STATR_EOC | ADC_STATR_JEOC)

void adc_init(void)
{
    uint32_t i;

    RCC->APB2PCENR |= (1u << 9);                                /* ADC1 时钟 */
    RCC->CFGR0 = (RCC->CFGR0 & ~(3u << 14)) | (2u << 14);       /* ADCPRE = /6, 8 MHz */

    gpio_mode(GPIOB, 0x00000002u, 0x0u);                        /* PB1 模拟输入（触摸电极） */

    ADCR(ADC_CTLR2) |= ADC_CTLR2_ADON;
    for (i = 0; i < 20000u; i++)
        ;

    ADCR(ADC_CTLR1) |= ADC_CTLR1_TKEN;                          /* 原厂：CTLR1 |= 0x1000000 */
}

/* 采样时间取最长的 239.5 周期（3 位编码 7）：触摸电极面积小、阻抗高 */
static void adc_set_channel(uint8_t ch)
{
    uint32_t shift;

    ADCR(ADC_RSQR1) = 0u;                                       /* 序列长度 1 */

    if (ch < 10u) {
        shift = (uint32_t)ch * 3u;
        ADCR(ADC_SAMPTR1) = (ADCR(ADC_SAMPTR1) & ~(7u << shift)) | (7u << shift);
    } else {
        shift = (uint32_t)(ch - 10u) * 3u;
        ADCR(ADC_SAMPTR2) = (ADCR(ADC_SAMPTR2) & ~(7u << shift)) | (7u << shift);
    }

    ADCR(ADC_RSQR3) = (uint32_t)ch;
}

uint16_t adc_read(uint8_t ch)
{
    static uint8_t  s_busy;
    static uint8_t  s_ch = 0xFFu;
    static uint16_t s_value;
    uint32_t v;

    /* 触摸键的充电/放电转换要几毫秒，原地等就会把 USB 端点拖到超时
       （主机侧表现：卡顿、整帧写截断、花屏）。所以采用“先取上次结果、
       再启动本次”的方式：同一通道连续采样时完全不阻塞。 */
    if (s_busy && ch == s_ch) {
        if (!(ADCR(ADC_STATR) & ADC_STATR_DONE))
            return s_value;                 /* 还没转换完，先给上一拍的值 */
        v = ADCR(ADC_RDATAR);
        ADCR(ADC_STATR) = 0u;
        s_value = (uint16_t)(v & 0x0FFFu);
        s_busy = 0u;
    }

    adc_set_channel(ch);
    ADCR(ADC_STATR) = 0u;
    ADCR(ADC_IDATAR1) = 0x70u;
    ADCR(ADC_RDATAR) = 0x40u;

    if (ch != s_ch) {
        /* 切通道也不等：立即返回旧值，让下一拍去取。
           一旦在这里等转换，主循环就会停数十毫秒，
           期间 USB 接收环没人消费，整帧字节被静默丢弃。 */
        s_ch = ch;
        s_busy = 1u;
        return s_value;
    }

    s_busy = 1u;
    return s_value;
}