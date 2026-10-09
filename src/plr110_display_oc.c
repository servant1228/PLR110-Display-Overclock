// SPDX-License-Identifier: GPL-2.0-only
/*
 * plr110_display_oc - OnePlus Ace 6T (PLR110 / SM8845) display refresh overclock
 *
 * Phase 1: load + symbol resolution + struct dsi_mode_info ABI validation.  DONE,
 *          verified on device (see docs/device-info.md section 7).
 * Phase 2: locate struct dsi_display / struct dsi_panel and inject a 185 Hz mode.
 *
 * Nothing here writes to kernel memory yet: phase 2a is a read-only probe that
 * measures the two struct layouts on the *running* kernel instead of trusting
 * offsets derived from the 16.0.10.500 source tree.
 *
 * Why we need those offsets (from the vendor source):
 *
 *   dsi_connector_get_modes()
 *     -> dsi_display_get_mode_count()
 *     -> dsi_display_get_modes(display, &out_modes)
 *          display->modes = kcalloc(panel->num_display_modes, ...)
 *          dsi_display_get_modes_helper(..., panel->num_timing_nodes, ...)
 *              loop mode_idx < num_timing_nodes:
 *                  dsi_panel_get_mode(panel, mode_idx, &display_mode, ...)
 *                  display->modes[array_idx++] = display_mode
 *
 *   dsi_conn_mode_valid()                      <- DRM mode validation
 *     convert_to_dsi_mode(mode, &dsi_mode)
 *     dsi_display_find_mode(display, &dsi_mode, NULL, &full_dsi_mode)  -> MODE_ERROR if absent
 *
 * So a synthetic DRM mode is only accepted if a matching dsi_display_mode
 * exists in display->modes[]. The array is sized by num_display_modes and the
 * fill loop runs num_timing_nodes times; for this panel both are 5, i.e. there
 * is no slack. Injecting therefore means bumping both counters by one and
 * fabricating the extra entry inside dsi_panel_get_mode().
 */

#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/utsname.h>
#include <drm/drm_connector.h>
#include <drm/drm_modes.h>

#define PLR110_TAG "plr110_display_oc"

#define PLR110_BUILD_RELEASE \
	"6.12.69-android16-6-g242e1a07f878-ab15865446-4k"

#define plr110_info(fmt, ...) pr_info(PLR110_TAG ": " fmt, ##__VA_ARGS__)
#define plr110_err(fmt, ...)  pr_err(PLR110_TAG ": " fmt, ##__VA_ARGS__)

/* ------------------------------------------------------------------ config */

static int  enable;
static int  dump_modes = 1;
static int  scan_structs = 1;
static uint target_hz = 185;

module_param(enable, int, 0644);
MODULE_PARM_DESC(enable, "0 = diagnostic only (default), 1 = enable injection");
module_param(dump_modes, int, 0644);
MODULE_PARM_DESC(dump_modes, "log every mode pushed through dsi_display_set_mode");
module_param(scan_structs, int, 0644);
MODULE_PARM_DESC(scan_structs, "read-only scan of struct dsi_display / dsi_panel");
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

#define MAX_TARGET_SYMS 28

static const char *target_syms[] = {
	/* DRM / mode plumbing (KMI) */
	"drm_mode_probed_add",
	"drm_mode_duplicate",
	"drm_mode_destroy",
	"drm_mode_vrefresh",
	"drm_connector_list_iter_begin",
	"drm_connector_list_iter_next",
	"drm_connector_list_iter_end",
	/* vendor display (kallsyms only, not exported) */
	"dsi_display_get_modes",
	"dsi_display_get_modes_helper",
	"dsi_display_get_mode_count",
	"dsi_connector_get_modes",
	"dsi_display_set_mode",
	"dsi_panel_get_mode",
	"dsi_panel_get_mode_count",
	"dsi_display_find_mode",
	"dsi_display_validate_mode",
	"dsi_panel_calc_dsi_transfer_time",
	"dsi_display_clk_ctrl",
	"dsi_panel_tx_cmd_set",
	"dsi_display_bind",
	/* oplus layer */
	"oplus_display_panel_cmd_print",
	"oplus_dsi_panel_cmd_print",
};

static struct { const char *name; unsigned long addr; } hits[MAX_TARGET_SYMS];
static int hit_count;

static unsigned long sym(const char *name)
{
	int i;

	for (i = 0; i < hit_count; i++)
		if (!strcmp(hits[i].name, name))
			return hits[i].addr;
	return 0;
}

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
 * Field-for-field copy of
 *   vendor/qcom/opensource/display-drivers/msm/dsi/dsi_defs.h
 *
 * Phase 1 measured the running kernel: with this struct at 104 bytes the
 * kprobe read bpp=8 out of offset 104, i.e. the real sizeof(dsi_mode_info) is
 * 88 and pixel_clk_khz sits at 88. 88 is exactly what you get when the struct
 * ends after dsc_enabled/vdc_enabled and the dsc/vdc pointers are not part of
 * it, so that is what we mirror here. plr110_raw_dump() below re-verifies it.
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
	void *priv_info;
};

static void plr110_dump_mode(const char *why, struct plr110_display_mode *m)
{
	plr110_info("%s: %ux%u @%uHz clk=%llu min_dsi=%llu mdp_t=%u "
		    "hporch=%u/%u/%u vporch=%u/%u/%u dsc=%u bpp=%u pxclk=%u "
		    "flags=%#x caps=%#x fmt=%#x idx=%u priv=%px\n", why,
		    m->timing.h_active, m->timing.v_active, m->timing.refresh_rate,
		    m->timing.clk_rate_hz, m->timing.min_dsi_clk_hz,
		    m->timing.mdp_transfer_time_us,
		    m->timing.h_front_porch, m->timing.h_back_porch,
		    m->timing.h_sync_width,
		    m->timing.v_front_porch, m->timing.v_back_porch,
		    m->timing.v_sync_width,
		    m->timing.dsc_enabled, m->bpp, m->pixel_clk_khz,
		    m->dsi_mode_flags, m->panel_mode_caps, m->pixel_format_caps,
		    m->mode_idx, m->priv_info);
}

/* ------------------------------------------------------- struct discovery */

static void *last_display;
static void *last_panel;
static int   display_get_modes_calls;
static int   panel_get_mode_calls;
static bool  panel_dumped;
static bool  display_dumped;

/*
 * A u32 that looks like a small counter (vanilla kernel: num_timing_nodes and
 * num_display_modes are both 5 for this panel). Printing only rows that contain
 * such a value keeps the dmesg dump small while showing the whole neighbourhood,
 * including the cur_mode pointer that precedes them.
 */
static bool row_is_interesting(const u32 *w)
{
	int i;

	for (i = 0; i < 4; i++)
		if (w[i] >= 1 && w[i] <= 64)
			return true;
	return false;
}

static void scan_u32_rows(const char *what, const void *base, size_t bytes,
			  bool only_interesting)
{
	const u32 *w = base;
	size_t i;

	plr110_info("%s @%px, scanning %zu bytes\n", what, base, bytes);
	for (i = 0; i + 16 <= bytes; i += 16) {
		if (only_interesting && !row_is_interesting(&w[i / 4]))
			continue;
		plr110_info("  +%04zx: %08x %08x %08x %08x\n", i,
			    w[i / 4], w[i / 4 + 1], w[i / 4 + 2], w[i / 4 + 3]);
	}
}

/* find the offset of a 64-bit value inside a struct: gives us
 * offsetof(struct dsi_display, panel) once we already know the panel pointer.
 */
static void find_ptr_in_struct(const char *what, const void *base, size_t bytes,
			       const void *needle)
{
	const u64 *q = base;
	size_t i;
	int found = 0;

	for (i = 0; i + 8 <= bytes; i += 8) {
		if (q[i / 8] == (u64)(uintptr_t)needle) {
			plr110_info("%s: %px at +%#zx (offsetof = %zu)\n",
				    what, needle, i, i);
			found++;
		}
	}
	if (!found)
		plr110_info("%s: %px not found in first %zu bytes\n",
			    what, needle, bytes);
	else
		plr110_info("%s: %d match(es)\n", what, found);
}

/* ------------------------------------------- measured struct offsets (2a) */
/*
 * Measured on the running 6.12.69 kernel, not derived from the 16.0.10.500
 * source tree - see docs/device-info.md section 8 for the raw dump.
 *
 *   offsetof(struct dsi_display, panel)      = 0x108 (264)
 *   offsetof(struct dsi_panel, cur_mode)     = 0x5c8 (1480)
 *   offsetof(struct dsi_panel, num_timing_nodes)  = 0x5d0 (1488)
 *   offsetof(struct dsi_panel, num_display_modes) = 0x5d4 (1492)
 *
 * Identified by printing panel->name (validates the object) and looking for
 * the two adjacent u32 counters that follow the cur_mode pointer.
 * Nothing writes to these yet; they are recorded here so the next revision
 * can validate them again at runtime before ever storing anything.
 */
#define DISPLAY_OFF_PANEL		0x108
#define DISPLAY_OFF_DRM_CONN		0x10
#define PANEL_OFF_CUR_MODE		0x5c8
#define PANEL_OFF_NUM_TIMING_NODES	0x5d0
#define PANEL_OFF_NUM_DISPLAY_MODES	0x5d4

/* ------------------------------------------------- DRM connector modes */

static bool str_looks_valid(const char *s, size_t max)
{
	size_t i;

	if (!s)
		return false;
	for (i = 0; i < max; i++) {
		unsigned char c = (unsigned char)s[i];

		if (c == '\0')
			return i > 0;
		if (c < 0x20 || c > 0x7e)
			return false;
	}
	return false;
}

static bool ptr_is_kernel(const void *p)
{
	u64 v = (u64)(uintptr_t)p;

	return v >= 0xffff000000000000ULL;
}

static u32 mode_vrefresh(const struct drm_display_mode *m)
{
	u32 htotal = m->htotal ? m->htotal : (m->hdisplay + m->hsync_end - m->hsync_start);
	u32 vtotal = m->vtotal ? m->vtotal : (m->vdisplay + m->vsync_end - m->vsync_start);

	if (!htotal || !vtotal)
		return 0;
	return (u32)(((u64)m->clock * 1000ULL) / ((u64)htotal * vtotal));
}

static void dump_one_mode_list(const char *which, struct list_head *head)
{
	struct drm_display_mode *m;
	int n = 0;

	list_for_each_entry(m, head, head) {
		if (!ptr_is_kernel(m) || !str_looks_valid(m->name, 32)) {
			plr110_info("  %s[%d]: bogus entry %px, stopping\n", which, n, m);
			return;
		}
		plr110_info("  %s[%d] \"%s\" clock=%dkHz %dx%d vrefresh=%u type=%#x\n",
			    which, n, m->name, m->clock, m->hdisplay, m->vdisplay,
			    mode_vrefresh(m), m->type);
		if (++n >= 32) {
			plr110_info("  %s: truncated at 32\n", which);
			return;
		}
	}
	plr110_info("  %s: %d entries\n", which, n);
}

static void dump_drm_connector(void *display)
{
	struct drm_connector *c = *(struct drm_connector **)
		((char *)display + DISPLAY_OFF_DRM_CONN);

	plr110_info("compile-time offsets: sizeof(drm_connector)=%zu "
		    "name@%zu modes@%zu probed_modes@%zu | "
		    "sizeof(drm_display_mode)=%zu clock@%zu hdisplay@%zu "
		    "vdisplay@%zu head@%zu\n",
		    sizeof(struct drm_connector),
		    offsetof(struct drm_connector, name),
		    offsetof(struct drm_connector, modes),
		    offsetof(struct drm_connector, probed_modes),
		    sizeof(struct drm_display_mode),
		    offsetof(struct drm_display_mode, clock),
		    offsetof(struct drm_display_mode, hdisplay),
		    offsetof(struct drm_display_mode, vdisplay),
		    offsetof(struct drm_display_mode, head));

	if (!ptr_is_kernel(c)) {
		plr110_info("display+%#x = %px is not a kernel pointer\n",
			    DISPLAY_OFF_DRM_CONN, c);
		return;
	}
	if (!str_looks_valid(c->name, 64)) {
		plr110_info("connector %px name is not a plausible string; "
			    "struct layout mismatch, not walking lists\n", c);
		return;
	}

	plr110_info("drm_connector %px name=\"%s\" type=%d/%d status=%d\n",
		    c, c->name, c->connector_type, c->connector_type_id,
		    c->status);
	dump_one_mode_list("probed_modes", &c->probed_modes);
	dump_one_mode_list("modes", &c->modes);
}

/* ------------------------------------------------- priv_info discovery */
/*
 * struct dsi_display_mode_priv_info (msm/dsi/dsi_defs.h:715) starts with
 *
 *     struct dsi_panel_cmd_set cmd_sets[DSI_CMD_SET_MAX];
 *     u32 *phy_timing_val;
 *     u32 phy_timing_len;
 *     ...
 *     u64 clk_rate_hz;
 *
 * so the two fields an 185Hz injection needs are behind a variable-length
 * array and cannot be hardcoded from the header. Instead we anchor on two
 * values that are unique to the running panel:
 *
 *   phy_timing_val points at a 14-byte buffer holding exactly the 1363.2 MHz
 *   vector from the device tree - an unmistakable fingerprint
 *   clk_rate_hz == 1363200000
 *
 * panel->cur_mode (+0x5c8) is a real entry of display->modes[], so unlike the
 * dsi_display_set_mode() argument (which is the by-value bridge copy with
 * priv_info == NULL) it carries a fully populated priv_info.
 */
static const u8 plr165_phy[14] = {
	0x00, 0x2c, 0x0c, 0x0c, 0x1d, 0x1a, 0x0c, 0x0c,
	0x0b, 0x02, 0x04, 0x00, 0x24, 0x11
};
#define PLR165_CLK_RATE_HZ 1363200000ULL

static bool mem_has(const void *hay, size_t len, const u8 *needle, size_t nlen)
{
	const u8 *b = hay;
	size_t i;

	for (i = 0; i + nlen <= len; i++)
		if (!memcmp(b + i, needle, nlen))
			return true;
	return false;
}


static void scan_words(const char *what, const void *base, size_t bytes,
		       bool only_interesting)
{
	const u32 *w = base;
	size_t i;

	plr110_info("%s @%px (%zu bytes)\n", what, base, bytes);
	for (i = 0; i + 16 <= bytes; i += 16) {
		if (only_interesting && !row_is_interesting(&w[i / 4]))
			continue;
		plr110_info("  +%04zx: %08x %08x %08x %08x\n", i,
			    w[i / 4], w[i / 4 + 1], w[i / 4 + 2], w[i / 4 + 3]);
	}
}

static void scan_priv_info(void *mode_ptr)
{
	struct plr110_display_mode *cm = mode_ptr;
	size_t off, j;
	void *priv = NULL;

	if (!last_panel || !ptr_is_kernel(last_panel)) {
		plr110_info("priv scan: no panel yet\n");
		return;
	}

	cm = *(void **)((char *)last_panel + PANEL_OFF_CUR_MODE);
	if (!ptr_is_kernel(cm)) {
		plr110_info("priv scan: cur_mode %px is not a kernel pointer\n", cm);
		return;
	}

	plr110_info("cur_mode=%px  %ux%u @%uHz dsc=%u pxclk=%u flags=%#x "
		    "caps=%#x fmt=%#x idx=%u\n", cm,
		    cm->timing.h_active, cm->timing.v_active,
		    cm->timing.refresh_rate, cm->timing.dsc_enabled,
		    cm->pixel_clk_khz, cm->dsi_mode_flags, cm->panel_mode_caps,
		    cm->pixel_format_caps, cm->mode_idx);

	scan_words("struct dsi_display_mode (full, from validate_mode)", cm, 0x100, false);

	/* Walk every kernel pointer in the mode and look for the phy table. */
	for (off = 0; off + 8 <= 0x100; off += 8) {
		void *p = *(void **)((char *)cm + off);
		bool hit;

		if (!ptr_is_kernel(p))
			continue;

		hit = mem_has(p, 0x4000, plr165_phy, sizeof(plr165_phy));
		plr110_info("  mode+%#04zx -> %px  %08x %08x %08x %08x  phy165=%s\n",
			    off, p, ((u32 *)p)[0], ((u32 *)p)[1],
			    ((u32 *)p)[2], ((u32 *)p)[3], hit ? "FOUND" : "-");
		if (hit)
			priv = p;
	}

	if (!priv) {
		plr110_info("priv scan: priv_info not identified\n");
		return;
	}

	plr110_info("priv_info=%px - locating fields\n", priv);
	for (j = 0; j + 8 <= 0x4000; j += 8) {
		void *q = *(void **)((char *)priv + j);
		u64 v = *(u64 *)((char *)priv + j);

		if (ptr_is_kernel(q) && !memcmp(q, plr165_phy, 14))
			plr110_info("  priv+%#zx -> %px = phy_timing_val\n", j, q);
		if (v == PLR165_CLK_RATE_HZ)
			plr110_info("  priv+%#zx = clk_rate_hz (%llu)\n", j, v);
		if (*(u32 *)((char *)priv + j) == 14)
			plr110_info("  priv+%#zx = 14 (phy_timing_len?)\n", j);
	}
}

/* ------------------------------------------------------------- mode kprobe */

static struct kprobe kp_set_mode;
static struct kprobe kp_panel_get_mode;
static struct kprobe kp_display_get_modes;
static struct kprobe kp_panel_tx_cmd_set;
static struct kprobe kp_validate_mode;
static bool full_mode_scanned;
static unsigned long set_mode_calls;

/*
 * Discovery without touching the mode-enumeration path.
 *
 * dsi_connector_get_modes() only runs during DRM probe, i.e. before a
 * KernelSU module can possibly be loaded, so dsi_panel_get_mode() is not a
 * usable handle. dsi_panel_tx_cmd_set(panel, type) however is invoked on
 * every DCS command set (panel on/off, brightness, fps switch ...) and its
 * first argument is the struct dsi_panel we need.
 *
 *   x0 = struct dsi_panel *       (validated by printing ->name, offset 0)
 *
 * Then offsetof(struct dsi_display, panel) is found by scanning the
 * dsi_display we hold from dsi_display_set_mode() for that exact pointer
 * value. That scan only reads memory belonging to the display struct and
 * never dereferences a candidate, so it cannot fault.
 */
static bool panel_tx_cmd_set_reported;

static int panel_tx_cmd_set_pre(struct kprobe *p, struct pt_regs *regs)
{
	void *panel = (void *)regs->regs[0];

	if (!panel)
		return 0;

	if (!last_panel)
		last_panel = panel;

	if (scan_structs && !panel_dumped) {
		panel_dumped = true;
		plr110_info("dsi_panel_tx_cmd_set: panel=%px type=%lu\n",
			    panel, (unsigned long)regs->regs[1]);
		plr110_info("panel->name = \"%s\" (offset 0, validates the object)\n",
			    *(const char **)panel);
		scan_u32_rows("struct dsi_panel", panel, 3072, true);
	}

	if (!panel_tx_cmd_set_reported) {
		panel_tx_cmd_set_reported = true;
		plr110_info("panel_tx_cmd_set fired (panel=%px)\n", panel);
	}
	return 0;
}

static int set_mode_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct plr110_display_mode *m = (void *)regs->regs[1];
	void *display = (void *)regs->regs[0];

	set_mode_calls++;

	if (scan_structs && !display_dumped && display && last_panel) {
		display_dumped = true;
		plr110_info("set_mode: display=%px panel=%px\n", display, last_panel);
		find_ptr_in_struct("struct dsi_display", display, 2048, last_panel);
		dump_drm_connector(display);
	}

	if (dump_modes && m) {
		plr110_dump_mode("set_mode", m);

		/* Raw words so the tail of struct dsi_display_mode (pixel_clk_khz,
		 * mode flags, bpp, mode_idx, priv_info) can be located exactly on
		 * the running kernel instead of being inferred. First few calls
		 * only, to keep dmesg readable. */
		if (set_mode_calls <= 3) {
			const u32 *w = (const u32 *)m;
			int i;

			for (i = 0; i < 40; i += 4)
				plr110_info("  mode+%03x: %08x %08x %08x %08x\n",
					    i * 4, w[i], w[i + 1], w[i + 2], w[i + 3]);
		}
	}
	return 0;
}

/*
 * dsi_display_validate_mode(display, mode, flags) is called from
 * dsi_conn_mode_valid() with the mode that dsi_display_find_mode() returned,
 * i.e. a real display->modes[] entry with a fully populated priv_info - unlike
 * the dsi_display_set_mode() argument (the by-value bridge copy) and unlike
 * panel->cur_mode, which we measured to be that same bridge copy.
 */
static int validate_mode_pre(struct kprobe *p, struct pt_regs *regs)
{
	void *mode = (void *)regs->regs[1];

	if (!full_mode_scanned && scan_structs && ptr_is_kernel(mode)) {
		full_mode_scanned = true;
		plr110_info("validate_mode: display=%px mode=%px\n",
			    (void *)regs->regs[0], mode);
		scan_priv_info(mode);
	}
	return 0;
}

static int panel_get_mode_pre(struct kprobe *p, struct pt_regs *regs)
{
	void *panel = (void *)regs->regs[0];
	unsigned int index = (unsigned int)regs->regs[1];

	if (panel)
		last_panel = panel;

	if (panel_get_mode_calls < 16)
		plr110_info("panel_get_mode#%d: panel=%px index=%u\n",
			    panel_get_mode_calls, panel, index);
	panel_get_mode_calls++;

	if (scan_structs && !panel_dumped && panel && panel_get_mode_calls >= 3) {
		panel_dumped = true;
		plr110_info("panel->name = \"%s\" (offset 0, validates the object)\n",
			    *(const char **)panel);
		scan_u32_rows("struct dsi_panel", panel, 3072, true);
	}
	return 0;
}

static int display_get_modes_pre(struct kprobe *p, struct pt_regs *regs)
{
	void *display = (void *)regs->regs[0];

	if (display)
		last_display = display;

	display_get_modes_calls++;
	if (display_get_modes_calls <= 4)
		plr110_info("display_get_modes#%d: display=%px out_modes=%px\n",
			    display_get_modes_calls, display,
			    (void *)regs->regs[1]);

	if (scan_structs && !display_dumped && display && last_panel &&
	    display_get_modes_calls >= 2) {
		display_dumped = true;
		scan_u32_rows("struct dsi_display", display, 2048, true);
		find_ptr_in_struct("struct dsi_display", display, 2048, last_panel);
	}
	return 0;
}
static struct kretprobe kr_conn_get_modes;

static int conn_get_modes_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	long count = (long)regs->regs[0];

	plr110_info("connector_get_modes returned %ld modes\n", count);
	return 0;
}

/* ---------------------------------------------------------------- install */

static int install_probe(struct kprobe *kp, const char *name,
			 kprobe_pre_handler_t pre)
{
	unsigned long addr = sym(name);

	if (!addr) {
		plr110_err("%s not found, cannot probe\n", name);
		return -ENOENT;
	}
	kp->pre_handler = pre;
	kp->addr = (kprobe_opcode_t *)addr;
	return register_kprobe(kp);
}

static int install_all_probes(void)
{
	int ret;

	ret = install_probe(&kp_set_mode, "dsi_display_set_mode", set_mode_pre);
	if (ret)
		return ret;

	ret = install_probe(&kp_panel_tx_cmd_set, "dsi_panel_tx_cmd_set",
			    panel_tx_cmd_set_pre);
	if (ret)
		return ret;

	ret = install_probe(&kp_validate_mode, "dsi_display_validate_mode",
			    validate_mode_pre);
	if (ret)
		return ret;

	ret = install_probe(&kp_panel_get_mode, "dsi_panel_get_mode",
			    panel_get_mode_pre);
	if (ret)
		return ret;

	ret = install_probe(&kp_display_get_modes, "dsi_display_get_modes",
			    display_get_modes_pre);
	if (ret)
		return ret;

	if (sym("dsi_connector_get_modes")) {
		memset(&kr_conn_get_modes, 0, sizeof(kr_conn_get_modes));
		kr_conn_get_modes.kp.addr =
			(kprobe_opcode_t *)sym("dsi_connector_get_modes");
		kr_conn_get_modes.handler = conn_get_modes_ret;
		ret = register_kretprobe(&kr_conn_get_modes);
		if (ret)
			return ret;
	}
	return 0;
}

/* ---------------------------------------------------------------- lifecycle */

static int __init plr110_init(void)
{
	int ret;

	plr110_info("loading: built-for=%s running=%s\n",
		    PLR110_BUILD_RELEASE, utsname()->release);
	plr110_info("enable=%d dump_modes=%d scan_structs=%d target_hz=%u\n",
		    enable, dump_modes, scan_structs, target_hz);

	ret = resolve_kallsyms_lookup_name();
	if (ret)
		return ret;

	plr110_info("resolving target symbols:");
	resolve_targets();
	plr110_info("resolved %d/%zu symbols\n", hit_count,
		    ARRAY_SIZE(target_syms));

	ret = install_all_probes();
	if (ret) {
		plr110_err("install_all_probes failed: %d\n", ret);
		return ret;
	}

	plr110_info("ready (phase 2a: read-only struct discovery)\n");
	return 0;
}

static void __exit plr110_exit(void)
{
	if (kr_conn_get_modes.kp.addr)
		unregister_kretprobe(&kr_conn_get_modes);
	if (kp_validate_mode.addr)
		unregister_kprobe(&kp_validate_mode);
	if (kp_panel_tx_cmd_set.addr)
		unregister_kprobe(&kp_panel_tx_cmd_set);
	if (kp_display_get_modes.addr)
		unregister_kprobe(&kp_display_get_modes);
	if (kp_panel_get_mode.addr)
		unregister_kprobe(&kp_panel_get_mode);
	if (kp_set_mode.addr)
		unregister_kprobe(&kp_set_mode);

	plr110_info("unloaded after %lu set_mode calls\n", set_mode_calls);
}

module_init(plr110_init);
module_exit(plr110_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PLR110 display OC project");
MODULE_DESCRIPTION("OnePlus Ace 6T (PLR110/SM8845) DSI display overclock");
MODULE_VERSION("0.2.0");
