/**
 * Unit tests for distributor JavaScript files
 *
 * Tests the WiFi patcher and web interface logic.
 */

const fs = require('fs');
const path = require('path');
const assert = require('assert');
const https = require('https');

// ANSI colors
const COLOR_GREEN = '\x1b[32m';
const COLOR_RED = '\x1b[31m';
const COLOR_YELLOW = '\x1b[33m';
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
        'styles.css'
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
    
    const boardsJsonPath = path.join(__dirname, '..', 'distributor', 'boards.json');
    
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
    console.log(`${COLOR_YELLOW}SKIP: Local manifest files no longer required; assets served from GitHub Pages.${COLOR_RESET}`);
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


// ============================================================================
// Additional Test Functions for Enhanced Coverage
// ============================================================================

function testFlasherJsStructure() {
    const flasherPath = path.join(__dirname, '..', 'distributor', 'flasher.js');
    
    if (fs.existsSync(flasherPath)) {
        const content = fs.readFileSync(flasherPath, 'utf8');
        
        // Check for EspFlasher class
        assert(content.includes('class EspFlasher') || content.includes('EspFlasher'),
               'flasher.js should define EspFlasher class');
        
        // Check for flash states
        assert(content.includes('FLASH_STATES') || content.includes('state'),
               'flasher.js should define flash states');
        
        // Check for esptool-js integration
        assert(content.includes('esptool') || content.includes('ESPLoader'),
               'flasher.js should integrate with esptool-js');
        
        // Check for Serial API usage
        assert(content.includes('serial') || content.includes('SerialPort'),
               'flasher.js should use Web Serial API');
        
        console.log(`${COLOR_GREEN}PASS: flasher.js has expected structure${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_RED}SKIP: flasher.js not found${COLOR_RESET}`);
    }
}

function testManifestVersionConsistency() {
    const boardsJsonPath = path.join(__dirname, '..', 'distributor', 'boards.json');
    if (fs.existsSync(boardsJsonPath)) {
        const boardsData = JSON.parse(fs.readFileSync(boardsJsonPath, 'utf8'));
        const rawBoards = boardsData.boards || [];
        const boards = Array.isArray(rawBoards) ? rawBoards : Object.values(rawBoards || {});
        const versions = boards.map(b => b.version).filter(Boolean);
        const uniqueVersions = [...new Set(versions)];
        if (uniqueVersions.length > 1) {
            console.log(`${COLOR_YELLOW}NOTE: Multiple board versions in boards.json: ${uniqueVersions.join(', ')}${COLOR_RESET}`);
        } else {
            console.log(`${COLOR_GREEN}PASS: Board versions are consistent in boards.json${COLOR_RESET}`);
            testsPassed++;
        }
    }
}

function testBoardChipFamilyValidation() {
    const boardsJsonPath = path.join(__dirname, '..', 'distributor', 'boards.json');
    
    if (fs.existsSync(boardsJsonPath)) {
        const boardsData = JSON.parse(fs.readFileSync(boardsJsonPath, 'utf8'));
        const rawBoards = boardsData.boards || [];
        const boards = Array.isArray(rawBoards) ? rawBoards : Object.values(rawBoards || {});
        
        const validChipFamilies = [
            'ESP32', 'ESP32-S2', 'ESP32-S3', 'ESP32-C3', 'ESP32-C6', 'ESP32-H2'
        ];
        
        boards.forEach(board => {
            assert(board.chipFamily, `Board ${board.id} should have chipFamily`);
            
            const isValid = validChipFamilies.some(family => 
                board.chipFamily.includes(family)
            );
            
            assert(isValid, 
                   `Board ${board.id} chipFamily "${board.chipFamily}" should be recognized ESP32 variant`);
        });
        
        console.log(`${COLOR_GREEN}PASS: All board chip families are valid${COLOR_RESET}`);
        testsPassed++;
    }
}

function testManifestBuildStructure() {
    const boardsJsonPath = path.join(__dirname, '..', 'distributor', 'boards.json');
    
    if (fs.existsSync(boardsJsonPath)) {
        console.log(`${COLOR_YELLOW}SKIP: Local manifest validation removed (assets served from Pages).${COLOR_RESET}`);
    }
}

function testFirmwareBinariesExist() {
    const boardsJsonPath = path.join(__dirname, '..', 'distributor', 'boards.json');
    
    if (fs.existsSync(boardsJsonPath)) {
        console.log(`${COLOR_YELLOW}SKIP: Local firmware binaries not required; served from Pages.${COLOR_RESET}`);
    }
}

function testDockerfileExists() {
    const dockerfilePath = path.join(__dirname, '..', 'distributor', 'Dockerfile');
    
    if (fs.existsSync(dockerfilePath)) {
        const content = fs.readFileSync(dockerfilePath, 'utf8');
        
        // Check for basic Dockerfile structure
        assert(content.includes('FROM'), 'Dockerfile should have FROM instruction');
        
        // Check for node/http server setup
        const hasNodeSetup = content.toLowerCase().includes('node') ||
                            content.toLowerCase().includes('npm');
        
        assert(hasNodeSetup || content.includes('EXPOSE'),
               'Dockerfile should setup web server or expose ports');
        
        console.log(`${COLOR_GREEN}PASS: Dockerfile exists and has valid structure${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_YELLOW}SKIP: Dockerfile not found${COLOR_RESET}`);
    }
}

function testStylesCssExists() {
    const stylesPath = path.join(__dirname, '..', 'distributor', 'styles.css');
    
    if (fs.existsSync(stylesPath)) {
        const content = fs.readFileSync(stylesPath, 'utf8');
        
        // Check for CSS rules
        assert(content.includes('{') && content.includes('}'),
               'styles.css should contain CSS rules');
        
        // Check file size is reasonable
        assert(content.length > 100,
               'styles.css should contain substantial styling');
        
        console.log(`${COLOR_GREEN}PASS: styles.css exists and appears valid${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_YELLOW}SKIP: styles.css not found${COLOR_RESET}`);
    }
}

function fetchHead(url) {
    return new Promise((resolve, reject) => {
        const req = https.request(url, { method: 'HEAD', timeout: 7000 }, (res) => {
            resolve({ status: res.statusCode || 0 });
        });
        req.on('error', reject);
        req.on('timeout', () => {
            req.destroy(new Error('Request timed out'));
        });
        req.end();
    });
}

async function testRemoteAssetsAccessible() {
    console.log('\n=== Test: Remote release assets on GitHub Pages ===');
    const boardsJsonPath = path.join(__dirname, '..', 'distributor', 'boards.json');
    if (!fs.existsSync(boardsJsonPath)) {
        console.log(`${COLOR_RED}SKIP: boards.json not found${COLOR_RESET}`);
        return;
    }

    const raw = fs.readFileSync(boardsJsonPath, 'utf8');
    const boardsObj = JSON.parse(raw);
    const boards = Array.isArray(boardsObj) ? boardsObj : boardsObj.boards || [];
    assert(boards.length > 0, 'boards.json must list at least one board');

    const tag = process.env.TEST_RELEASE_TAG || 'v0.6.3-alpha';
    let failures = 0;

    for (const board of boards) {
        const base = `https://harpua555.github.io/centauri-carbon-motion-detector/releases/${tag}/${board.id}`;
        const urls = [
            `${base}-firmware_merged.bin`,
            `${base}-firmware.bin`,
            `${base}-littlefs.bin`
        ];
        for (const url of urls) {
            try {
                const res = await fetchHead(url);
                assert(res.status === 200, `Expected 200 for ${url}, got ${res.status}`);
            } catch (err) {
                console.error(`${COLOR_RED}FAIL: ${url} unreachable (${err.message || err})${COLOR_RESET}`);
                failures++;
            }
        }
    }

    assert(failures === 0, 'Some remote assets were not reachable on GitHub Pages');
    console.log(`${COLOR_GREEN}PASS: Remote assets reachable for tag ${tag}${COLOR_RESET}`);
    testsPassed++;
}

function testWebUIFaviconExists() {
    const faviconPaths = [
        path.join(__dirname, '..', 'distributor', 'favicon.ico'),
        path.join(__dirname, '..', 'webui_lite', 'favicon.ico'),
        path.join(__dirname, '..', 'data', 'lite', 'favicon.ico')
    ];
    
    let foundFavicon = false;
    faviconPaths.forEach(faviconPath => {
        if (fs.existsSync(faviconPath)) {
            const stats = fs.statSync(faviconPath);
            assert(stats.size > 0, `Favicon at ${faviconPath} should not be empty`);
            foundFavicon = true;
        }
    });
    
    if (foundFavicon) {
        console.log(`${COLOR_GREEN}PASS: Favicon(s) found${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_YELLOW}SKIP: No favicons found${COLOR_RESET}`);
    }
}

function testWebUIBuildScript() {
    const buildScriptPath = path.join(__dirname, '..', 'webui_lite', 'build.js');
    
    if (fs.existsSync(buildScriptPath)) {
        const content = fs.readFileSync(buildScriptPath, 'utf8');
        
        // Check for build logic
        assert(content.includes('fs') || content.includes('file'),
               'build.js should handle file operations');
        
        // Check for minification or compression
        const hasOptimization = content.includes('gzip') ||
                               content.includes('compress') ||
                               content.includes('minify');
        
        if (!hasOptimization) {
            console.log(`${COLOR_YELLOW}NOTE: build.js might not include optimization${COLOR_RESET}`);
        }
        
        console.log(`${COLOR_GREEN}PASS: build.js exists and has expected structure${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_YELLOW}SKIP: build.js not found${COLOR_RESET}`);
    }
}

function testOTAReadmeFiles() {
    const otaReadmePaths = [
        path.join(__dirname, '..', 'distributor', 'firmware', 'esp32s3', 'OTA', 'OTA_readme.md'),
        path.join(__dirname, '..', 'distributor', 'firmware', 'esp32c3', 'OTA', 'OTA_readme.md'),
        path.join(__dirname, '..', 'distributor', 'firmware', 'seeed_esp32c3', 'OTA', 'OTA_readme.md')
    ];
    
    let foundReadme = false;
    otaReadmePaths.forEach(readmePath => {
        if (fs.existsSync(readmePath)) {
            const content = fs.readFileSync(readmePath, 'utf8');
            assert(content.length > 0, `OTA readme at ${readmePath} should not be empty`);
            foundReadme = true;
        }
    });
    
    if (foundReadme) {
        console.log(`${COLOR_GREEN}PASS: OTA readme file(s) found${COLOR_RESET}`);
        testsPassed++;
    } else {
        console.log(`${COLOR_YELLOW}SKIP: No OTA readme files found${COLOR_RESET}`);
    }
}

async function runAllTests() {
    console.log('\n========================================');
    console.log('  Distributor & WebUI Test Suite');
    console.log('  (Enhanced Coverage)');
    console.log('========================================');
    
    try {
        // Original tests
        testDistributorFilesExist();
        testBoardsJsonValid();
        testManifestJsonValid();
        testWifiPatcherStructure();
        testAppJsStructure();
        testIndexHtmlValid();
        testWebUILiteFiles();
        testLiteOTAJsStructure();
        testDevServerJs();
        await testRemoteAssetsAccessible();
        
        // Additional comprehensive tests
        testFlasherJsStructure();
        testManifestVersionConsistency();
        testBoardChipFamilyValidation();
        testManifestBuildStructure();
        testFirmwareBinariesExist();
        testDockerfileExists();
        testStylesCssExists();
        testWebUIFaviconExists();
        testWebUIBuildScript();
        testOTAReadmeFiles();
    } catch (error) {
        console.log(`${COLOR_RED}TEST ERROR: ${error.message}${COLOR_RESET}`);
        console.log(error.stack);
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
runAllTests().then((code) => process.exit(code)).catch((err) => {
    console.log(`${COLOR_RED}TEST ERROR: ${err.message}${COLOR_RESET}`);
    process.exit(1);
});
