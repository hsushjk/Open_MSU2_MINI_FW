/* 描述符表：干净的双接口 CDC-ACM，不暴露 HID 接口。
 * 设备只有 CDC 控制 + CDC 数据两个接口，主机据此只加载串口驱动。 */
#include <stdint.h>
#include "usb_desc.h"

static const uint8_t usb_device_desc[18] = {
    0x12, 0x01, 0x10, 0x01, 0x02, 0x00, 0x00, 0x40,
    0x86, 0x1A, 0x0D, 0xFE, 0x09, 0x01,
    0x01, 0x02, 0x03, 0x01
};

static const uint8_t usb_config_desc[67] = {
    /* 配置描述符 */
    0x09, 0x02, 0x43, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,
    /* 接口 0：CDC 控制 */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    /* 头功能描述符 */
    0x05, 0x24, 0x00, 0x10, 0x01,
    /* 呼叫管理 */
    0x05, 0x24, 0x01, 0x00, 0x01,
    /* ACM */
    0x04, 0x24, 0x02, 0x02,
    /* 联合 */
    0x05, 0x24, 0x06, 0x00, 0x01,
    /* EP1 IN 中断 */
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x10,
    /* 接口 1：CDC 数据 */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    /* EP2 OUT 批量 */
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
    /* EP3 IN 批量 */
    0x07, 0x05, 0x83, 0x02, 0x40, 0x00, 0x00
};

static const uint8_t usb_string_langid[4] = { 0x04, 0x03, 0x09, 0x04 };

static const uint8_t usb_string_1[10] = {
    0x0A, 0x03, 'M', 0, 'S', 0, 'U', 0, '2', 0
};

static const uint8_t usb_string_2[20] = {
    0x14, 0x03,
    'M', 0, 'S', 0, 'U', 0, '2', 0, '_', 0, 'M', 0, 'I', 0, 'N', 0, 'I', 0
};

static const uint8_t usb_string_3[26] = {
    0x18, 0x03,
    'M', 0, 'S', 0, 'U', 0, '2', 0, 'M', 0, '2', 0, '4', 0, 'Q', 0, '3', 0, '0', 0, '0', 0, '1', 0
};

const uint8_t *usb_desc_get(uint8_t type, uint8_t index, uint16_t *len)
{
    switch (type) {
    case 0x01:
        *len = sizeof(usb_device_desc);
        return usb_device_desc;
    case 0x02:
        *len = sizeof(usb_config_desc);
        return usb_config_desc;
    case 0x03:
        switch (index) {
        case 0:
            *len = sizeof(usb_string_langid);
            return usb_string_langid;
        case 1:
            *len = sizeof(usb_string_1);
            return usb_string_1;
        case 2:
            *len = sizeof(usb_string_2);
            return usb_string_2;
        case 3:
            *len = sizeof(usb_string_3);
            return usb_string_3;
        default:
            return 0;
        }
    default:
        return 0;
    }
}