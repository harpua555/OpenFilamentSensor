#!/usr/bin/env node

/**
 * Build script for lightweight WebUI.
 * Copies artifacts into data_lite/ (staging) and mirrors them to data/lite/
 * so LittleFS always has the latest assets.
 */

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const SOURCE_DIR = __dirname;
const STAGING_DIR = path.join(__dirname, '..', 'data_lite');
const FINAL_DIR = path.join(__dirname, '..', 'data', 'lite');

const files = [
    { src: 'index.html', dest: 'index.htm' },
    { src: 'lite_ota.js', dest: 'lite_ota.js' },
    { src: 'favicon.ico', dest: 'favicon.ico', skipGzip: true },
];

function ensureDir(dir) {
    if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true });
        console.log('Created directory:', dir);
    }
}

function copyRecursive(src, dest) {
    const stats = fs.statSync(src);
    if (stats.isDirectory()) {
        ensureDir(dest);
        for (const entry of fs.readdirSync(src)) {
            copyRecursive(path.join(src, entry), path.join(dest, entry));
        }
    } else {
        fs.copyFileSync(src, dest);
    }
}

console.log('\n=== Building lightweight WebUI ===\n');
// Clear staging directory to ensure clean build
fs.rmSync(STAGING_DIR, { recursive: true, force: true });
ensureDir(STAGING_DIR);

files.forEach(file => {
    const srcPath = path.join(SOURCE_DIR, file.src);
    if (!fs.existsSync(srcPath)) {
        if (file.skipGzip) {
            console.log(`Skipping ${file.src} (not found)`);
            return;
        }
        console.error(`Source file not found: ${srcPath}`);
        process.exit(1);
    }

    const destPath = path.join(STAGING_DIR, file.dest);
    const content = fs.readFileSync(srcPath);

    if (!file.skipGzip) {
        // For gzipped files, only create the .gz version to save space
        const gzipped = zlib.gzipSync(content, { level: 9 });
        fs.writeFileSync(destPath + '.gz', gzipped);
        console.log(`Gzipped ${file.src} -> ${file.dest}.gz (${gzipped.length} bytes)`);
    } else {
        // For non-gzipped files (e.g., favicon), copy as-is
        fs.writeFileSync(destPath, content);
        console.log(`Copied ${file.src} -> ${file.dest}`);
    }
});

// Mirror staging directory into data/lite
console.log('\nSyncing artifacts to data/lite ...');
fs.rmSync(FINAL_DIR, { recursive: true, force: true });
ensureDir(FINAL_DIR);
copyRecursive(STAGING_DIR, FINAL_DIR);
console.log(`Synced lightweight UI to ${FINAL_DIR}\n`);
