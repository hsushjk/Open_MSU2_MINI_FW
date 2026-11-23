#ifndef USB_DESC_H
#define USB_DESC_H

#include <stdint.h>

/* type 与 USB 描述符类型编号一致；index 仅对字符串描述符有效 */
const uint8_t *usb_desc_get(uint8_t type, uint8_t index, uint16_t *len);

#endif
