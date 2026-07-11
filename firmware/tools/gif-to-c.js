#!/usr/bin/env node
/**
 * gif-to-c.js — 把 clawd GIF 素材转成 C 字节数组（纯 Node.js，零依赖）
 *
 * 内置帧延迟加速：把所有帧的 delay 除以 SPEED_FACTOR（默认 3），
 * 让动画快 3 倍。不改图片数据、不丢帧，只是帧停留时间变短。
 *
 * 输出到 firmware/main/assets/clawd/c/gif_image_data.h，
 * LVGL 9 的 lv_gif_set_src() 可直接使用。
 *
 * 用法：  node gif-to-c.js
 */

const fs   = require('fs');
const path = require('path');

const GIF_DIR     = path.join(__dirname, '..', 'main', 'assets', 'clawd', 'gif', '150');
const OUT_FILE    = path.join(__dirname, '..', 'main', 'assets', 'clawd', 'c', 'gif_image_data.h');
const SPEED_X     = 5;    /* 帧延迟除以这个值，5 = 快 5 倍 */
const MIN_DELAY_CS = 2;   /* 最慢不低于 2 厘秒（20ms），避免 LVGL 来不及刷新 */

// ---- 12 态 → GIF 文件名映射（不用 clawd-mini-* 系列）----
const STATE_MAP = {
    'idle':         'clawd-idle.gif',
    'thinking':     'clawd-thinking.gif',
    'typing':       'clawd-typing.gif',
    'building':     'clawd-building.gif',
    'notification': 'clawd-notification.gif',
    'waiting':      'clawd-idle-reading.gif',
    'permission':   'clawd-react-annoyed.gif',
    'speaking':     'clawd-headphones-groove.gif',
    'happy':        'clawd-happy.gif',
    'error':        'clawd-error.gif',
    'sleeping':     'clawd-sleeping.gif',
    'disconnected': null,
};

/** 解析宽高和帧数 */
function parseGif(buf) {
    if (buf.length < 10 || buf.toString('ascii', 0, 3) !== 'GIF')
        throw new Error('not a GIF');
    const w = buf.readUInt16LE(6);
    const h = buf.readUInt16LE(8);
    let frames = 0;
    for (let i = 0; i < buf.length - 4; i++) {
        if (buf[i] === 0x00 && buf[i+1] === 0x21 && buf[i+2] === 0xF9 && buf[i+3] === 0x04)
            frames++;
    }
    return { w, h, frames };
}

/**
 * 加速 GIF：原地修改所有帧延迟（centiseconds → centiseconds/SPEED_X）。
 * 返回 { minDelay, maxDelay } 供日志输出。
 */
function speedUpGif(buf) {
    let minBefore = 999, maxBefore = 0;
    let minAfter = 999, maxAfter = 0;
    // GCE 结构：... 00 21 F9 04 [flags] [delay_lo] [delay_hi] [trans_idx] 00 ...
    for (let i = 0; i < buf.length - 8; i++) {
        if (buf[i] !== 0x00 || buf[i+1] !== 0x21 || buf[i+2] !== 0xF9 || buf[i+3] !== 0x04)
            continue;
        const delayLo = i + 5;  // delay 在 GCE header 后偏移 1 (flags) 处
        const delayHi = i + 6;
        let delay = buf[delayLo] | (buf[delayHi] << 8);
        if (delay < MIN_DELAY_CS) continue;   // delay=0 通常是静止帧，不动
        minBefore = Math.min(minBefore, delay);
        maxBefore = Math.max(maxBefore, delay);
        delay = Math.max(MIN_DELAY_CS, Math.round(delay / SPEED_X));
        buf[delayLo] = delay & 0xff;
        buf[delayHi] = (delay >> 8) & 0xff;
        minAfter = Math.min(minAfter, delay);
        maxAfter = Math.max(maxAfter, delay);
    }
    return { minBefore, maxBefore, minAfter, maxAfter };
}

function toHex(byte) {
    return '0x' + (byte & 0xff).toString(16).padStart(2, '0');
}

function generate() {
    const lines = [];
    lines.push('/* Auto-generated GIF image data for LVGL lv_gif widget.');
    lines.push(' * Source: clawd-on-desk GIF assets (~302x300).');
    lines.push(` * Tool: tools/gif-to-c.js — frame delays divided by ${SPEED_X} (min ${MIN_DELAY_CS}cs).`);
    lines.push(' *');
    lines.push(' * Usage: lv_gif_set_src(gif_obj, &clawd_gif_<state>);');
    lines.push(' */');
    lines.push('');
    lines.push('#ifndef GIF_IMAGE_DATA_H');
    lines.push('#define GIF_IMAGE_DATA_H');
    lines.push('');
    lines.push('#include "lvgl.h"');
    lines.push('');

    for (const [state, filename] of Object.entries(STATE_MAP)) {
        if (!filename) continue;

        const filePath = path.join(GIF_DIR, filename);
        if (!fs.existsSync(filePath)) {
            console.error(`  SKIP ${state}: file not found (${filename})`);
            continue;
        }
        // 读原始字节 → 加速 → 输出
        const raw = fs.readFileSync(filePath);
        const buf  = Buffer.from(raw);   // 可写副本
        const del  = speedUpGif(buf);
        const info = parseGif(buf);
        const varName = `clawd_gif_${state}`;

        console.log(`  ${state.padEnd(14)} ${filename.padEnd(30)} ${String(info.w)}x${String(info.h)}  ~${info.frames}f  delay ${del.minBefore}-${del.maxBefore}cs → ${del.minAfter}-${del.maxAfter}cs`);

        lines.push(`/* ${filename}: ${info.w}x${info.h}, ~${info.frames} frames, ${buf.length} bytes, delay ÷${SPEED_X} */`);
        lines.push(`static const uint8_t ${varName}_data[${buf.length}] = {`);

        for (let i = 0; i < buf.length; i += 16) {
            const chunk = [];
            for (let j = i; j < Math.min(i + 16, buf.length); j++)
                chunk.push(toHex(buf[j]));
            lines.push('    ' + chunk.join(', ') + (i + 16 < buf.length ? ',' : ''));
        }
        lines.push('};');
        lines.push('');

        lines.push(`static const lv_image_dsc_t ${varName} = {`);
        lines.push('    .header = {');
        lines.push('        .magic = LV_IMAGE_HEADER_MAGIC,');
        lines.push(`        .cf = LV_COLOR_FORMAT_RAW,`);
        lines.push(`        .w = ${info.w},`);
        lines.push(`        .h = ${info.h},`);
        lines.push(`        .stride = ${info.w},`);
        lines.push('    },');
        lines.push(`    .data_size = sizeof(${varName}_data),`);
        lines.push(`    .data = ${varName}_data,`);
        lines.push('};');
        lines.push('');
    }

    lines.push('#endif /* GIF_IMAGE_DATA_H */');
    return lines.join('\n');
}

// ---- main ----
console.log(`gif-to-c: converting clawd GIFs with ${SPEED_X}x speed-up...\n`);

const outDir = path.dirname(OUT_FILE);
if (!fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true });

const content = generate();
fs.writeFileSync(OUT_FILE, content, 'utf8');

const totalKB = fs.statSync(OUT_FILE).size / 1024;
console.log(`\nDone -> ${OUT_FILE} (${totalKB.toFixed(0)} KB)`);
