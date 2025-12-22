#!/usr/bin/env node

/**
 * Development server for testing WebUI locally
 * Provides mock API endpoints matching the ESP32 backend
 * 
 * Simulation Modes:
 *   [1] Normal print  - actual = expected - 3mm, pulses match expected
 *   [2] Hard jam      - filament stops moving, hard jam % rises
 *   [3] Soft jam      - filament slips, soft jam % rises
 *   [F] Freeze        - pause all value updates
 *   [R] Reset         - reset simulation to initial state
 */

const express = require('express');
const path = require('path');
const readline = require('readline');

const app = express();
const PORT = 5174;

// ============================================================================
// Simulation State
// ============================================================================

const MODES = {
    NORMAL: 'normal',
    HARD_JAM: 'hard_jam',
    SOFT_JAM: 'soft_jam'
};

let simState = {
    mode: MODES.NORMAL,
    frozen: false,
    isPrinting: true,
    startTime: Date.now(),

    // Print progress (mm of filament)
    expectedFilament: 0,
    actualFilament: 0,

    // Jam percentages
    hardJamPercent: 0,
    softJamPercent: 0,

    // Pulses
    movementPulses: 0,
};

// Growth rates per second
const FILAMENT_RATE_MM_PER_SEC = 2.0;  // Expected filament usage
const NORMAL_DEFICIT_MM = 3.0;          // Normal print: actual lags by 3mm
const HARD_JAM_RATE = 5.0;              // Hard jam % increase per second
const SOFT_JAM_RATE = 3.0;              // Soft jam % increase per second

function resetSimulation() {
    simState.startTime = Date.now();
    simState.expectedFilament = 0;
    simState.actualFilament = 0;
    simState.hardJamPercent = 0;
    simState.softJamPercent = 0;
    simState.movementPulses = 0;
    simState.isPrinting = true;
    console.log(`\n🔄 Simulation reset`);
    printStatus();
}

function updateSimulation() {
    if (simState.frozen || !simState.isPrinting) return;

    const elapsed = (Date.now() - simState.startTime) / 1000;
    const mmPerPulse = currentSettings.mm_per_pulse;

    // Expected filament grows steadily
    simState.expectedFilament = elapsed * FILAMENT_RATE_MM_PER_SEC;

    switch (simState.mode) {
        case MODES.NORMAL:
            // Actual tracks expected with a small constant deficit
            simState.actualFilament = Math.max(0, simState.expectedFilament - NORMAL_DEFICIT_MM);
            simState.hardJamPercent = Math.max(0, simState.hardJamPercent - 1); // Decay
            simState.softJamPercent = Math.max(0, simState.softJamPercent - 1); // Decay
            break;

        case MODES.HARD_JAM:
            // Actual filament stops moving (stays at current value)
            // Hard jam percentage rises
            simState.hardJamPercent = Math.min(100, simState.hardJamPercent + HARD_JAM_RATE / 10);
            simState.softJamPercent = Math.max(0, simState.softJamPercent - 0.5);
            break;

        case MODES.SOFT_JAM:
            // Actual moves slower than expected (slipping)
            simState.actualFilament += (FILAMENT_RATE_MM_PER_SEC * 0.5) / 10; // Half speed
            simState.softJamPercent = Math.min(100, simState.softJamPercent + SOFT_JAM_RATE / 10);
            simState.hardJamPercent = Math.max(0, simState.hardJamPercent - 0.5);
            break;
    }

    // Calculate pulses from actual filament movement
    simState.movementPulses = Math.floor(simState.actualFilament / mmPerPulse);
}

function printStatus() {
    const modeNames = {
        [MODES.NORMAL]: '✅ Normal',
        [MODES.HARD_JAM]: '🔴 Hard Jam',
        [MODES.SOFT_JAM]: '🟡 Soft Jam'
    };
    const freezeStatus = simState.frozen ? ' [FROZEN]' : '';
    console.log(`   Mode: ${modeNames[simState.mode]}${freezeStatus}`);
}

// Update simulation every 100ms
setInterval(updateSimulation, 100);

// ============================================================================
// Keyboard Controls
// ============================================================================

function setupKeyboardControls() {
    // Only set up keyboard controls if we're in a TTY
    if (!process.stdin.isTTY) {
        console.log('   (Keyboard controls disabled - not a TTY)');
        return;
    }

    try {
        readline.emitKeypressEvents(process.stdin);
        process.stdin.setRawMode(true);
        process.stdin.resume();  // Ensure stdin is flowing

        process.stdin.on('keypress', (str, key) => {
            if (!key) return;

            if (key.ctrl && key.name === 'c') {
                console.log('\n👋 Shutting down...');
                process.exit();
            }

            switch (key.name) {
                case '1':
                    simState.mode = MODES.NORMAL;
                    console.log(`\n✅ Mode: Normal print`);
                    printStatus();
                    break;
                case '2':
                    simState.mode = MODES.HARD_JAM;
                    console.log(`\n🔴 Mode: Hard jam simulation`);
                    printStatus();
                    break;
                case '3':
                    simState.mode = MODES.SOFT_JAM;
                    console.log(`\n🟡 Mode: Soft jam simulation`);
                    printStatus();
                    break;
                case 'f':
                    simState.frozen = !simState.frozen;
                    console.log(`\n${simState.frozen ? '⏸️  Frozen' : '▶️  Resumed'}`);
                    printStatus();
                    break;
                case 'r':
                    resetSimulation();
                    break;
                case 's':
                    // Print current state for debugging
                    console.log('\n📊 Current State:', JSON.stringify(simState, null, 2));
                    break;
            }
        });
    } catch (err) {
        console.log('   (Keyboard controls disabled - error setting up TTY)');
    }
}

// ============================================================================
// Express Middleware
// ============================================================================

app.use(express.json());

// Explicitly serve index.html for root route
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// Static files
app.use(express.static(__dirname));

// ============================================================================
// Mock API Endpoints
// ============================================================================

function buildStatusData() {
    const deficit = Math.max(0, simState.expectedFilament - simState.actualFilament);
    const passRatio = simState.expectedFilament > 0
        ? simState.actualFilament / simState.expectedFilament
        : 1.0;

    return {
        stopped: simState.mode === MODES.HARD_JAM && simState.hardJamPercent > 50,
        filamentRunout: false,
        elegoo: {
            isWebsocketConnected: true,
            printStatus: simState.isPrinting ? 2 : 0,  // 2 = printing
            expectedFilament: simState.expectedFilament,
            actualFilament: simState.actualFilament,
            currentDeficitMm: deficit,
            movementPulses: simState.movementPulses,
            hardJamPercent: simState.hardJamPercent,
            softJamPercent: simState.softJamPercent,
            passRatio: passRatio,
            ratioThreshold: 0.25,
            runoutPausePending: false,
            runoutPauseRemainingMm: 0,
            runoutPauseCommanded: false,
            uiRefreshIntervalMs: 1000
        },
        mac: '24:0A:C4:XX:XX:XX',
        ip: '192.168.1.100'
    };
}

// SSE status events endpoint
app.get('/status_events', (req, res) => {
    res.setHeader('Content-Type', 'text/event-stream');
    res.setHeader('Cache-Control', 'no-cache');
    res.setHeader('Connection', 'keep-alive');
    res.flushHeaders();

    const sendEvent = () => {
        const data = buildStatusData();
        res.write(`event: status\ndata: ${JSON.stringify(data)}\n\n`);
    };

    sendEvent();
    const interval = setInterval(sendEvent, 1000);
    req.on('close', () => clearInterval(interval));
});

// Sensor status endpoint
app.get('/sensor_status', (req, res) => {
    res.json(buildStatusData());
});

// Mock settings
let currentSettings = {
    wifi_ssid: 'MyNetwork',
    wifi_password: '',
    elegoo_ip: '192.168.1.150',
    pulse_pin: 4,
    debounce_ms: 50,
    motion_timeout_ms: 1000,
    mm_per_pulse: 1.0,
    enable_websocket: true
};

app.get('/get_settings', (req, res) => {
    res.json(currentSettings);
});

app.post('/update_settings', (req, res) => {
    currentSettings = { ...currentSettings, ...req.body };
    console.log('\n⚙️  Settings updated:', currentSettings);
    res.json({ success: true });
});

// Mock printer discovery
app.get('/discover_printer', (req, res) => {
    setTimeout(() => {
        res.json({ ip: '192.168.1.150' });
    }, 1000);
});

// Mock logs
const mockLogs = [
    '[INFO] System started',
    '[INFO] WiFi connected to MyNetwork',
    '[INFO] IP address: 192.168.1.100',
    '[INFO] Web server started on port 80',
    '[INFO] Sensor initialized on pin 4',
    '[DEBUG] Pulse detected',
    '[DEBUG] Pulse detected',
    '[INFO] Filament moving detected',
    '[DEBUG] Pulse detected',
    '[DEBUG] Pulse detected',
    '[WARNING] Pulse rate high: 85 Hz',
    '[DEBUG] Pulse detected',
    '[INFO] Total distance: 152.3 mm'
];

app.get('/api/logs_live', (req, res) => {
    res.json({ logs: mockLogs });
});

app.get('/api/logs_text', (req, res) => {
    res.type('text/plain').send(mockLogs.join('\n'));
});

app.post('/api/logs_clear', (req, res) => {
    console.log('\n🗑️  Logs cleared');
    res.json({ success: true });
});

// Mock version info
app.get('/version', (req, res) => {
    const now = new Date();
    const pad = (value) => String(value).padStart(2, '0');
    const thumbprint = [
        pad(now.getMonth() + 1),
        pad(now.getDate()),
        pad(now.getFullYear() % 100),
        pad(now.getHours()),
        pad(now.getMinutes()),
        pad(now.getSeconds())
    ].join('');

    res.json({
        firmware_version: 'v1.0.0-dev',
        firmware_thumbprint: thumbprint,
        filesystem_thumbprint: thumbprint,
        build_version: '1.0.0',
        chip_family: 'ESP32-S3'
    });
});

// Serve index.html for all other routes (SPA routing)
app.get('*', (req, res) => {
    if (!req.path.includes('.')) {
        res.sendFile(path.join(__dirname, 'index.html'));
    }
});

// ============================================================================
// Start Server
// ============================================================================

app.listen(PORT, () => {
    console.log(`\n🚀 Development server running!`);
    console.log(`\n   Local:   http://localhost:${PORT}`);
    console.log(`\n📝 Mock API endpoints active:`);
    console.log(`   GET  /sensor_status`);
    console.log(`   GET  /status_events (SSE)`);
    console.log(`   GET  /get_settings`);
    console.log(`   POST /update_settings`);
    console.log(`   GET  /discover_printer`);
    console.log(`   GET  /api/logs_live`);
    console.log(`   GET  /api/logs_text`);
    console.log(`   GET  /version`);
    console.log(`\n🎮 Keyboard Controls:`);
    console.log(`   [1] Normal print simulation`);
    console.log(`   [2] Hard jam simulation`);
    console.log(`   [3] Soft jam simulation`);
    console.log(`   [F] Freeze/unfreeze values`);
    console.log(`   [R] Reset simulation`);
    console.log(`   [S] Show current state`);
    console.log(`   [Ctrl+C] Quit`);
    console.log('');
    printStatus();
    console.log(`\n✨ Ready!\n`);

    setupKeyboardControls();
});
