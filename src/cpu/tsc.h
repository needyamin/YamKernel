/* YamKernel - TSC helpers */
#ifndef _CPU_TSC_H
#define _CPU_TSC_H

#include <nexus/types.h>

void tsc_init(void);
u64  tsc_read(void);
bool tsc_deadline_available(void);

#endif
