# AIC8800D80 SDIO Driver Audit
## `OpenCentauri/aic8800d80-sdio` (`sdio-cc2`) vs `LYU4662/aic8800-sdio-linux-1.0` (`6.18`)

> **Purpose:** Agent-actionable fix list. Each task has: file path, exact problem, exact fix (diff or instruction).  
> **Audit date:** 2026-07-03  
> **Our branch:** `sdio-cc2` @ `69a84c94c75318b25a44a2609a56dea7f42aa7e2`  
> **Reference:** LYU4662 `6.18` branch @ `aic8800-sdio-linux-1.0`

---

## CRITICAL — Task 1: `CONFIG_SDIO_SUPPORT` is `n` and `CONFIG_USB_SUPPORT` is `y`

**File:** `drivers/aic8800/aic8800_fdrv/Makefile`  
**SHA:** `335ec5429a14dd7035d2d4b1418779a78913fbab`

### Problem
```makefile
CONFIG_SDIO_SUPPORT =n   # ← SDIO driver is NOT compiled in
CONFIG_USB_SUPPORT =y    # ← USB driver is compiled in
```
This means `aicwf_sdio.c`, `sdio_host.c`, and the SDIO path of `aicwf_txrxif.c` all **compile out**. The `AICWF_SDIO_SUPPORT` macro never gets defined. The LYU4662 6.18 reference has exactly the opposite.

### Fix
Apply this patch to `drivers/aic8800/aic8800_fdrv/Makefile`:
```diff
-CONFIG_SDIO_SUPPORT =n
-CONFIG_USB_SUPPORT =y
+CONFIG_SDIO_SUPPORT =y
+CONFIG_USB_SUPPORT =n
```

---

## CRITICAL — Task 2: USB-specific `ccflags` are emitted unconditionally when SDIO-only

**File:** `drivers/aic8800/aic8800_fdrv/Makefile`

### Problem
Even with `CONFIG_USB_SUPPORT=n`, the Makefile defines these unconditionally:
```makefile
CONFIG_USB_BT=y
CONFIG_USB_ALIGN_DATA = y
CONFIG_USB_MSG_OUT_EP = y
CONFIG_USB_MSG_IN_EP = y
CONFIG_USB_RX_REASSEMBLE = n
CONFIG_USB_RX_AGGR = n
CONFIG_USB_TX_AGGR = n
CONFIG_USB_NO_TRANS_DMA_MAP = n
CONFIG_USE_USB_ZERO_PACKET = y
```
These cause `ccflags-$(CONFIG_USB_BT)`, `ccflags-$(CONFIG_USB_ALIGN_DATA)` etc. to emit `-DCONFIG_USB_BT`, `-DCONFIG_USB_ALIGN_DATA` etc. into the build even for SDIO-only. Those macros guard USB-specific code that will then try to compile with USB headers absent.

The LYU4662 6.18 reference has **none of these** — they do not exist at all in that Makefile.

### Fix
Wrap the entire block in a USB guard in `drivers/aic8800/aic8800_fdrv/Makefile`:
```diff
+ifeq ($(CONFIG_USB_SUPPORT), y)
 CONFIG_USB_BT=y
 CONFIG_USB_ALIGN_DATA = y
 CONFIG_USB_MSG_OUT_EP = y
 CONFIG_USB_MSG_IN_EP = y
 CONFIG_USB_RX_REASSEMBLE = n
 CONFIG_USB_RX_AGGR = n
 CONFIG_USB_TX_AGGR = n
 CONFIG_USB_NO_TRANS_DMA_MAP = n
 CONFIG_USE_USB_ZERO_PACKET = y
+endif
```

---

## CRITICAL — Task 3: `aicwf_txrxif.o` is in both SDIO and USB link lists (double-link bomb)

**File:** `drivers/aic8800/aic8800_fdrv/Makefile`

### Problem
```makefile
$(MODULE_NAME)-$(CONFIG_SDIO_SUPPORT)  += aicwf_txrxif.o
...
$(MODULE_NAME)-$(CONFIG_USB_SUPPORT)   += aicwf_txrxif.o
```
If both flags are ever `y`, `aicwf_txrxif.o` is linked twice → duplicate symbol linker error. The LYU4662 6.18 Makefile has the same pattern, but since they never enable both simultaneously (and neither should we), this is a latent bug. It should be guarded.

### Fix
Replace the two conditional lines with a single safe guard:
```diff
-$(MODULE_NAME)-$(CONFIG_SDIO_SUPPORT)  += aicwf_txrxif.o
 ...
-$(MODULE_NAME)-$(CONFIG_USB_SUPPORT)   += aicwf_txrxif.o
+# aicwf_txrxif.o is shared; link exactly once for whichever transport is active
+ifeq ($(CONFIG_SDIO_SUPPORT), y)
+$(MODULE_NAME)-y += aicwf_txrxif.o
+else ifeq ($(CONFIG_USB_SUPPORT), y)
+$(MODULE_NAME)-y += aicwf_txrxif.o
+endif
```

---

## CRITICAL — Task 4: Missing `CONFIG_SDIO_PWRCTRL` flag — power control is silently disabled

**File:** `drivers/aic8800/aic8800_fdrv/Makefile`

### Problem
LYU4662 6.18 has:
```makefile
CONFIG_SDIO_PWRCTRL ?= y
...
ifeq ($(CONFIG_SDIO_SUPPORT), y)
ccflags-y += -DAICWF_SDIO_SUPPORT
ccflags-$(CONFIG_SDIO_PWRCTRL) += -DCONFIG_SDIO_PWRCTRL
endif
```

Our Makefile has no `CONFIG_SDIO_PWRCTRL` at all. The power control thread (`aicwf_sdio_pwrctl_thread`), timer (`aicwf_sdio_bus_pwrctl`), wakeup/sleep sequences, and `aicwf_sdio_pwr_stctl()` are all gated on `#ifdef CONFIG_SDIO_PWRCTRL` in the LYU4662 source. Without this flag, power management is compiled out.

Inspection of our `aicwf_sdio.c` shows these functions **are present** in the source, but since the macro is never defined they may be excluded or produce inconsistent behaviour depending on how guards are applied.

### Fix
Add to `drivers/aic8800/aic8800_fdrv/Makefile`, near the existing SDIO support block:
```diff
+CONFIG_SDIO_PWRCTRL ?= y
+
 ifeq ($(CONFIG_SDIO_SUPPORT), y)
 ccflags-y += -DAICWF_SDIO_SUPPORT
+ccflags-$(CONFIG_SDIO_PWRCTRL) += -DCONFIG_SDIO_PWRCTRL
 endif
```

Then audit `aicwf_sdio.c` for any `#ifdef CONFIG_SDIO_PWRCTRL` guards and ensure all power control functions are inside them (or remove guards if always-on is desired for CC2).

---

## HIGH — Task 5: `CONFIG_FDRV_NO_REG_SDIO` is missing — no way to skip SDIO auto-register

**File:** `drivers/aic8800/aic8800_fdrv/Makefile`

### Problem
LYU4662 6.18 has:
```makefile
CONFIG_FDRV_NO_REG_SDIO=n
...
ccflags-$(CONFIG_FDRV_NO_REG_SDIO) += -DCONFIG_FDRV_NO_REG_SDIO
```
This flag lets the BSP control whether the SDIO driver auto-registers at `module_init` or defers to an external trigger (e.g., after GPIO power sequencing). On Allwinner R528, WiFi power-on via `sunxi_wlan_set_power()` must happen *before* SDIO enumeration. Without this flag there is no way to defer registration.

Our Makefile has no such flag.

### Fix
Add to `drivers/aic8800/aic8800_fdrv/Makefile`:
```diff
+CONFIG_FDRV_NO_REG_SDIO ?= n
 ...
+ccflags-$(CONFIG_FDRV_NO_REG_SDIO) += -DCONFIG_FDRV_NO_REG_SDIO
```

---

## HIGH — Task 6: Missing `CONFIG_OOB` out-of-band IRQ flag

**File:** `drivers/aic8800/aic8800_fdrv/Makefile`

### Problem
LYU4662 6.18 has:
```makefile
CONFIG_OOB = n
...
ccflags-$(CONFIG_OOB) += -DCONFIG_OOB
```
OOB (Out-of-Band) IRQ is used on platforms where SDIO in-band interrupts are unreliable or unavailable. On R528, depending on the board design, OOB via a dedicated GPIO may be required. Absence of this flag means the option cannot be enabled without modifying source.

### Fix
Add to `drivers/aic8800/aic8800_fdrv/Makefile`:
```diff
+CONFIG_OOB ?= n
 ...
+ccflags-$(CONFIG_OOB) += -DCONFIG_OOB
```

---

## HIGH — Task 7: `CONFIG_RESV_MEM_SUPPORT` missing — RX buffer pre-allocation unavailable

**File:** `drivers/aic8800/aic8800_fdrv/Makefile`

### Problem
LYU4662 6.18 has `CONFIG_RESV_MEM_SUPPORT = y` and `ccflags-$(CONFIG_RESV_MEM_SUPPORT) += -DCONFIG_RESV_MEM_SUPPORT`. This enables a reserved memory pool for RX SKBs, which is important on embedded targets with limited DMA-coherent memory (like R528).

### Fix
Add to `drivers/aic8800/aic8800_fdrv/Makefile`:
```diff
+CONFIG_RESV_MEM_SUPPORT ?= y
 ...
+ccflags-$(CONFIG_RESV_MEM_SUPPORT) += -DCONFIG_RESV_MEM_SUPPORT
```
Then verify `aicwf_rx_prealloc.c`/`.h` is present and linked when `CONFIG_RESV_MEM_SUPPORT=y`. Note: LYU4662 links `aicwf_rx_prealloc.o` unconditionally in the base `$(MODULE_NAME)-y` list. Our repo has `aicwf_rx_prealloc.h` present (SHA `dc805a53`) but no `aicwf_rx_prealloc.c`. **Either add `aicwf_rx_prealloc.c` from LYU4662, or set `CONFIG_RESV_MEM_SUPPORT=n` explicitly until the source is ported.**

---

## HIGH — Task 8: Firmware path is hardcoded as Android `/vendor/etc/firmware`

**File:** `drivers/aic8800/aic8800_fdrv/Makefile`

### Problem
```makefile
CONFIG_AIC_FW_PATH = "/vendor/etc/firmware"
export CONFIG_AIC_FW_PATH
```
This is an Android path. Yocto/Linux standard firmware path is `/lib/firmware`. The LYU4662 6.18 Makefile does **not** set `CONFIG_AIC_FW_PATH` here at all — it is instead passed via `ccflags-y` from a parent `Makefile` or Yocto variable:
```makefile
ccflags-y += -DCONFIG_AIC_FW_PATH=\"$(CONFIG_AIC_FW_PATH)\"
```

### Fix
In `drivers/aic8800/aic8800_fdrv/Makefile`:
```diff
-CONFIG_AIC_FW_PATH = "/vendor/etc/firmware"
-export CONFIG_AIC_FW_PATH
+CONFIG_AIC_FW_PATH ?= "/lib/firmware"
+ccflags-y += -DCONFIG_AIC_FW_PATH=\"$(CONFIG_AIC_FW_PATH)\"
```
This allows Yocto `EXTRA_OEMAKE` or `kernel.bbclass` to override: `CONFIG_AIC_FW_PATH="/lib/firmware/aic8800"`.

---

## HIGH — Task 9: `aicwf_sdio_func_init()` calls `set_ios` directly — illegal on 6.x kernels

**File:** `drivers/aic8800/aic8800_fdrv/aicwf_sdio.c`  
**SHA:** `5353c597fe792dcc98c26d6406e37e0f778cd65f`

### Problem
```c
host->ios.clock = 60000000;
host->ops->set_ios(host, &host->ios);
```
Directly calling `host->ops->set_ios()` bypasses the MMC core clock management and is **not allowed** in kernel 6.x — `mmc_set_clock()` or `mmc_set_ios()` must be used instead, and they require the host lock. LYU4662's 6.18 branch **does not have this direct `set_ios` call** — it is absent from their `aicwf_sdio.c`.

This will cause a kernel WARN or BUG on 6.18 when `sdio_enable_func` is called without proper host claim around the clock change.

### Fix
In `drivers/aic8800/aic8800_fdrv/aicwf_sdio.c`, inside `aicwf_sdio_func_init()`, remove the direct clock setting:
```diff
     ret = sdio_enable_func(sdiodev->func);
     if (ret < 0) {
         sdio_release_host(sdiodev->func);
         sdio_err("enable func fail %d.\n", ret);
     }
-
-    host->ios.clock = 60000000;
-    host->ops->set_ios(host, &host->ios);
     sdio_release_host(sdiodev->func);
-    sdio_dbg("Set SDIO Clock %d MHz\n",host->ios.clock/1000000);
```
The SDIO clock is managed by the MMC core after `sdio_enable_func`. If a specific clock rate is needed, use `mmc_set_clock(host, 50000000)` inside `sdio_claim_host`/`sdio_release_host`, or configure it via Device Tree `max-frequency` on the SDIO host node.

---

## HIGH — Task 10: `sdio_set_block_size` is called BEFORE `sdio_enable_func`

**File:** `drivers/aic8800/aic8800_fdrv/aicwf_sdio.c`

### Problem
Our `aicwf_sdio_func_init()` sequence:
```c
sdio_claim_host(sdiodev->func);
ret = sdio_set_block_size(sdiodev->func, SDIOWIFI_FUNC_BLOCKSIZE);  // ← BEFORE enable
...
ret = sdio_enable_func(sdiodev->func);
```
Per the SDIO spec and Linux MMC subsystem: **`sdio_enable_func` must be called before `sdio_set_block_size`**. Setting block size on a disabled function results in undefined behaviour on many controllers. LYU4662's `sdio_host.c` does this correctly: `enable_func` first, then `set_block_size`.

### Fix
In `drivers/aic8800/aic8800_fdrv/aicwf_sdio.c`, swap the order in `aicwf_sdio_func_init()`:
```diff
 sdio_claim_host(sdiodev->func);
-ret = sdio_set_block_size(sdiodev->func, SDIOWIFI_FUNC_BLOCKSIZE);
-if (ret < 0) {
-    sdio_err("set blocksize fail %d\n", ret);
-    sdio_release_host(sdiodev->func);
-    return ret;
-}
 ret = sdio_enable_func(sdiodev->func);
 if (ret < 0) {
     sdio_release_host(sdiodev->func);
     sdio_err("enable func fail %d.\n", ret);
+    return ret;
 }
+ret = sdio_set_block_size(sdiodev->func, SDIOWIFI_FUNC_BLOCKSIZE);
+if (ret < 0) {
+    sdio_err("set blocksize fail %d\n", ret);
+    sdio_release_host(sdiodev->func);
+    return ret;
+}
 sdio_release_host(sdiodev->func);
```

---

## HIGH — Task 11: `timer_container_of` guard for kernel ≥ 6.16 is missing `from_timer` fallback completeness

**File:** `drivers/aic8800/aic8800_fdrv/aicwf_sdio.c`

### Problem
Our code has:
```c
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
static void aicwf_sdio_bus_pwrctl(ulong data)
#else
static void aicwf_sdio_bus_pwrctl(struct timer_list *t)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
    struct aic_sdio_dev *sdiodev = (struct aic_sdio_dev *) data;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 16, 0)
    struct aic_sdio_dev *sdiodev = timer_container_of(sdiodev, t, timer);
#else
    struct aic_sdio_dev *sdiodev = from_timer(sdiodev, t, timer);
#endif
```
`timer_container_of` was introduced in 6.16 to replace `from_timer`. The guard `>= 6.16.0` is correct. However this is actually one of the few places where our port is **ahead** of LYU4662's 6.18 branch (which does not yet have the 6.16 guard). Verify this compiles cleanly on 6.18 — `from_timer` still exists in 6.18 as an alias, so both should work, but the `timer_container_of` path should be tested.

### Fix (Verification only)
Confirm by building with `LINUX_VERSION_CODE` matching kernel 6.18 (0x060120). No code change needed unless build fails, in which case add:
```c
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 16, 0)
    struct aic_sdio_dev *sdiodev = timer_container_of(sdiodev, t, timer);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
    struct aic_sdio_dev *sdiodev = from_timer(sdiodev, t, timer);
#else
    struct aic_sdio_dev *sdiodev = (struct aic_sdio_dev *) data;
#endif
```

---

## HIGH — Task 12: `aicwf_sdio.h` — `CONFIG_PLATFORM_NANOPI` extern guard is dead code; no Allwinner fallback

**File:** `drivers/aic8800/aic8800_fdrv/aicwf_sdio.h`  
**SHA:** `296a6c05afca8aaac0b6181f6054870de72561f4`

### Problem
```c
#ifdef CONFIG_PLATFORM_NANOPI
extern void extern_wifi_set_enable(int is_on);
extern void sdio_reinit(void);
#endif
```
`CONFIG_PLATFORM_NANOPI` is never defined in our Makefile. Meanwhile, `aicwf_sdio.c` calls `extern_wifi_set_enable(0/1)` and `sdio_reinit()` inside `aicwf_sdio_register()` under `#ifdef CONFIG_PLATFORM_NANOPI` — which also never fires. 

But our `aicwf_sdio.c` **already correctly** implements `platform_wifi_power_on()` and `platform_wifi_power_off()` under `#ifdef CONFIG_PLATFORM_ALLWINNER` using real Sunxi BSP calls. The `aicwf_sdio_register()` function then uses `#ifdef CONFIG_PLATFORM_ALLWINNER` to call `platform_wifi_power_on()`. This is correct.

However, `CONFIG_PLATFORM_ALLWINNER` is also not set in the Makefile, so even the Allwinner power-on path does not fire.

### Fix
Add to `drivers/aic8800/aic8800_fdrv/Makefile` under the platform block:
```diff
+CONFIG_PLATFORM_ALLWINNER_R528 ?= n
+
+ifeq ($(CONFIG_PLATFORM_ALLWINNER_R528), y)
+CONFIG_PLATFORM_ALLWINNER = y
+ARCH ?= arm
+CROSS_COMPILE ?= arm-linux-gnueabihf-
+KDIR ?= $(KERNEL_SRC)
+ccflags-y += -DCONFIG_PLATFORM_ALLWINNER
+endif
```
For Yocto builds, `KERNEL_SRC` is set by `kernel.bbclass`. Pass `CONFIG_PLATFORM_ALLWINNER_R528=y` via `EXTRA_OEMAKE` in the recipe.

---

## HIGH — Task 13: `aicwf_sdio_probe` hardcodes `chipid = PRODUCT_ID_AIC8800D80N` without validation

**File:** `drivers/aic8800/aic8800_fdrv/aicwf_sdio.c`

### Problem
```c
/* ponytail: CC2 ships D80N today; fail fast on the vendor, assume family here. */
sdiodev->chipid = PRODUCT_ID_AIC8800D80N;
dev_info(&func->dev, "probing SDIO %04x:%04x as AIC8800D80N\n",
         func->vendor, func->device);
```
This is intentional for CC2 but is a permanent divergence from upstream. If `func->device` does not match any known D80N SDIO device ID, the driver still proceeds. The SDIO device ID table uses `SDIO_DEVICE_CLASS(SDIO_CLASS_WLAN)` which matches **any** WLAN SDIO device — not just AIC chips.

LYU4662 detects chip ID by reading registers after probe. Our shortcut is pragmatic but should at minimum validate `func->vendor`.

### Fix
The existing vendor check is already there:
```c
if (func->vendor != SDIO_VENDOR_ID_AIC) {
    dev_err(&func->dev, "unsupported SDIO device %04x:%04x\n", ...);
    return err;
}
```
This is correct. Add device ID matching to make the probe more precise and avoid attaching to unrelated WLAN SDIO devices:
```diff
-static const struct sdio_device_id aicwf_sdmmc_ids[] = {
-    {SDIO_DEVICE_CLASS(SDIO_CLASS_WLAN)},
-    { },
-};
+static const struct sdio_device_id aicwf_sdmmc_ids[] = {
+    { SDIO_DEVICE(SDIO_VENDOR_ID_AIC, SDIO_DEVICE_ID_AIC) },
+    { SDIO_DEVICE_CLASS(SDIO_CLASS_WLAN) },  /* fallback */
+    { },
+};
```
Where `SDIO_VENDOR_ID_AIC = 0x8800` and `SDIO_DEVICE_ID_AIC = 0x0001` (already defined in `aicwf_sdio.h`).

---

## MEDIUM — Task 14: Kconfig has no `depends on MMC` — in-tree builds miss SDIO dependency

**File:** `drivers/aic8800/aic8800_fdrv/Kconfig`  
**SHA:** `92d5986816937d31b2f283943b98fb26a4217c5d`

### Problem
The Kconfig is a one-option stub with no `depends on MMC`. For standalone DKMS builds (Ubuntu) this doesn't matter because MMC is already compiled in. But for Yocto in-tree builds, `select` or `depends` is needed to ensure MMC/SDIO is available.

### Fix
Replace `drivers/aic8800/aic8800_fdrv/Kconfig` with:
```kconfig
config AIC8800_WLAN_SUPPORT
	tristate "AIC8800 WiFi driver"
	depends on MMC
	select WIRELESS_EXT
	select WEXT_PRIV
	help
	  Out-of-tree driver for AIC8800D80/D80N WiFi over SDIO.
	  Requires MMC/SDIO host controller support (CONFIG_MMC).
```

---

## MEDIUM — Task 15: `aicwf_sdio.c` — `#ifdef AICWF_SDIO_SUPPORT` guard around `pwrctl_tsk` kthread start is redundant and confusing

**File:** `drivers/aic8800/aic8800_fdrv/aicwf_sdio.c`

### Problem
Inside `aicwf_sdio_bus_init()`:
```c
#ifdef AICWF_SDIO_SUPPORT
    sdiodev->pwrctl_tsk = kthread_run(aicwf_sdio_pwrctl_thread, sdiodev, "aicwf_pwrctl");
#endif
```
This file is **only compiled** when `AICWF_SDIO_SUPPORT` is defined (via `$(MODULE_NAME)-$(CONFIG_SDIO_SUPPORT) += aicwf_sdio.o`). So the `#ifdef AICWF_SDIO_SUPPORT` guard inside `aicwf_sdio.c` is always true and adds no protection — it only obscures the intent. It also means if `CONFIG_SDIO_PWRCTRL` is ever used as the guard (as LYU4662 does), this code will be incorrect.

### Fix
In `drivers/aic8800/aic8800_fdrv/aicwf_sdio.c`, remove the redundant inner guard and optionally add the `CONFIG_SDIO_PWRCTRL` check:
```diff
-#ifdef AICWF_SDIO_SUPPORT
+#ifdef CONFIG_SDIO_PWRCTRL
     sdiodev->pwrctl_tsk = kthread_run(aicwf_sdio_pwrctl_thread, sdiodev, "aicwf_pwrctl");
-#endif
+#else
+    sdiodev->pwrctl_tsk = NULL;
+#endif
```

---

## MEDIUM — Task 16: `aicwf_sdio.c` — `timer_delete_sync` not available in 6.x < 6.15; `del_timer_sync` needed

**File:** `drivers/aic8800/aic8800_fdrv/aicwf_sdio.c`

### Problem
```c
if (timer_pending(&sdiodev->timer))
    timer_delete_sync(&sdiodev->timer);
```
`timer_delete_sync()` was only introduced as a replacement for `del_timer_sync()` in kernel 6.15+. On kernel 6.18 it exists, but on 6.7–6.14 it does not. Since LYU4662 targets 6.18 and uses `del_timer_sync`, and our target is also 6.18, this is not a current break — but it should be guarded for forward compatibility. Also used in `aicwf_sdio_pwrctl_timer()`:
```c
timer_delete_sync(&sdiodev->timer);
```

### Fix
In `drivers/aic8800/aic8800_fdrv/rwnx_compat.h` (or at the top of `aicwf_sdio.c`), add:
```c
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
#define timer_delete_sync(t)  del_timer_sync(t)
#define timer_delete(t)       del_timer(t)
#endif
```
This is purely defensive but ensures the driver builds on kernels below 6.15 without modification.

---

## MEDIUM — Task 17: `aicwf_sdio.c` — `#ifndef CONFIG_PLATFORM_ALLWINNER` suppresses "Interrupt but no data" log inappropriately

**File:** `drivers/aic8800/aic8800_fdrv/aicwf_sdio.c`

### Problem
```c
} else {
#ifndef CONFIG_PLATFORM_ALLWINNER
    sdio_err("Interrupt but no data\n");
#endif
}
```
Suppressing the error log on Allwinner means spurious interrupts are silently ignored. This is a workaround for a known Sunxi SDIO controller quirk (false CD-detect interrupts during power transitions) but makes debugging harder. A rate-limited log would be better.

### Fix
```diff
 } else {
-#ifndef CONFIG_PLATFORM_ALLWINNER
-    sdio_err("Interrupt but no data\n");
-#endif
+    /* Allwinner SDIO may fire spurious interrupts during power transitions */
+    sdio_dbg("Interrupt but no data (intstatus=0)\n");
 }
```
Use `sdio_dbg` (which maps to `pr_debug` and is normally silent) rather than conditionally suppressing `sdio_err`.

---

## MEDIUM — Task 18: `rwnx_platform.c` — `aicwf_usb_register` / `aicwf_usb_exit` must be guarded

**File:** `drivers/aic8800/aic8800_fdrv/rwnx_platform.c`  
**SHA:** `90b67c2d4abe3b2b902f71d86be83ee7f0dafd96` (156 KB)

### Problem
Our `rwnx_platform.c` is 156 KB vs LYU4662's 77 KB — approximately double. The extra code is the USB path. The `module_init` / `module_exit` callsites for `aicwf_usb_register()` and `aicwf_usb_exit()` must be inside `#ifdef AICWF_USB_SUPPORT`. If they are not, they will generate unresolved symbol errors when building SDIO-only (since `aicwf_usb.c` is not linked).

### Fix
Run on the sdio-cc2 branch:
```bash
grep -n "aicwf_usb_register\|aicwf_usb_exit\|aicwf_sdio_register\|aicwf_sdio_exit" \
    drivers/aic8800/aic8800_fdrv/rwnx_platform.c
```
For every `aicwf_usb_*` call found **outside** an `#ifdef AICWF_USB_SUPPORT` block, wrap it:
```c
#ifdef AICWF_USB_SUPPORT
    aicwf_usb_register();
#endif
#ifdef AICWF_SDIO_SUPPORT
    aicwf_sdio_register();
#endif
```

---

## MEDIUM — Task 19: `aicwf_txrxif.c` — audit for unguarded `usbdev` references

**File:** `drivers/aic8800/aic8800_fdrv/aicwf_txrxif.c`  
**SHA:** `afc56b7f08d9fd8dac396fdaa1fc2a31ea51e3c7` (41,616 bytes vs LYU4662's ~21 KB)

### Problem
Our `aicwf_txrxif.c` is nearly double the size of LYU4662's. The extra ~20 KB is USB-path code that must be guarded by `#ifdef AICWF_USB_SUPPORT`. The original shenmintao base had 96 unguarded `usbdev` references — confirm all have been wrapped.

### Fix
Run:
```bash
grep -n "usbdev\|usb_submit_urb\|usb_alloc_urb\|usb_free_urb\|aicwf_usb_dev\|struct aicwf_usb" \
    drivers/aic8800/aic8800_fdrv/aicwf_txrxif.c
```
Every match must be inside `#ifdef AICWF_USB_SUPPORT ... #endif`. Any that are not — wrap them. Use LYU4662's `aicwf_txrxif.c` (SDIO-only, 21 KB) as the reference for what a clean SDIO-only version looks like.

---

## LOW — Task 20: Platform Makefile defaults to `CONFIG_PLATFORM_UBUNTU=y` — wrong for Yocto cross-compile

**File:** `drivers/aic8800/aic8800_fdrv/Makefile`

### Problem
```makefile
CONFIG_PLATFORM_UBUNTU ?= y
```
On a Yocto cross-compile host, `uname -r` returns the **host** kernel version, not the target. `KDIR` then points to the host kernel build tree, producing wrong-arch modules.

LYU4662 has the same default (also `CONFIG_PLATFORM_UBUNTU ?= y`) but since they're DKMS-focused it's appropriate for them. For our embedded/Yocto use this is wrong.

### Fix
Add an Allwinner/Yocto platform target and remove Ubuntu as the default:
```diff
+CONFIG_PLATFORM_ALLWINNER_R528 ?= n
 CONFIG_PLATFORM_ROCKCHIP ?= n
 CONFIG_PLATFORM_ALLWINNER ?= n
 CONFIG_PLATFORM_AMLOGIC ?= n
 CONFIG_PLATFORM_HI ?= n
-CONFIG_PLATFORM_UBUNTU ?= y
+CONFIG_PLATFORM_UBUNTU ?= n

+ifeq ($(CONFIG_PLATFORM_ALLWINNER_R528), y)
+ARCH ?= arm
+CROSS_COMPILE ?= arm-linux-gnueabihf-
+KDIR ?= $(KERNEL_SRC)
+ccflags-y += -DCONFIG_PLATFORM_ALLWINNER
+endif
```
In Yocto recipe: `EXTRA_OEMAKE += "CONFIG_PLATFORM_ALLWINNER_R528=y"`.

---

## LOW — Task 21: `NX_REMOTE_STA_MAX_FOR_OLD_IC` differs: ours is 8, LYU4662 is 10

**File:** `drivers/aic8800/aic8800_fdrv/Makefile`

### Problem
```makefile
# Ours:
ccflags-y += -DNX_REMOTE_STA_MAX_FOR_OLD_IC=8

# LYU4662 6.18:
ccflags-y += -DNX_REMOTE_STA_MAX_FOR_OLD_IC=10
```
This controls the remote station table size for older IC variants (8800D, 8800DCDW u01). Using 8 matches older vendor trees; LYU4662 bumped it to 10. Both values are valid — the firmware must be compiled with the matching value. Confirm with the D80N firmware binary being used on CC2.

### Fix
If using the same firmware as LYU4662's H618 target, update to match:
```diff
-ccflags-y += -DNX_REMOTE_STA_MAX_FOR_OLD_IC=8
+ccflags-y += -DNX_REMOTE_STA_MAX_FOR_OLD_IC=10
```
Otherwise leave at 8 and document the firmware expectation.

---

## Summary Table

| # | Priority | File | Issue | Action |
|---|----------|------|-------|--------|
| 1 | **CRITICAL** | `Makefile` | `CONFIG_SDIO_SUPPORT=n` — SDIO never compiles | Flip to `y`, USB to `n` |
| 2 | **CRITICAL** | `Makefile` | USB `ccflags` emitted unconditionally | Wrap in `ifeq USB_SUPPORT y` |
| 3 | **CRITICAL** | `Makefile` | `aicwf_txrxif.o` double-linked | Single conditional link |
| 4 | **CRITICAL** | `Makefile` | `CONFIG_SDIO_PWRCTRL` missing — power mgmt disabled | Add flag + `ccflags` |
| 5 | **HIGH** | `Makefile` | `CONFIG_FDRV_NO_REG_SDIO` missing | Add flag |
| 6 | **HIGH** | `Makefile` | `CONFIG_OOB` missing | Add flag |
| 7 | **HIGH** | `Makefile` + source | `CONFIG_RESV_MEM_SUPPORT` missing; `aicwf_rx_prealloc.c` absent | Add flag; port or disable |
| 8 | **HIGH** | `Makefile` | Firmware path hardcoded as Android `/vendor/etc/firmware` | Change to `?= /lib/firmware` |
| 9 | **HIGH** | `aicwf_sdio.c` | Direct `host->ops->set_ios()` call — illegal on 6.x | Remove; let MMC core manage clock |
| 10 | **HIGH** | `aicwf_sdio.c` | `set_block_size` before `enable_func` | Swap order |
| 11 | **HIGH** | `aicwf_sdio.c` | `timer_container_of` guard — verify builds on 6.18 | Verify; restructure guards |
| 12 | **HIGH** | `aicwf_sdio.h` + `Makefile` | `CONFIG_PLATFORM_ALLWINNER` never defined | Add `CONFIG_PLATFORM_ALLWINNER_R528` to Makefile |
| 13 | **HIGH** | `aicwf_sdio.c` | SDIO device ID table uses class-match only | Add vendor+device exact match |
| 14 | **MEDIUM** | `Kconfig` | No `depends on MMC` | Add depends |
| 15 | **MEDIUM** | `aicwf_sdio.c` | Redundant `#ifdef AICWF_SDIO_SUPPORT` inside SDIO-only file | Replace with `CONFIG_SDIO_PWRCTRL` |
| 16 | **MEDIUM** | `aicwf_sdio.c` | `timer_delete_sync` / `timer_delete` not in < 6.15 | Add compat shims to `rwnx_compat.h` |
| 17 | **MEDIUM** | `aicwf_sdio.c` | Spurious IRQ log silenced with `#ifndef ALLWINNER` | Use `sdio_dbg` instead |
| 18 | **MEDIUM** | `rwnx_platform.c` | `aicwf_usb_register/exit` must be guarded | Wrap in `#ifdef AICWF_USB_SUPPORT` |
| 19 | **MEDIUM** | `aicwf_txrxif.c` | Unguarded `usbdev` refs from shenmintao base | Audit and wrap all USB refs |
| 20 | **LOW** | `Makefile` | `CONFIG_PLATFORM_UBUNTU=y` default breaks Yocto | Add R528 target; remove Ubuntu default |
| 21 | **LOW** | `Makefile` | `NX_REMOTE_STA_MAX_FOR_OLD_IC=8` vs LYU4662's `10` | Verify firmware expectation; update if needed |

---

## Reference SHAs

| File | Repo | Branch | SHA |
|------|------|--------|-----|
| `drivers/aic8800/aic8800_fdrv/Makefile` | OpenCentauri | `sdio-cc2` | `335ec5429a14dd7035d2d4b1418779a78913fbab` |
| `drivers/aic8800/aic8800_fdrv/aicwf_sdio.c` | OpenCentauri | `sdio-cc2` | `5353c597fe792dcc98c26d6406e37e0f778cd65f` |
| `drivers/aic8800/aic8800_fdrv/aicwf_sdio.h` | OpenCentauri | `sdio-cc2` | `296a6c05afca8aaac0b6181f6054870de72561f4` |
| `drivers/aic8800/aic8800_fdrv/Kconfig` | OpenCentauri | `sdio-cc2` | `92d5986816937d31b2f283943b98fb26a4217c5d` |
| `aic8800_fdrv/Makefile` | LYU4662 | `6.18` | `74957daef8d01ce66759662f735c7aee42dff33f` |
