#ifndef HW_ARM_ISIS_OBC_GPIO_LED_H
#define HW_ARM_ISIS_OBC_GPIO_LED_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"


#define TYPE_GPIO_LED "at91-gpio_led"
#define GPIO_LED(obj) OBJECT_CHECK(GpioLedState, (obj), TYPE_GPIO_LED)

typedef struct {
    SysBusDevice parent_obj;

    char* name;
    int state;
} GpioLedState;

#endif /* HW_ARM_ISIS_OBC_GPIO_LED_H */
