/*
 * Simple GPIO pushbuttons.
 *
 * Emulates an array of 32 GPIO pushbuttons. Specifically, this device
 * provides 32 IRQ lines, which can be controlled via a UDP packet to
 * localhost:6000. The data format expected is two bytes: The first byte
 * provides the number of the pin, the second the state (0 or 1).
 *
 * Useful for the AT91 getting-started example. Currently not added to the
 * board.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

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
