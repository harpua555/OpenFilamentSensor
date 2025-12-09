#!/usr/bin/env node

/**
 * Simple development server for testing WebUI locally
 * Provides mock API endpoints matching the ESP32 backend
 */

const express = require('express');
const path = require('path');

const app = express();
const PORT = 5174;

// Middleware
app.use(express.json());
app.use(express.static(__dirname));

// Mock sensor status
app.get('/sensor_status', (req, res) => {
    res.json({
        is_running: true,
        filament_moving: Math.random() > 0.5,
        pulse_count: Math.floor(Math.random() * 10000),
        pulse_rate: Math.floor(Math.random() * 100),
        distance_mm: Math.random() * 500,
        uptime_ms: Date.now() - (Math.random() * 86400000),
        mac: '24:0A:C4:XX:XX:XX',
        ip: '192.168.1.100'
    });
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
    console.log('Settings updated:', currentSettings);
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
        firmware_version: 'v1.0.0-lite',
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

app.listen(PORT, () => {
    console.log(`\n🚀 Development server running!`);
    console.log(`\n   Local:   http://localhost:${PORT}`);
    console.log(`\n📝 Mock API endpoints active:`);
    console.log(`   GET  /sensor_status`);
    console.log(`   GET  /get_settings`);
    console.log(`   POST /update_settings`);
    console.log(`   GET  /discover_printer`);
    console.log(`   GET  /api/logs_live`);
    console.log(`   GET  /api/logs_text`);
    console.log(`   GET  /version`);
    console.log(`\n✨ Press Ctrl+C to stop\n`);
});
