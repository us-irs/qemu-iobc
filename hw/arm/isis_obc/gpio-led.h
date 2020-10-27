/*
 * Simple emulated LED.
 *
 * Simulates a single LED accessible via a GPIO, which outputs its state on
 * change to the QEMU log. The GPIO is a named IRQ line ("led") and controlls
 * the state of the LED (on/off). Connect this GPIO to control the LED, e.g.
 * via a the push-button emulation. The LED can be given a name via the "name"
 * property.
 *
 * Useful for the AT91 getting-started example. Currently not added to the
 * board.
 *
 * Copyright (c) 2019-2020 KSat e.V. Stuttgart
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version. See the COPYING file in the top-level directory.
 */

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
