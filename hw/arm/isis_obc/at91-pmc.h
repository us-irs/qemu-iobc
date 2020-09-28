/*
 * AT91 Power Management Controller.
 *
 * Controlls AT91 system master clock.
 *
 * Notes: Register callback via at91_pmc_set_mclk_change_callback to get
 * notified when sytem clock changes. Only one callback allowed at a time.
 * This should be done by the board implementation.
 *
 * See at91-pmc.c for implementation status.
 */

#ifndef HW_ARM_ISIS_OBC_PMC_H
#define HW_ARM_ISIS_OBC_PMC_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"


#define AT91_PMC_SLCK          32768    // slow clock oscillator frequency
#define AT91_PMC_MCK        18432000    // main oscillator frequency

#define TYPE_AT91_PMC "at91-pmc"
#define AT91_PMC(obj) OBJECT_CHECK(PmcState, (obj), TYPE_AT91_PMC)


typedef void(at91_mclk_cb)(void *opaque, unsigned value);

typedef struct {
    uint32_t reg_ckgr_mor;
    uint32_t reg_ckgr_plla;
    uint32_t reg_ckgr_pllb;
    uint32_t reg_pmc_mckr;
} PmcInitState;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    const PmcInitState *init_state;

    // registers
    uint32_t reg_pmc_scsr;
    uint32_t reg_pmc_pcsr;
    uint32_t reg_ckgr_mor;
    uint32_t reg_ckgr_mcfr;
    uint32_t reg_ckgr_plla;
    uint32_t reg_ckgr_pllb;
    uint32_t reg_pmc_mckr;
    uint32_t reg_pmc_pck0;
    uint32_t reg_pmc_pck1;
    uint32_t reg_pmc_sr;
    uint32_t reg_pmc_imr;
    uint32_t reg_pmc_pllicpr;

    unsigned master_clock_freq;

    // observer for master-clock change
    at91_mclk_cb *mclk_cb;
    void *mclk_opaque;
} PmcState;


/*
 * Set the callback function to be called when the AT91 master clock changes.
 * Only one callback can be set at a time.
 */
inline static void at91_pmc_set_mclk_change_callback(PmcState *s, void *opaque, at91_mclk_cb *cb)
{
    s->mclk_cb = cb;
    s->mclk_opaque = opaque;
}

inline static void at91_pmc_set_init_state(PmcState *s, const PmcInitState *init)
{
    s->init_state = init;
}

#endif /* HW_ARM_ISIS_OBC_PWC_H */
