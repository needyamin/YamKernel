#ifndef YAMOS_INSTALLER_H
#define YAMOS_INSTALLER_H

#include <nexus/types.h>
#include "kernel/api/syscall.h"

void installer_init(void);
int installer_status(const char *package, yam_installer_status_t *out);
int installer_request(const char *package, yam_installer_status_t *out);

#endif
