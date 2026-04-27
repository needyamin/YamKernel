/* YamKernel — Local APIC + APIC timer + IO-APIC */
#ifndef _CPU_APIC_H
#define _CPU_APIC_H

#include <nexus/types.h>

#define APIC_VEC_SPURIOUS  0xFF
#define APIC_VEC_TIMER     0x20   /* same as legacy IRQ0 vector */

void  apic_init(u64 hhdm);            /* call after acpi_init */
void  apic_init_local(void);          /* call on every CPU */
void  apic_eoi(void);
bool  apic_active(void);
void  apic_timer_start(u32 hz);       /* periodic, vector = APIC_VEC_TIMER */

void  ioapic_init(u64 hhdm);
void  ioapic_set_irq(u8 irq, u8 vector, u8 lapic_id);  /* legacy ISA IRQ → vector */

#endif
