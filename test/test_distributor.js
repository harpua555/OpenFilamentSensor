/**
 * Unit tests for distributor JavaScript files
 *
 * Tests the WiFi patcher and web interface logic.
 */

const fs = require('fs');
const path = require('path');
const assert = require('assert');

// ANSI colors
const COLOR_GREEN = '\x1b[32m';
const COLOR_RED = '\x1b[31m';
const COLOR_RESET = '\x1b[0m';

let testsPassed = 0;
let testsFailed = 0;

function testDistributorFilesExist() {
    console.log('\n=== Test: Distributor Files Exist ===');
    
    const distributorDir = path.join(__dirname, '..', 'distributor');
    const requiredFiles = [
        'app.js',
        'wifiPatcher.js',
        'index.html',
        'styles.css',
        'firmware/boards.json'
    ];
    
    for (const file of requiredFiles) {
        const filepath = path.join(distributorDir, file);
        assert(fs.existsSync(filepath), `Required file not found: ${file}`);
    }
    
    console.log(`${COLOR_GREEN}PASS: All distributor files exist${COLOR_RESET}`);
    testsPassed++;
}

function testBoardsJsonValid() {
    console.log('\n=== Test: boards.json Valid JSON ===');
    
    const boardsJsonPath = path.join(__dirname, '..', 'distributor', 'firmware', 'boards.json');
    
    if (fs.existsSync(boardsJsonPath)) {
        const content = fs.readFileSync(boardsJsonPath, 'utf8');
        let boards;
        
        try {
            boards = JSON.parse(content);
        } catch (e) {
            assert.fail(`boards.json is not valid JSON: ${e.message}`);
        }
        
        // Verify structure
        assert(Array.isArray(boards) || typeof boards === 'object',
               'boards.json should be an array or object');
        
        console.log(`${COLOR_GREEN}PASS: boards.json is valid JSON${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_RED}SKIP: boards.json not found${COLOR_RESET}`);
    }
}

function testManifestJsonValid() {
    console.log('\n=== Test: Manifest JSON Files Valid ===');
    
    const firmwareDir = path.join(__dirname, '..', 'distributor', 'firmware');
    
    if (!fs.existsSync(firmwareDir)) {
        console.log(`${COLOR_RED}SKIP: Firmware directory not found${COLOR_RESET}`);
        return;
    }
    
    const boardDirs = fs.readdirSync(firmwareDir, { withFileTypes: true })
        .filter(dirent => dirent.isDirectory())
        .map(dirent => dirent.name);
    
    let manifestCount = 0;
    
    for (const boardDir of boardDirs) {
        const manifestPath = path.join(firmwareDir, boardDir, 'manifest.json');
        
        if (fs.existsSync(manifestPath)) {
            const content = fs.readFileSync(manifestPath, 'utf8');
            try {
                const manifest = JSON.parse(content);
                
                // Verify expected structure
                assert(manifest.name, 'manifest.json should have a name field');
                assert(manifest.version || manifest.builds,
                       'manifest.json should have version or builds field');
                
                manifestCount++;
            } catch (e) {
                assert.fail(`Invalid JSON in ${boardDir}/manifest.json: ${e.message}`);
            }
        }
    }
    
    if (manifestCount > 0) {
        console.log(`${COLOR_GREEN}PASS: All ${manifestCount} manifest.json files are valid${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_RED}SKIP: No manifest.json files found${COLOR_RESET}`);
    }
}

function testWifiPatcherStructure() {
    console.log('\n=== Test: wifiPatcher.js Structure ===');
    
    const wifiPatcherPath = path.join(__dirname, '..', 'distributor', 'wifiPatcher.js');
    
    if (fs.existsSync(wifiPatcherPath)) {
        const content = fs.readFileSync(wifiPatcherPath, 'utf8');
        
        // Check for expected function/class definitions
        assert(content.includes('function') || content.includes('=>') || content.includes('class'),
               'wifiPatcher.js should contain functions or classes');
        
        // Check for WiFi-related logic
        const hasWifiLogic = content.toLowerCase().includes('ssid') ||
                           content.toLowerCase().includes('wifi') ||
                           content.toLowerCase().includes('password');
        
        assert(hasWifiLogic, 'wifiPatcher.js should contain WiFi-related logic');
        
        console.log(`${COLOR_GREEN}PASS: wifiPatcher.js has expected structure${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_RED}SKIP: wifiPatcher.js not found${COLOR_RESET}`);
    }
}

function testAppJsStructure() {
    console.log('\n=== Test: app.js Structure ===');
    
    const appJsPath = path.join(__dirname, '..', 'distributor', 'app.js');
    
    if (fs.existsSync(appJsPath)) {
        const content = fs.readFileSync(appJsPath, 'utf8');
        
        // Check for Node.js/Express patterns
        const hasServerLogic = content.includes('require') ||
                              content.includes('import') ||
                              content.includes('express') ||
                              content.includes('http');
        
        assert(hasServerLogic, 'app.js should contain server-related code');
        
        console.log(`${COLOR_GREEN}PASS: app.js has expected structure${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_RED}SKIP: app.js not found${COLOR_RESET}`);
    }
}

function testIndexHtmlValid() {
    console.log('\n=== Test: index.html Valid HTML ===');
    
    const indexPath = path.join(__dirname, '..', 'distributor', 'index.html');
    
    if (fs.existsSync(indexPath)) {
        const content = fs.readFileSync(indexPath, 'utf8');
        
        // Basic HTML validation
        assert(content.includes('<!DOCTYPE') || content.includes('<html'),
               'index.html should be valid HTML');
        assert(content.includes('</html>'), 'index.html should have closing html tag');
        
        // Check for critical elements
        assert(content.includes('<body'), 'index.html should have body tag');
        
        console.log(`${COLOR_GREEN}PASS: index.html is valid HTML${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_RED}SKIP: index.html not found${COLOR_RESET}`);
    }
}

function testWebUILiteFiles() {
    console.log('\n=== Test: WebUI Lite Files ===');
    
    const webuiDir = path.join(__dirname, '..', 'webui_lite');
    const requiredFiles = [
        'index.html',
        'lite_ota.js',
        'build.js',
        'dev-server.js'
    ];
    
    let existCount = 0;
    
    for (const file of requiredFiles) {
        const filepath = path.join(webuiDir, file);
        if (fs.existsSync(filepath)) {
            existCount++;
        }
    }
    
    assert(existCount > 0, 'At least some WebUI lite files should exist');
    
    console.log(`${COLOR_GREEN}PASS: ${existCount}/${requiredFiles.length} WebUI lite files found${COLOR_RESET}`);
    testsPassed++;
}

function testLiteOTAJsStructure() {
    console.log('\n=== Test: lite_ota.js Structure ===');
    
    const liteOtaPath = path.join(__dirname, '..', 'webui_lite', 'lite_ota.js');
    
    if (fs.existsSync(liteOtaPath)) {
        const content = fs.readFileSync(liteOtaPath, 'utf8');
        
        // Check for OTA-related logic
        const hasOtaLogic = content.toLowerCase().includes('ota') ||
                          content.toLowerCase().includes('update') ||
                          content.toLowerCase().includes('upload') ||
                          content.toLowerCase().includes('firmware');
        
        assert(hasOtaLogic, 'lite_ota.js should contain OTA-related logic');
        
        console.log(`${COLOR_GREEN}PASS: lite_ota.js has expected structure${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_RED}SKIP: lite_ota.js not found${COLOR_RESET}`);
    }
}

function testDevServerJs() {
    console.log('\n=== Test: dev-server.js Structure ===');
    
    const devServerPath = path.join(__dirname, '..', 'webui_lite', 'dev-server.js');
    
    if (fs.existsSync(devServerPath)) {
        const content = fs.readFileSync(devServerPath, 'utf8');
        
        // Check for server patterns
        const hasServerCode = content.includes('require') ||
                             content.includes('import') ||
                             content.includes('http') ||
                             content.includes('server');
        
        assert(hasServerCode, 'dev-server.js should contain server code');
        
        console.log(`${COLOR_GREEN}PASS: dev-server.js has expected structure${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_RED}SKIP: dev-server.js not found${COLOR_RESET}`);
    }
}

function runAllTests() {
    console.log('\n========================================');
    console.log('  Distributor & WebUI Test Suite');
    console.log('========================================');
    
    try {
        testDistributorFilesExist();
        testBoardsJsonValid();
        testManifestJsonValid();
        testWifiPatcherStructure();
        testAppJsStructure();
        testIndexHtmlValid();
        testWebUILiteFiles();
        testLiteOTAJsStructure();
        testDevServerJs();
    } catch (error) {
        console.log(`${COLOR_RED}TEST ERROR: ${error.message}${COLOR_RESET}`);
        testsFailed++;
    }
    
    console.log('\n========================================');
    console.log('Test Results:');
    console.log(`${COLOR_GREEN}  Passed: ${testsPassed}${COLOR_RESET}`);
    if (testsFailed > 0) {
        console.log(`${COLOR_RED}  Failed: ${testsFailed}${COLOR_RESET}`);
    }
    console.log('========================================\n');
    
    return testsFailed > 0 ? 1 : 0;
}

// Run tests
process.exit(runAllTests());