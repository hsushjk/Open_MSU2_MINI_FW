#ifndef FW_H
#define FW_H

#include <stdint.h>
#include "regs.h"

/* ---- 基础 ---- */
void delay_ms(uint32_t ms);

/* ---- SPI（屏与 flash 共用） ---- */
uint8_t spi_byte(uint8_t out);
void spi_flush(void);

/* ---- 时钟 / 面板 ---- */
void board_clock_init(void);
void lcd_gpio_init(void);
void lcd_init(void);
void lcd_command(uint8_t cmd);
void lcd_data8(uint8_t value);
void lcd_fill(int x, int y, int w, int h, uint16_t colour);
/* 开机自检画面（bootscreen.c） */
void boot_screen_begin(void);
void boot_screen_row(uint8_t row, const char *label, uint8_t state);
void boot_screen_done(uint8_t passed, uint8_t total);
void lcd_blit(int x, int y, int w, int h, const uint8_t *bytes_pixels);
void lcd_set_window(int x0, int y0, int x1, int y1);
void lcd_stream_begin(int x, int y, int x2, int y2);
void lcd_raw_write(const uint8_t *p, uint32_t n);
void lcd_stream_wait(void);      /* 等这一笔 DMA 走完（不抬片选） */
void lcd_raw_idle(void);
void lcd_dc_as_input(void);
void lcd_dc_as_output(void);

/* ---- 触摸 ---- */
void adc_init(void);
uint16_t adc_read(uint8_t ch);

/* ---- SPI NOR（P25D80H） ---- */
void    flash_read_id(uint8_t id[3]);
uint8_t flash_read_status(void);
int     flash_wait_ready(uint32_t spin);
void    flash_read(uint32_t addr, uint8_t *dst, int n);
void    flash_wren(void);
void    flash_program(uint32_t addr, const uint8_t *src, int n);
void    flash_erase_4k(uint32_t addr);
int     flash_erase_chip_now(void);
int     flash_is_blank(uint32_t addr, int n);

/* ---- CRC32（逐位实现，不查表，省 flash） ---- */
uint32_t asset_crc32_byte(uint32_t crc, uint8_t b);
uint32_t asset_crc32_bytes(uint32_t crc, const uint8_t *p, int n);

/* ---- 资产：开机 / 待机画面，设置块 ---- */
typedef struct {
    uint8_t  boot_mode;      /* 0=不画 1=画 NOR 里的开机图 */
    uint8_t  standby_mode;   /* 0=黑屏 1=四角方块 2=图片轮播 3=弹球 4=呼吸 */
    uint8_t  standby_count;  /* 轮播图片张数 1..8 */
    uint16_t dwell;          /* 每张停留秒数 */
    uint8_t  boot_hold;      /* 开机画面停留秒数 1..60（0 当作 4） */
} settings_t;

#define ASSET_BOOT     0u
#define ASSET_STANDBY  1u
#define STANDBY_SLOTS  8u     /* 待机图片槽数（NOR 里 0x010000 起每槽 32 KB） */

uint32_t asset_slot_addr(uint8_t kind, uint8_t idx);
uint32_t asset_max_len(void);
uint32_t asset_data_addr(uint8_t kind, uint8_t idx);
void     asset_begin(uint8_t kind, uint8_t idx, uint32_t len);
void     asset_feed(const uint8_t *src, int n);
int      asset_writing(void);
void     asset_abort(void);
uint32_t asset_left(void);
int      asset_finish(uint32_t expect_crc);
void     asset_erase(uint8_t kind, uint8_t idx);
int      asset_valid(uint8_t kind, uint8_t idx);
int     asset_verify(uint8_t kind, uint8_t idx, uint32_t *calc);
void    asset_invalidate(void);
int      asset_draw(uint8_t kind, uint8_t idx);

void settings_default(settings_t *s);
int  settings_load(settings_t *s);
int  settings_save(const settings_t *s);

/* ---- 杂项 ---- */
void gpio_mode(GPIO_TypeDef *port, uint32_t pins, uint32_t nibble);
void delay_cycles(uint32_t n);

/* ---- USB（CDC-ACM 设备） ---- */
extern int usb_irq_enable;
void usb_init(void);
void usb_gpio_init(void);
int  usb_read(uint8_t *dst, int max);
int  usb_write(const uint8_t *src, int len);
int  usb_take_greeting(uint8_t *dst);
void usb_service(void);
void usb_pump(void);

extern volatile uint32_t usb_reset_count;
extern volatile uint32_t usb_ep2_drop;
extern volatile uint32_t usb_packet_count;
extern volatile uint32_t usb_req_count;
extern volatile uint32_t usb_irq_count;
extern volatile uint32_t usb_setup_count;
extern volatile uint32_t usb_suspend_count;
extern volatile uint32_t usb_in_count;
extern volatile uint32_t usb_out_count;
extern volatile uint16_t usb_last_len;
extern volatile uint8_t  usb_last_b0;
extern volatile uint8_t  usb_last_b1;
extern volatile uint32_t usb_other_count;
extern volatile uint8_t  usb_last_fg;
extern volatile uint8_t  usb_last_st;
extern volatile uint8_t  ep3_busy;
extern uint8_t  usb_req_log[16];
extern uint8_t  usb_req_type[16];
extern uint8_t  usb_setup_snap[4];
extern uint8_t  usb_tx_snap[8];
extern uint16_t usb_tx_len;
extern uint8_t  cfg_selected;

#ifndef FW_VER_MAJOR
#define FW_VER_MAJOR  59u
#endif
#ifndef FW_VER_MINOR
#define FW_VER_MINOR  0u
#endif

#endif /* FW_H */