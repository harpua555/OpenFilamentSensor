/**
 * JavaScript Validation Tests
 * 
 * Tests for JavaScript files in the project:
 * - distributor/app.js
 * - distributor/wifiPatcher.js
 * - webui_lite/lite_ota.js
 */

const fs = require('fs');
const path = require('path');

// ANSI color codes
const colors = {
    reset: '\x1b[0m',
    red: '\x1b[31m',
    green: '\x1b[32m',
    yellow: '\x1b[33m',
    blue: '\x1b[34m',
    cyan: '\x1b[36m'
};

let totalTests = 0;
let passedTests = 0;
let failedTests = 0;

function test(description, fn) {
    totalTests++;
    try {
        fn();
        console.log(`${colors.green}[PASS]${colors.reset} ${description}`);
        passedTests++;
        return true;
    } catch (error) {
        console.log(`${colors.red}[FAIL]${colors.reset} ${description}`);
        console.log(`  ${colors.red}Error: ${error.message}${colors.reset}`);
        failedTests++;
        return false;
    }
}

function assert(condition, message) {
    if (!condition) {
        throw new Error(message || 'Assertion failed');
    }
}

function assertEquals(actual, expected, message) {
    if (actual !== expected) {
        throw new Error(message || `Expected ${expected}, got ${actual}`);
    }
}

function assertExists(filePath, message) {
    if (!fs.existsSync(filePath)) {
        throw new Error(message || `File does not exist: ${filePath}`);
    }
}

function assertValidJS(filePath) {
    const content = fs.readFileSync(filePath, 'utf8');
    
    // Check for basic syntax issues
    assert(content.length > 0, 'File should not be empty');
    
    // Check for unmatched braces
    const openBraces = (content.match(/{/g) || []).length;
    const closeBraces = (content.match(/}/g) || []).length;
    assert(openBraces === closeBraces, 'Unmatched braces detected');
    
    // Check for unmatched parentheses
    const openParens = (content.match(/\(/g) || []).length;
    const closeParens = (content.match(/\)/g) || []).length;
    assert(openParens === closeParens, 'Unmatched parentheses detected');
    
    // Check for unmatched brackets
    const openBrackets = (content.match(/\[/g) || []).length;
    const closeBrackets = (content.match(/]/g) || []).length;
    assert(openBrackets === closeBrackets, 'Unmatched brackets detected');
}

function assertValidJSON(filePath) {
    const content = fs.readFileSync(filePath, 'utf8');
    JSON.parse(content); // Will throw if invalid
}

console.log(`${colors.blue}`);
console.log('╔════════════════════════════════════════════════════════════╗');
console.log('║          JavaScript Validation Test Suite                 ║');
console.log('╚════════════════════════════════════════════════════════════╝');
console.log(`${colors.reset}\n`);

// Test distributor/app.js
console.log(`${colors.cyan}=== Testing distributor/app.js ===${colors.reset}`);

test('distributor/app.js exists', () => {
    assertExists(path.join(__dirname, '../distributor/app.js'));
});

test('distributor/app.js has valid syntax', () => {
    assertValidJS(path.join(__dirname, '../distributor/app.js'));
});

test('distributor/app.js contains expected imports', () => {
    const content = fs.readFileSync(path.join(__dirname, '../distributor/app.js'), 'utf8');
    assert(content.includes('initWifiPatcher'), 'Should import wifiPatcher');
    assert(content.includes('import'), 'Should use ES6 imports');
});

test('distributor/app.js defines state object', () => {
    const content = fs.readFileSync(path.join(__dirname, '../distributor/app.js'), 'utf8');
    assert(content.includes('const state'), 'Should define state object');
    assert(content.includes('boards:'), 'State should track boards');
    assert(content.includes('selected:'), 'State should track selection');
});

test('distributor/app.js has log management functions', () => {
    const content = fs.readFileSync(path.join(__dirname, '../distributor/app.js'), 'utf8');
    assert(content.includes('appendLog'), 'Should have appendLog function');
    assert(content.includes('installConsoleCapture'), 'Should capture console output');
});

// Test distributor/wifiPatcher.js
console.log(`\n${colors.cyan}=== Testing distributor/wifiPatcher.js ===${colors.reset}`);

test('distributor/wifiPatcher.js exists', () => {
    assertExists(path.join(__dirname, '../distributor/wifiPatcher.js'));
});

test('distributor/wifiPatcher.js has valid syntax', () => {
    assertValidJS(path.join(__dirname, '../distributor/wifiPatcher.js'));
});

test('distributor/wifiPatcher.js exports initWifiPatcher', () => {
    const content = fs.readFileSync(path.join(__dirname, '../distributor/wifiPatcher.js'), 'utf8');
    assert(content.includes('export') && content.includes('initWifiPatcher'), 
           'Should export initWifiPatcher function');
});

test('distributor/wifiPatcher.js handles WiFi credentials', () => {
    const content = fs.readFileSync(path.join(__dirname, '../distributor/wifiPatcher.js'), 'utf8');
    assert(content.includes('ssid') || content.includes('SSID'), 'Should handle SSID');
    assert(content.includes('password') || content.includes('passwd'), 'Should handle password');
});

// Test webui_lite/lite_ota.js
console.log(`\n${colors.cyan}=== Testing webui_lite/lite_ota.js ===${colors.reset}`);

test('webui_lite/lite_ota.js exists', () => {
    assertExists(path.join(__dirname, '../webui_lite/lite_ota.js'));
});

test('webui_lite/lite_ota.js has valid syntax', () => {
    assertValidJS(path.join(__dirname, '../webui_lite/lite_ota.js'));
});

test('webui_lite/lite_ota.js contains OTA functionality', () => {
    const content = fs.readFileSync(path.join(__dirname, '../webui_lite/lite_ota.js'), 'utf8');
    assert(content.includes('fetch') || content.includes('XMLHttpRequest'), 
           'Should make HTTP requests');
});

// Test webui_lite/build.js
console.log(`\n${colors.cyan}=== Testing webui_lite/build.js ===${colors.reset}`);

test('webui_lite/build.js exists', () => {
    assertExists(path.join(__dirname, '../webui_lite/build.js'));
});

test('webui_lite/build.js has valid syntax', () => {
    assertValidJS(path.join(__dirname, '../webui_lite/build.js'));
});

test('webui_lite/build.js has build logic', () => {
    const content = fs.readFileSync(path.join(__dirname, '../webui_lite/build.js'), 'utf8');
    assert(content.includes('fs') || content.includes('readFile') || content.includes('writeFile'),
           'Should perform file operations');
});

// Test webui_lite/dev-server.js
console.log(`\n${colors.cyan}=== Testing webui_lite/dev-server.js ===${colors.reset}`);

test('webui_lite/dev-server.js exists', () => {
    assertExists(path.join(__dirname, '../webui_lite/dev-server.js'));
});

test('webui_lite/dev-server.js has valid syntax', () => {
    assertValidJS(path.join(__dirname, '../webui_lite/dev-server.js'));
});

test('webui_lite/dev-server.js sets up server', () => {
    const content = fs.readFileSync(path.join(__dirname, '../webui_lite/dev-server.js'), 'utf8');
    assert(content.includes('express') || content.includes('http') || content.includes('listen'),
           'Should set up HTTP server');
});

// Test JSON configuration files
console.log(`\n${colors.cyan}=== Testing JSON Configuration Files ===${colors.reset}`);

test('webui_lite/package.json is valid', () => {
    assertValidJSON(path.join(__dirname, '../webui_lite/package.json'));
});

test('webui_lite/package.json has required fields', () => {
    const pkg = JSON.parse(fs.readFileSync(path.join(__dirname, '../webui_lite/package.json'), 'utf8'));
    assert(pkg.name, 'Should have name field');
    assert(pkg.scripts, 'Should have scripts field');
    assert(pkg.scripts.build, 'Should have build script');
});

test('data/user_settings.json is valid', () => {
    assertValidJSON(path.join(__dirname, '../data/user_settings.json'));
});

test('data/user_settings.json has detection settings', () => {
    const settings = JSON.parse(fs.readFileSync(path.join(__dirname, '../data/user_settings.json'), 'utf8'));
    assert('detection_ratio_threshold' in settings, 'Should have detection_ratio_threshold');
    assert('detection_hard_jam_mm' in settings, 'Should have detection_hard_jam_mm');
    assert('detection_soft_jam_time_ms' in settings, 'Should have detection_soft_jam_time_ms');
});

// Test distributor firmware manifests
console.log(`\n${colors.cyan}=== Testing Distributor Manifests ===${colors.reset}`);

const manifestDirs = [
    '../distributor/firmware/esp32',
    '../distributor/firmware/esp32c3supermini',
    '../distributor/firmware/esp32s3',
    '../distributor/firmware/seeed_esp32c3'
];

manifestDirs.forEach(dir => {
    const manifestPath = path.join(__dirname, dir, 'manifest.json');
    if (fs.existsSync(manifestPath)) {
        test(`${dir}/manifest.json is valid JSON`, () => {
            assertValidJSON(manifestPath);
        });
        
        test(`${dir}/manifest.json has required fields`, () => {
            const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
            assert(manifest.name || manifest.version || Array.isArray(manifest.builds), 
                   'Manifest should have name, version, or builds array');
        });
    }
});

test('distributor/firmware/boards.json is valid', () => {
    const boardsPath = path.join(__dirname, '../distributor/firmware/boards.json');
    if (fs.existsSync(boardsPath)) {
        assertValidJSON(boardsPath);
    }
});

// Print summary
console.log(`\n${colors.blue}╔════════════════════════════════════════════════════════════╗${colors.reset}`);
console.log(`${colors.blue}║                      Test Summary                          ║${colors.reset}`);
console.log(`${colors.blue}╚════════════════════════════════════════════════════════════╝${colors.reset}\n`);

console.log(`Total tests: ${totalTests}`);
console.log(`${colors.green}Passed: ${passedTests}${colors.reset}`);
console.log(`${failedTests > 0 ? colors.red : colors.green}Failed: ${failedTests}${colors.reset}\n`);

if (failedTests === 0) {
    console.log(`${colors.green}╔════════════════════════════════════════════════════════════╗${colors.reset}`);
    console.log(`${colors.green}║              ✓ ALL TESTS PASSED ✓                         ║${colors.reset}`);
    console.log(`${colors.green}╚════════════════════════════════════════════════════════════╝${colors.reset}\n`);
    process.exit(0);
} else {
    console.log(`${colors.red}╔════════════════════════════════════════════════════════════╗${colors.reset}`);
    console.log(`${colors.red}║              ✗ SOME TESTS FAILED ✗                        ║${colors.reset}`);
    console.log(`${colors.red}╚════════════════════════════════════════════════════════════╝${colors.reset}\n`);
    process.exit(1);
}