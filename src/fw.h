#ifndef FW_H
#define FW_H

#include <stdint.h>
#include "regs.h"

void delay_ms(uint32_t ms);

uint8_t spi_byte(uint8_t out);
void spi_flush(void);

void board_clock_init(void);
void lcd_gpio_init(void);
void lcd_init(void);
void lcd_command(uint8_t cmd);
void lcd_data8(uint8_t value);
void lcd_fill(int x, int y, int w, int h, uint16_t colour);
void lcd_blit(int x, int y, int w, int h, const uint8_t *bytes_pixels);
void lcd_set_window(int x0, int y0, int x1, int y1);
void lcd_stream_begin(int x, int y, int x2, int y2);
void lcd_raw_write(const uint8_t *p, uint32_t n);
void lcd_raw_idle(void);
void lcd_dc_as_input(void);
void lcd_dc_as_output(void);

void adc_init(void);
uint16_t adc_read(uint8_t ch);

#endif
/* ?? SPI NOR?U2 P25D80H? */
void    flash_read_id(uint8_t id[3]);
uint8_t flash_read_status(void);
int     flash_wait_ready(uint32_t spin);
void    flash_read(uint32_t addr, uint8_t *dst, int n);
void    flash_erase_4k(uint32_t addr);
void    flash_erase_chip(void);

void gpio_mode(GPIO_TypeDef *port, uint32_t pins, uint32_t nibble);
void delay_cycles(uint32_t n);

/* USB???? CDC-ACM? */
extern int usb_irq_enable;
void usb_init(void);
void usb_gpio_init(void);
int  usb_read(uint8_t *dst, int max);
int  usb_write(const uint8_t *src, int len);
int  usb_take_greeting(uint8_t *dst);
/* ????????usb.c? */
extern volatile uint32_t usb_reset_count;
extern volatile uint32_t usb_ep2_drop;
extern volatile uint32_t usb_packet_count;
extern uint8_t usb_req_log[16];
extern uint8_t usb_req_type[16];
extern volatile uint32_t usb_req_count;
extern volatile uint32_t usb_irq_count;
void usb_service(void);
extern volatile uint32_t usb_setup_count;
extern uint8_t usb_setup_snap[4];
extern uint8_t cfg_selected;
extern uint8_t usb_tx_snap[8];
extern volatile uint8_t ep3_busy;
extern uint16_t usb_tx_len;
extern volatile uint8_t usb_last_fg;
extern volatile uint8_t usb_last_st;
extern volatile uint32_t usb_suspend_count;
extern volatile uint32_t usb_in_count;
extern volatile uint32_t usb_out_count;
extern volatile uint16_t usb_last_len;
extern volatile uint8_t usb_last_b0;
extern volatile uint8_t usb_last_b1;
extern volatile uint32_t usb_other_count;

#ifndef FW_VER_MAJOR
#define FW_VER_MAJOR  28u
#endif
#ifndef FW_VER_MINOR
#define FW_VER_MINOR  0u
#endif

void usb_pump(void);
