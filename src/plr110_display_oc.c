// SPDX-License-Identifier: GPL-2.0-only
/*
 * plr110_display_oc - OnePlus Ace 6T (PLR110 / SM8845) display refresh overclock
 *
 * Phase 1: diagnostic / bring-up.
 *
 * This build does NOT modify any timing. It exists to prove that a module built
 * against the published OnePlus source tree loads on the running 6.12.69 kernel
 * and that the vendor display ABI we mirror still matches at runtime.
 *
 * Everything vendor-private is resolved through kallsyms at runtime, so the
 * module links against no vendor symbol and needs no vendor Module.symvers.
 */

#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/version.h>

#define PLR110_TAG "plr110_display_oc"

#define plr110_info(fmt, ...) pr_info(PLR110_TAG ": " fmt, ##__VA_ARGS__)
#define plr110_err(fmt, ...)  pr_err(PLR110_TAG ": " fmt, ##__VA_ARGS__)

/* ------------------------------------------------------------------ config */

static int  enable = 0;      /* 0 = diagnose only, 1 = install hooks */
static int  dump_modes = 1;
static uint target_hz = 185;

module_param(enable, int, 0644);
MODULE_PARM_DESC(enable, "0 = diagnostic only (default), 1 = enable hooks");
module_param(dump_modes, int, 0644);
MODULE_PARM_DESC(dump_modes, "log every mode pushed through dsi_display_set_mode");
module_param(target_hz, uint, 0644);
MODULE_PARM_DESC(target_hz, "refresh rate to inject when enable=1");

/* ------------------------------------------------- runtime symbol resolving */

static unsigned long (*kln)(const char *name);

static int resolve_kallsyms_lookup_name(void)
{
	struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
	int ret;

	ret = register_kprobe(&kp);
	if (ret) {
		plr110_err("kprobe on kallsyms_lookup_name failed: %d\n", ret);
		return ret;
	}
	kln = (void *)kp.addr;
	unregister_kprobe(&kp);

	if (!kln) {
		plr110_err("kallsyms_lookup_name resolved to NULL\n");
		return -ENOENT;
	}
	plr110_info("kallsyms_lookup_name @ %pS\n", kln);
	return 0;
}

#define MAX_TARGET_SYMS 24

static const char *target_syms[] = {
	/* DRM / mode plumbing (KMI) */
	"drm_mode_probed_add",
	"drm_mode_duplicate",
	"drm_mode_destroy",
	"drm_mode_vrefresh",
	"drm_connector_list_iter_begin",
	"drm_connector_list_iter_next",
	"drm_connector_list_iter_end",
	/* vendor display (not exported, kallsyms only) */
	"dsi_display_get_modes",
	"dsi_display_get_modes_helper",
	"dsi_connector_get_modes",
	"dsi_display_set_mode",
	"dsi_panel_parse_timing",
	"dsi_panel_get_drm_mode",
	"dsi_panel_calc_dsi_transfer_time",
	"dsi_display_clk_ctrl",
	"dsi_panel_tx_cmd_set",
	"dsi_panel_parse_dt",
	"dsi_display_bind",
	"dsi_display_get",
	/* oplus layer */
	"oplus_display_panel_cmd_print",
	"oplus_dsi_panel_cmd_print",
};

struct sym_hit {
	const char *name;
	unsigned long addr;
};

static struct sym_hit hits[MAX_TARGET_SYMS];
static int hit_count;

static void resolve_targets(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(target_syms) && hit_count < MAX_TARGET_SYMS; i++) {
		unsigned long a = kln ? kln(target_syms[i]) : 0;

		if (a) {
			hits[hit_count].name = target_syms[i];
			hits[hit_count].addr = a;
			hit_count++;
			plr110_info("  %-34s %ps\n", target_syms[i], (void *)a);
		} else {
			plr110_info("  %-34s MISSING\n", target_syms[i]);
		}
	}
}

/* ------------------------------------------- mirror of struct dsi_mode_info */
/*
 * Field-for-field copy of the leading part of
 *   vendor/qcom/opensource/display-drivers/msm/dsi/dsi_defs.h
 * Only the part we read here; layout is deterministic (u32/bool/u64/pointer).
 */
struct plr110_mode_timing {
	u32 h_active;
	u32 h_back_porch;
	u32 h_sync_width;
	u32 h_front_porch;
	u32 h_skew;
	bool h_sync_polarity;

	u32 v_active;
	u32 v_back_porch;
	u32 v_sync_width;
	u32 v_front_porch;
	bool v_sync_polarity;

	u32 refresh_rate;
	u64 clk_rate_hz;
	u64 min_dsi_clk_hz;
	u32 mdp_transfer_time_us;
	u32 dsi_transfer_time_us;
	bool dsc_enabled;
	bool vdc_enabled;
	void *dsc;
	void *vdc;
};

struct plr110_display_mode {
	struct plr110_mode_timing timing;
	u32 pixel_clk_khz;
	u32 dsi_mode_flags;
	u32 panel_mode_caps;
	u32 pixel_format_caps;
	u32 bpp;
	bool is_preferred;
	u32 mode_idx;
};

/* ------------------------------------------------------------- mode kprobe */

static struct kprobe kp_set_mode;
static unsigned long set_mode_calls;

static int set_mode_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct plr110_display_mode *m = (void *)regs->regs[1];

	set_mode_calls++;

	if (!dump_modes || !m)
		return 0;

	plr110_info("set_mode#%lu: %ux%u @%uHz clk=%llu min_dsi=%llu mdp_t=%u "
		    "hporch=%u/%u/%u vporch=%u/%u/%u dsc=%u bpp=%u pxclk=%u\n",
		    set_mode_calls,
		    m->timing.h_active, m->timing.v_active, m->timing.refresh_rate,
		    m->timing.clk_rate_hz, m->timing.min_dsi_clk_hz,
		    m->timing.mdp_transfer_time_us,
		    m->timing.h_front_porch, m->timing.h_back_porch,
		    m->timing.h_sync_width,
		    m->timing.v_front_porch, m->timing.v_back_porch,
		    m->timing.v_sync_width,
		    m->timing.dsc_enabled, m->bpp, m->pixel_clk_khz);

	return 0;
}

static int install_mode_probe(void)
{
	unsigned long addr = kln ? kln("dsi_display_set_mode") : 0;

	if (!addr) {
		plr110_err("dsi_display_set_mode not found; cannot probe\n");
		return -ENOENT;
	}

	kp_set_mode.pre_handler = set_mode_pre;
	kp_set_mode.addr = addr;

	return register_kprobe(&kp_set_mode);
}

/* ---------------------------------------------------------------- lifecycle */

static int __init plr110_init(void)
{
	int ret;

	plr110_info("loading: built for %s\n", UTS_RELEASE);
	plr110_info("vermagic tail must match: %s\n", VERMAGIC_STRING);
	plr110_info("enable=%d dump_modes=%d target_hz=%u\n",
		    enable, dump_modes, target_hz);

	ret = resolve_kallsyms_lookup_name();
	if (ret)
		return ret;

	plr110_info("resolving target symbols:");
	resolve_targets();
	plr110_info("resolved %d/%zu symbols\n", hit_count,
		    ARRAY_SIZE(target_syms));

	ret = install_mode_probe();
	if (ret) {
		plr110_err("install_mode_probe failed: %d\n", ret);
		return ret;
	}

	plr110_info("ready (phase 1: diagnostic only, no timing modified)\n");
	return 0;
}

static void __exit plr110_exit(void)
{
	if (kp_set_mode.addr)
		unregister_kprobe(&kp_set_mode);

	plr110_info("unloaded after %lu set_mode calls\n", set_mode_calls);
}

module_init(plr110_init);
module_exit(plr110_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PLR110 display OC project");
MODULE_DESCRIPTION("OnePlus Ace 6T (PLR110/SM8845) DSI display overclock - phase 1 diagnostic");
MODULE_VERSION("0.1.0");
