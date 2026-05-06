#include "ahci.h"
#include "block.h"
#include "../bus/pci.h"
#include "../../mem/pmm.h"
#include "../../mem/vmm.h"
#include "../../mem/heap.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"

#define SATA_SIG_ATA    0x00000101  /* SATA drive */
#define SATA_SIG_ATAPI  0xEB140101  /* SATAPI drive */
#define AHCI_PORT_DET_PRESENT 3
#define AHCI_PORT_IPM_ACTIVE  1

typedef struct {
    u32 clb;
    u32 clbu;
    u32 fb;
    u32 fbu;
    u32 is;
    u32 ie;
    u32 cmd;
    u32 res0;
    u32 tfd;
    u32 sig;
    u32 ssts;
    u32 sctl;
    u32 serr;
    u32 sact;
    u32 ci;
    u32 sntf;
    u32 fbs;
    u32 res1[11];
    u32 vendor[4];
} __attribute__((packed)) hba_port_t;

typedef struct {
    u32 cap;
    u32 ghc;
    u32 is;
    u32 pi;
    u32 vs;
    u32 ccc_ctl;
    u32 ccc_pts;
    u32 em_loc;
    u32 em_ctl;
    u32 cap2;
    u32 bohc;
    u8  res[0xA0-0x2C];
    u8  vendor[0x100-0xA0];
    hba_port_t ports[32];
} __attribute__((packed)) hba_mem_t;

typedef struct {
    u8  cfl:5;
    u8  a:1;
    u8  w:1;
    u8  p:1;
    u8  r:1;
    u8  b:1;
    u8  c:1;
    u8  rsv0:1;
    u8  pmp:4;
    u16 prdtl;
    u32 prdbc;
    u32 ctba;
    u32 ctbau;
    u32 rsv1[4];
} __attribute__((packed)) ahci_cmd_hdr_t;

typedef struct {
    u32 dba;
    u32 dbau;
    u32 rsv0;
    u32 dbc:22;
    u32 rsv1:9;
    u32 i:1;
} __attribute__((packed)) ahci_prdt_entry_t;

typedef struct {
    u8  cfis[64];
    u8  acmd[16];
    u8  rsv[48];
    ahci_prdt_entry_t prdt_entry[256]; /* Up to 256 PRDT entries, 1MB max per command */
} __attribute__((packed)) ahci_cmd_tbl_t;

typedef struct {
    pci_device_t *pci_dev;
    hba_mem_t *abar;
    u8 port_no;
    hba_port_t *port;
    block_device_t bdev;
    ahci_cmd_tbl_t *cmd_tbl;
    ahci_cmd_hdr_t *cmd_list;
} ahci_drive_t;

static int check_type(hba_port_t *port) {
    u32 ssts = port->ssts;
    u8 det = ssts & 0x0F;
    u8 ipm = (ssts >> 8) & 0x0F;
    if (det != AHCI_PORT_DET_PRESENT) return 0;
    if (ipm != AHCI_PORT_IPM_ACTIVE) return 0;
    if (port->sig == SATA_SIG_ATA) return 1;
    if (port->sig == SATA_SIG_ATAPI) return 2;
    return 0;
}

static void stop_cmd(hba_port_t *port) {
    port->cmd &= ~(1 << 0); // ST (Start)
    port->cmd &= ~(1 << 4); // FRE (FIS Receive Enable)
    while (1) {
        if (port->cmd & (1 << 15)) continue; // CR (Command List Running)
        if (port->cmd & (1 << 14)) continue; // FR (FIS Receive Running)
        break;
    }
}

static void start_cmd(hba_port_t *port) {
    while (port->cmd & (1 << 15));
    port->cmd |= (1 << 4); // FRE
    port->cmd |= (1 << 0); // ST
}

static bool ahci_port_rebase(hba_port_t *port) {
    stop_cmd(port);
    u64 clb_phys = pmm_alloc_pages(1); // 4KB for command list (1024 bytes) and FIS (256 bytes)
    if (!clb_phys) return false;
    memset(vmm_phys_to_virt(clb_phys), 0, PAGE_SIZE);

    port->clb = (u32)(clb_phys & 0xFFFFFFFF);
    port->clbu = (u32)(clb_phys >> 32);
    
    u64 fb_phys = clb_phys + 1024; // FIS fits in the same 4K page
    port->fb = (u32)(fb_phys & 0xFFFFFFFF);
    port->fbu = (u32)(fb_phys >> 32);

    ahci_cmd_hdr_t *cmdheader = (ahci_cmd_hdr_t *)vmm_phys_to_virt(clb_phys);
    u64 ctba_phys = pmm_alloc_pages(1); // 4KB for 1 command table
    if (!ctba_phys) return false;
    memset(vmm_phys_to_virt(ctba_phys), 0, PAGE_SIZE);

    cmdheader[0].prdtl = 256;
    cmdheader[0].ctba = (u32)(ctba_phys & 0xFFFFFFFF);
    cmdheader[0].ctbau = (u32)(ctba_phys >> 32);

    start_cmd(port);
    return true;
}

static int ahci_read_write(block_device_t *dev, u64 lba, u32 count, void *buf, bool write) {
    ahci_drive_t *drive = (ahci_drive_t *)dev->driver_data;
    hba_port_t *port = drive->port;

    port->is = (u32)-1; // Clear pending interrupt bits
    int spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) spin++; // Wait for BSY and DRQ
    if (spin == 1000000) return -1;

    u64 phys_buf = vmm_virt_hhdm_to_phys(buf);
    
    ahci_cmd_hdr_t *cmdheader = (ahci_cmd_hdr_t *)vmm_phys_to_virt(((u64)port->clbu << 32) | port->clb);
    cmdheader[0].cfl = sizeof(u32) * 5 / 4; // FIS size in DWORDS (20 bytes = 5 dwords)
    cmdheader[0].w = write ? 1 : 0;
    cmdheader[0].prdtl = 1; // 1 PRDT entry for now, assuming contiguous physical memory
    
    ahci_cmd_tbl_t *cmdtbl = (ahci_cmd_tbl_t *)vmm_phys_to_virt(((u64)cmdheader[0].ctbau << 32) | cmdheader[0].ctba);
    memset(cmdtbl, 0, sizeof(ahci_cmd_tbl_t) + sizeof(ahci_prdt_entry_t));
    
    cmdtbl->prdt_entry[0].dba = (u32)(phys_buf & 0xFFFFFFFF);
    cmdtbl->prdt_entry[0].dbau = (u32)(phys_buf >> 32);
    cmdtbl->prdt_entry[0].dbc = (count * 512) - 1; // Byte count - 1
    cmdtbl->prdt_entry[0].i = 1;

    u8 *fis = cmdtbl->cfis;
    fis[0] = 0x27; // Host to device
    fis[1] = 0x80; // Command
    fis[2] = write ? 0x35 : 0x25; // Write DMA Ext / Read DMA Ext
    
    fis[4] = (u8)(lba & 0xFF);
    fis[5] = (u8)((lba >> 8) & 0xFF);
    fis[6] = (u8)((lba >> 16) & 0xFF);
    fis[7] = 0x40; // LBA mode
    
    fis[8] = (u8)((lba >> 24) & 0xFF);
    fis[9] = (u8)((lba >> 32) & 0xFF);
    fis[10] = (u8)((lba >> 40) & 0xFF);
    
    fis[12] = (u8)(count & 0xFF);
    fis[13] = (u8)((count >> 8) & 0xFF);
    fis[14] = 0; // Device Control

    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    port->ci = 1; // Issue command slot 0

    while (1) {
        if ((port->ci & 1) == 0) break;
        if (port->is & (1 << 30)) return -1; // Task file error
    }

    if (port->tfd & 0x01) return -1; // Error bit set in status
    return 0;
}

static int ahci_read(block_device_t *dev, u64 lba, u32 count, void *buf) {
    return ahci_read_write(dev, lba, count, buf, false);
}

static int ahci_write(block_device_t *dev, u64 lba, u32 count, const void *buf) {
    return ahci_read_write(dev, lba, count, (void *)buf, true);
}

static void identify_drive(ahci_drive_t *drive) {
    hba_port_t *port = drive->port;
    u16 *buf = (u16 *)kmalloc(512);
    if (!buf) return;
    memset(buf, 0, 512);
    
    u64 phys_buf = vmm_virt_hhdm_to_phys(buf);
    
    ahci_cmd_hdr_t *cmdheader = (ahci_cmd_hdr_t *)vmm_phys_to_virt(((u64)port->clbu << 32) | port->clb);
    cmdheader[0].cfl = 5;
    cmdheader[0].w = 0;
    cmdheader[0].prdtl = 1;
    
    ahci_cmd_tbl_t *cmdtbl = (ahci_cmd_tbl_t *)vmm_phys_to_virt(((u64)cmdheader[0].ctbau << 32) | cmdheader[0].ctba);
    memset(cmdtbl, 0, sizeof(ahci_cmd_tbl_t) + sizeof(ahci_prdt_entry_t));
    
    cmdtbl->prdt_entry[0].dba = (u32)(phys_buf & 0xFFFFFFFF);
    cmdtbl->prdt_entry[0].dbau = (u32)(phys_buf >> 32);
    cmdtbl->prdt_entry[0].dbc = 511; // 512 bytes
    cmdtbl->prdt_entry[0].i = 1;

    u8 *fis = cmdtbl->cfis;
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = 0xEC; // IDENTIFY DEVICE
    
    port->ci = 1;
    while (port->ci & 1);
    
    u64 sectors = *((u64 *)&buf[100]);
    if (sectors == 0) sectors = *((u32 *)&buf[60]); // fallback
    
    drive->bdev.sector_count = sectors;
    kfree(buf);
}

static void ahci_init_device(pci_device_t *dev) {
    pci_bar_t bar;
    if (!pci_read_bar(dev, 5, &bar)) {
        kprintf("[AHCI] Failed to read ABAR from PCI 00:%02x.%u\n", dev->slot, dev->func);
        return;
    }
    
    // Enable bus mastering and MMIO
    pci_enable_bus_master(dev);
    pci_enable_mmio(dev);
    
    u64 abar_virt = (u64)vmm_phys_to_virt(bar.base);
    hba_mem_t *hba = (hba_mem_t *)abar_virt;
    
    // Enable AHCI mode and global interrupts
    hba->ghc |= (1 << 31); // AE (AHCI Enable)
    
    u32 pi = hba->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            int type = check_type(&hba->ports[i]);
            if (type == 1) { // SATA
                kprintf_color(0xFF00FF88, "[AHCI] SATA drive found on port %d\n", i);
                if (ahci_port_rebase(&hba->ports[i])) {
                    ahci_drive_t *drive = (ahci_drive_t *)kmalloc(sizeof(ahci_drive_t));
                    memset(drive, 0, sizeof(ahci_drive_t));
                    drive->pci_dev = dev;
                    drive->abar = hba;
                    drive->port_no = i;
                    drive->port = &hba->ports[i];
                    
                    identify_drive(drive);
                    
                    ksnprintf(drive->bdev.name, sizeof(drive->bdev.name), "ahci%d", i);
                    drive->bdev.kind = BLOCK_DEVICE_AHCI;
                    drive->bdev.sector_size = 512;
                    drive->bdev.read_only = false;
                    drive->bdev.driver_data = drive;
                    drive->bdev.read = ahci_read;
                    drive->bdev.write = ahci_write;
                    drive->bdev.flush = NULL;
                    
                    block_register(drive->bdev);
                    kprintf_color(0xFF00FF88, "[AHCI] Registered block device %s (%lu sectors)\n",
                                  drive->bdev.name, drive->bdev.sector_count);
                }
            } else if (type == 2) {
                kprintf("[AHCI] SATAPI drive found on port %d (unsupported)\n", i);
            }
        }
    }
}

void ahci_init_all(void) {
    u32 count = pci_device_count();
    for (u32 i = 0; i < count; i++) {
        pci_device_t *dev = pci_device_at(i);
        if (dev->class_id == 0x01 && dev->subclass_id == 0x06) {
            ahci_init_device(dev);
        }
    }
}
