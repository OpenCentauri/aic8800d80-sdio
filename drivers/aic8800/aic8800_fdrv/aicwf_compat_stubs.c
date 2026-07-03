// SPDX-License-Identifier: GPL-2.0-only
/*
 * aicwf_compat_stubs.c
 *
 * Stubs for symbols normally provided by aic_load_fw, used when the loader
 * module is disabled (e.g. SDIO-only CC2 builds).
 */

#include <linux/types.h>
#include "rwnx_platform.h"

int get_adap_test(void)
{
    return 0;
}

int get_testmode(void)
{
    return 0;
}

void set_testmode(int mode)
{
}

int get_flash_bin_size(void)
{
    return 0;
}

u32 get_flash_bin_crc(void)
{
    return 0;
}

void get_fw_path(char *fw_path)
{
    if (fw_path)
        fw_path[0] = '\0';
}

void get_userconfig_txpwr_idx(txpwr_idx_conf_t *txpwr_idx)
{
}

void get_userconfig_txpwr_ofst(txpwr_ofst_conf_t *txpwr_ofst)
{
}
