/* ============================================================================
 * YamKernel — Intel e1000 Gigabit Ethernet Driver
 * ============================================================================ */
#include "e1000.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"
#include "../bus/pci.h"
#include "../../mem/vmm.h"
#include "../../mem/pmm.h"
#include "../../net/net.h"

static void *e1000_mmio_base;
static u8 e1000_mac[6];

#define E1000_RX_DESC_COUNT 32
#define E1000_TX_DESC_COUNT 8
#define E1000_RX_BUF_SIZE   2048
#define E1000_TX_BUF_SIZE   2048

static e1000_rx_desc_t *rx_descs;
static e1000_tx_desc_t *tx_descs;
static u8 *rx_bufs[E1000_RX_DESC_COUNT];
static u64 rx_buf_phys[E1000_RX_DESC_COUNT];
static u8 *tx_bufs[E1000_TX_DESC_COUNT];
static u64 tx_buf_phys[E1000_TX_DESC_COUNT];
static u32 rx_cur;
static u32 tx_cur;
static bool e1000_ready;
static bool e1000_tx_enabled;

static void e1000_write(u16 offset, u32 val) {
    *(volatile u32 *)((u64)e1000_mmio_base + offset) = val;
    (void)*(volatile u32 *)((u64)e1000_mmio_base + E1000_REG_STATUS);
}

static u32 e1000_read(u16 offset) {
    return *(volatile u32 *)((u64)e1000_mmio_base + offset);
}

static void e1000_send_packet(const void *buf, usize len) {
    if (!e1000_ready || !buf || len == 0) return;
    if (!e1000_tx_enabled) {
        kprintf("[e1000] TX not enabled yet; packet queued/drop len=%lu\n", len);
        return;
    }
    if (len > E1000_TX_BUF_SIZE) {
        kprintf("[e1000] TX drop oversized packet len=%lu\n", len);
        return;
    }

    u32 idx = tx_cur;
    if (!(tx_descs[idx].status & E1000_TX_STATUS_DD)) {
        kprintf("[e1000] TX ring full/drop idx=%u len=%lu\n", idx, len);
        return;
    }

    memcpy(tx_bufs[idx], buf, len);
    tx_descs[idx].addr = tx_buf_phys[idx];
    tx_descs[idx].length = (u16)len;
    tx_descs[idx].cso = 0;
    tx_descs[idx].cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    tx_descs[idx].status = 0;
    tx_descs[idx].css = 0;
    tx_descs[idx].special = 0;
    __asm__ volatile("" ::: "memory");

    tx_cur = (tx_cur + 1) % E1000_TX_DESC_COUNT;
    e1000_write(E1000_REG_TXDESCTAIL, tx_cur);

    for (int i = 0; i < 100000; i++) {
        if (tx_descs[idx].status & E1000_TX_STATUS_DD) return;
        __asm__ volatile("pause");
    }
    kprintf("[e1000] TX timeout idx=%u len=%lu\n", idx, len);
}

static bool e1000_alloc_rings(void) {
    u64 rx_desc_phys = pmm_alloc_page();
    u64 tx_desc_phys = pmm_alloc_page();
    if (!rx_desc_phys || !tx_desc_phys) return false;
    rx_descs = (e1000_rx_desc_t *)vmm_phys_to_virt(rx_desc_phys);
    tx_descs = (e1000_tx_desc_t *)vmm_phys_to_virt(tx_desc_phys);
    memset(rx_descs, 0, 4096);
    memset(tx_descs, 0, 4096);

    for (u32 i = 0; i < E1000_RX_DESC_COUNT; i++) {
        rx_buf_phys[i] = pmm_alloc_page();
        if (!rx_buf_phys[i]) return false;
        rx_bufs[i] = (u8 *)vmm_phys_to_virt(rx_buf_phys[i]);
        memset(rx_bufs[i], 0, E1000_RX_BUF_SIZE);
        rx_descs[i].addr = rx_buf_phys[i];
        rx_descs[i].status = 0;
    }
    for (u32 i = 0; i < E1000_TX_DESC_COUNT; i++) {
        tx_buf_phys[i] = pmm_alloc_page();
        if (!tx_buf_phys[i]) return false;
        tx_bufs[i] = (u8 *)vmm_phys_to_virt(tx_buf_phys[i]);
        memset(tx_bufs[i], 0, E1000_TX_BUF_SIZE);
        tx_descs[i].addr = tx_buf_phys[i];
        tx_descs[i].status = E1000_TX_STATUS_DD;
    }

    e1000_write(E1000_REG_RXDESCLO, (u32)(rx_desc_phys & 0xFFFFFFFF));
    e1000_write(E1000_REG_RXDESCHI, (u32)(rx_desc_phys >> 32));
    e1000_write(E1000_REG_RXDESCLEN, E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t));
    e1000_write(E1000_REG_RXDESCHEAD, 0);
    e1000_write(E1000_REG_RXDESCTAIL, E1000_RX_DESC_COUNT - 1);

    e1000_write(E1000_REG_TXDESCLO, (u32)(tx_desc_phys & 0xFFFFFFFF));
    e1000_write(E1000_REG_TXDESCHI, (u32)(tx_desc_phys >> 32));
    e1000_write(E1000_REG_TXDESCLEN, E1000_TX_DESC_COUNT * sizeof(e1000_tx_desc_t));
    e1000_write(E1000_REG_TXDESCHEAD, 0);
    e1000_write(E1000_REG_TXDESCTAIL, 0);

    rx_cur = 0;
    tx_cur = 0;
    return true;
}

static void e1000_enable_rxtx(void) {
    e1000_tx_enabled = false;
    e1000_write(E1000_REG_IMASK, 0);
    e1000_write(E1000_REG_CTRL, e1000_read(E1000_REG_CTRL) | E1000_CTRL_SLU);

    for (u32 i = 0; i < 128; i++) {
        e1000_write(E1000_REG_MTA + i * 4, 0);
    }

    e1000_write(E1000_REG_RDTR, 0);
    e1000_write(E1000_REG_RCTRL,
                (1u << 1)  |
                (1u << 2)  |
                (1u << 3)  |
                (1u << 4)  |
                (1u << 15) |
                (1u << 26));

    e1000_write(E1000_REG_TCTRL,
                (1u << 1) |
                (1u << 3) |
                (15u << 4) |
                (64u << 12));
    e1000_write(E1000_REG_TIPG, 10 | (8 << 10) | (6 << 20));
    e1000_tx_enabled = true;
    kprintf("[e1000] TX enabled TCTL=0x%x STATUS=0x%x link=%s\n",
            e1000_read(E1000_REG_TCTRL),
            e1000_read(E1000_REG_STATUS),
            (e1000_read(E1000_REG_STATUS) & E1000_STATUS_LU) ? "up" : "unknown/down");
}

void e1000_poll(void) {
    if (!e1000_ready || !rx_descs) return;
    for (u32 loops = 0; loops < E1000_RX_DESC_COUNT; loops++) {
        e1000_rx_desc_t *d = &rx_descs[rx_cur];
        if (!(d->status & E1000_RX_STATUS_DD)) return;

        u8 *packet = rx_bufs[rx_cur];
        u16 length = d->length;
        d->status = 0;
        d->errors = 0;
        e1000_write(E1000_REG_RXDESCTAIL, rx_cur);
        rx_cur = (rx_cur + 1) % E1000_RX_DESC_COUNT;

        if (length > 0 && length <= E1000_RX_BUF_SIZE) {
            net_receive(packet, length);
        }
    }
}

void e1000_init(void) {
    kprintf_color(0xFF00DDFF, "[e1000] Scanning PCI bus for Intel 82540EM...\n");
    /* Intel PRO/1000 MT Desktop (82540EM) is commonly 0x100E inside QEMU/VirtualBox */
    pci_device_t *nic = pci_get_device(0x8086, 0x100E);
    if (!nic) {
        kprintf_color(0xFF00DDFF, "[e1000] Intel Gigabit NIC not found.\n");
        return;
    }
    kprintf_color(0xFF00FF88, "[e1000] NIC Found at %02x:%02x.%u\n", nic->bus, nic->slot, nic->func);

    pci_enable_mmio(nic);
    pci_enable_bus_master(nic);
    kprintf_color(0xFF00DDFF, "[e1000] PCI command=0x%04x caps:%s%s\n",
                  pci_command(nic),
                  pci_has_msi(nic) ? " msi" : "",
                  pci_has_msix(nic) ? " msix" : "");

    pci_bar_t bar0;
    if (!pci_read_bar(nic, 0, &bar0) || bar0.type == PCI_BAR_IO || !bar0.base) {
        kprintf_color(0xFFFF0000, "[e1000] ERROR: invalid BAR0\n");
        return;
    }
    u64 phys_addr = bar0.base;
    kprintf_color(0xFF00DDFF, "[e1000] BAR0 MMIO: phys=0x%llx size=0x%llx\n",
                  phys_addr, bar0.size);
    
    if (phys_addr == 0) {
        kprintf_color(0xFFFF0000, "[e1000] ERROR: Invalid Physical Address 0x0!\n");
        return;
    }

    /* Limine's HHDM does NOT necessarily cover PCI MMIO BARs (it maps only up
       to the highest RAM region). Manually map 128 KB at the HHDM virtual.
       vmm_map_page now safely skips ranges already covered by huge pages. */
    e1000_mmio_base = vmm_phys_to_virt(phys_addr);
    u64 *pml4 = vmm_get_kernel_pml4();
    for (u64 i = 0; i < 128 * 1024; i += 4096) {
        vmm_map_page(pml4, (u64)e1000_mmio_base + i, (u64)phys_addr + i,
                     VMM_FLAG_WRITE | VMM_FLAG_NOCACHE);
    }
    /* Flush TLB so the new mappings take effect */
    __asm__ volatile ("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    kprintf_color(0xFF00DDFF, "[e1000] MMIO mapped at 0x%lx (128KB)\n", (u64)e1000_mmio_base);

    /* Read MAC Address from EEPROM/Registers */
    kprintf_color(0xFF00DDFF, "[e1000] Reading MAC Address (EEPROM offset 0x5400)...\n");
    u32 mac_low = e1000_read(E1000_REG_RAL);
    u32 mac_high = e1000_read(E1000_REG_RAH);
    
    e1000_mac[0] = mac_low & 0xFF;
    e1000_mac[1] = (mac_low >> 8) & 0xFF;
    e1000_mac[2] = (mac_low >> 16) & 0xFF;
    e1000_mac[3] = (mac_low >> 24) & 0xFF;
    e1000_mac[4] = mac_high & 0xFF;
    e1000_mac[5] = (mac_high >> 8) & 0xFF;

    kprintf_color(0xFF00FF88, "[e1000] Intel Gigabit NIC Initialized.\n");
    kprintf_color(0xFF00FF88, "[e1000] Hardware MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
                  e1000_mac[0], e1000_mac[1], e1000_mac[2], e1000_mac[3], e1000_mac[4], e1000_mac[5]);

    if (!e1000_alloc_rings()) {
        kprintf_color(0xFFFF0000, "[e1000] ERROR: failed to allocate TX/RX rings\n");
        return;
    }
    e1000_enable_rxtx();
    memcpy(g_net_iface.mac_addr, e1000_mac, 6);
    g_net_iface.is_up = true;
    g_net_iface.send = e1000_send_packet;
    e1000_ready = true;
    kprintf_color(0xFF00FF88, "[e1000] TX/RX rings ready; net interface is up\n");
}
