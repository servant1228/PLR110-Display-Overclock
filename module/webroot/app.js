/* SPDX-License-Identifier: GPL-2.0-only */
'use strict';

const STATE = '/data/adb/plr110_display_oc';
const KO = '/data/adb/modules/plr110_display_oc/system/lib/modules/plr110_display_oc.ko';

/* ---------------------------------------------------------------- ksu glue */
/* KernelSU injects window.ksu into the WebUI webview. */
const ksu = window.ksu || window.KernelSU || null;

async function sh(cmd) {
  if (!ksu) return { errno: 127, stdout: '', stderr: 'not running inside KernelSU WebUI' };
  if (typeof ksu.exec === 'function') {
    const r = await ksu.exec(cmd);
    if (typeof r === 'string') return { errno: 0, stdout: r, stderr: '' };
    return r;
  }
  if (typeof ksu.execCommand === 'function') {
    return { errno: 0, stdout: await ksu.execCommand(cmd), stderr: '' };
  }
  return { errno: 127, stdout: '', stderr: 'no exec API' };
}

function toast(msg) {
  if (ksu && typeof ksu.toast === 'function') { try { ksu.toast(msg); return; } catch (e) {} }
  console.log(msg);
}

const $ = (id) => document.getElementById(id);
const set = (id, v) => { $(id).textContent = v; };

/* ------------------------------------------------------------------ state */
async function readState() {
  const r = await sh(
    `mkdir -p ${STATE}; ` +
    `[ -f ${STATE}/enable ] || echo 0 > ${STATE}/enable; ` +
    `[ -f ${STATE}/target_hz ] || echo 185 > ${STATE}/target_hz; ` +
    `[ -f ${STATE}/dump_modes ] || echo 1 > ${STATE}/dump_modes; ` +
    `echo "enable=$(cat ${STATE}/enable)"; ` +
    `echo "target=$(cat ${STATE}/target_hz)"; ` +
    `echo "dump=$(cat ${STATE}/dump_modes)"`
  );
  const out = {};
  (r.stdout || '').trim().split('\n').forEach((l) => {
    const i = l.indexOf('=');
    if (i > 0) out[l.slice(0, i).trim()] = l.slice(i + 1).trim();
  });
  return out;
}

async function refresh() {
  set('webui-status', ksu ? 'KernelSU WebUI' : '浏览器预览模式（无 root）');

  const st = await readState();
  $('enable').checked = st.enable === '1';
  $('target').value = st.target || '185';
  $('dump').checked = st.dump !== '0';

  const info = await sh(
    `echo "K=$(uname -r)"; ` +
    `if grep -q '^plr110_display_oc ' /proc/modules; then echo "M=loaded"; else echo "M=not-loaded"; fi; ` +
    `echo "P=$(cat /sys/kernel/oplus_display/panel_id 2>/dev/null || echo -)"; ` +
    `echo "D=$(cat /sys/kernel/oplus_display/dump_info 2>/dev/null || echo -)"`
  );
  const kv = {};
  (info.stdout || '').trim().split('\n').forEach((l) => {
    const i = l.indexOf('=');
    if (i > 0) kv[l.slice(0, i)] = l.slice(i + 1);
  });

  set('st-kernel', kv.K || '—');
  set('st-module', kv.M === 'loaded' ? '已加载' : '未加载');
  set('st-panel', kv.P || '—');

  const modes = await sh(`dumpsys SurfaceFlinger 2>/dev/null | grep -m1 'activeMode=' ; ` +
                         `dumpsys display 2>/dev/null | grep -m1 'mSupportedRefreshRates'`);
  set('st-modes', (modes.stdout || '').trim() || '—');

  const log = await sh(`tail -n 120 ${STATE}/module.log 2>/dev/null`);
  $('log').textContent = (log.stdout || '').trim() || '（空）';
}

/* ----------------------------------------------------------------- actions */
async function save() {
  const en = $('enable').checked ? 1 : 0;
  const tgt = parseInt($('target').value, 10) || 185;
  const dmp = $('dump').checked ? 1 : 0;

  const r = await sh(
    `mkdir -p ${STATE}; ` +
    `echo ${en} > ${STATE}/enable; ` +
    `echo ${tgt} > ${STATE}/target_hz; ` +
    `echo ${dmp} > ${STATE}/dump_modes; ` +
    `if grep -q '^plr110_display_oc ' /proc/modules; then rmmod plr110_display_oc 2>&1; fi; ` +
    (en ? `sleep 1; insmod ${KO} enable=1 target_hz=${tgt} dump_modes=${dmp} 2>&1; echo "insmod=$?"` : `echo "disabled"`)
  );
  toast('已保存');
  const out = ((r.stdout || '') + (r.stderr || '')).trim();
  if (out) $('log').textContent = out;
  await refresh();
}

async function unload() {
  const r = await sh(`rmmod plr110_display_oc 2>&1; echo "rmmod=$?"`);
  toast('模块已卸载');
  $('log').textContent = ((r.stdout || '') + (r.stderr || '')).trim();
  await refresh();
}

async function clearLog() {
  await sh(`: > ${STATE}/module.log`);
  $('log').textContent = '（空）';
}

$('refresh').addEventListener('click', refresh);
$('save').addEventListener('click', save);
$('unload').addEventListener('click', unload);
$('clearlog').addEventListener('click', clearLog);

if (ksu && typeof ksu.fullScreen === 'function') { try { ksu.fullScreen(); } catch (e) {} }
refresh();
