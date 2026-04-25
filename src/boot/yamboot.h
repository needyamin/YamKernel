/* ============================================================================
 * YamBoot — Custom pre-kernel boot stage (runs after Limine, before kernel_main)
 * ============================================================================ */
#ifndef _BOOT_YAMBOOT_H
#define _BOOT_YAMBOOT_H

typedef enum {
    YAMBOOT_NORMAL = 1,
    YAMBOOT_SAFE   = 2,
    YAMBOOT_REBOOT = 3,
} yamboot_choice_t;

extern int g_yamboot_safe;

yamboot_choice_t yamboot_show(void);

#endif
