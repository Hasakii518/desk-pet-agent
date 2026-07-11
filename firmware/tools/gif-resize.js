#!/usr/bin/env node
/**
 * gif-resize.js — 把 clawd GIF 缩放到 150×150
 *
 * 需要 sharp：  cd tools && npm install sharp
 * 输出到 firmware/main/assets/clawd/gif/150/
 *
 * 用法：  node gif-resize.js
 */

const sharp  = require('sharp');
const fs     = require('fs');
const path   = require('path');

const SRC_DIR = path.join(__dirname, '..', 'main', 'assets', 'clawd', 'gif');
const DST_DIR = path.join(SRC_DIR, '150');
const SIZE    = 150;

const FILES = [
    'clawd-idle.gif',
    'clawd-thinking.gif',
    'clawd-typing.gif',
    'clawd-building.gif',
    'clawd-notification.gif',
    'clawd-idle-reading.gif',
    'clawd-react-annoyed.gif',
    'clawd-headphones-groove.gif',
    'clawd-happy.gif',
    'clawd-error.gif',
    'clawd-sleeping.gif',
];

async function main() {
    if (!fs.existsSync(DST_DIR))
        fs.mkdirSync(DST_DIR, { recursive: true });

    console.log(`Resizing clawd GIFs to ${SIZE}x${SIZE}...\n`);

    for (const f of FILES) {
        const src = path.join(SRC_DIR, f);
        const dst = path.join(DST_DIR, f);
        if (!fs.existsSync(src)) {
            console.error(`  SKIP ${f}: not found`);
            continue;
        }
        const srcKB = (fs.statSync(src).size / 1024).toFixed(1);

        await sharp(src, { animated: true })
            .resize(SIZE, SIZE, { fit: 'fill' })
            .gif()
            .toFile(dst);

        const dstKB = (fs.statSync(dst).size / 1024).toFixed(1);
        // Parse output dimensions
        const buf = fs.readFileSync(dst);
        const w = buf.readUInt16LE(6);
        const h = buf.readUInt16LE(8);
        let frames = 0;
        for (let i = 0; i < buf.length - 4; i++) {
            if (buf[i] === 0x00 && buf[i+1] === 0x21 && buf[i+2] === 0xF9 && buf[i+3] === 0x04)
                frames++;
        }
        console.log(`  ${f.padEnd(32)} ${srcKB}KB → ${dstKB}KB  ${w}x${h}  ${frames}frames`);
    }

    const totalKB = fs.readdirSync(DST_DIR).reduce((s, f) => s + fs.statSync(path.join(DST_DIR, f)).size, 0) / 1024;
    console.log(`\nDone → ${DST_DIR} (${totalKB.toFixed(0)} KB total)`);
}

main().catch(e => { console.error(e); process.exit(1); });
