#ifndef HW_ARM_ISIS_OBC_GPIO_PUSHBUTTON_H
#define HW_ARM_ISIS_OBC_GPIO_PUSHBUTTON_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "io/channel-socket.h"


#define TYPE_GPIO_PUSHBUTTON "at91-gpio_pushbutton"
#define GPIO_PUSHBUTTON(obj) OBJECT_CHECK(GpioPushbuttonState, (obj), TYPE_GPIO_PUSHBUTTON)

typedef struct {
    SysBusDevice parent_obj;

    qemu_irq buttons[32];
    QIOChannelSocket *ioc;
} GpioPushbuttonState;

#endif /* HW_ARM_ISIS_OBC_GPIO_PUSHBUTTON_H */
