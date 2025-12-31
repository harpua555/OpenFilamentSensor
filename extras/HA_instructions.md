# Home Assistant Integration

This guide explains how to add OpenFilamentSensor (OFS) data to Home Assistant using REST sensors.

## Prerequisites

- OFS device connected to your network
- Home Assistant with access to your OFS IP (if running in Docker, use `network_mode: host`)
- OFS IP address (e.g., `192.168.0.150`)

## Step 1: Add REST and Template Sensors

Edit your `sensor.yaml` file (or wherever your sensor configuration lives) and add:

```yaml
  - platform: rest
    resource: http://<YOUR-OFS-IP>/sensor_status
    name: ofs_status
    scan_interval: 5
    json_attributes:
      - stopped
      - filamentRunout
      - mac
      - ip
      - elegoo
    value_template: "{{ value_json.elegoo.isWebsocketConnected }}"

  - platform: template
    sensors:
      # === Connection Status ===
      ofs_printer_connected:
        friendly_name: "OFS Printer Connected"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').isWebsocketConnected }}"

      # === Print State ===
      ofs_printing:
        friendly_name: "OFS Is Printing"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').isPrinting }}"
      ofs_print_status:
        friendly_name: "OFS Print Status"
        value_template: >
          {% set status = state_attr('sensor.ofs_status', 'elegoo').printStatus %}
          {% set status_map = {
            0: 'Idle',
            1: 'Homing',
            2: 'Descending',
            3: 'Exposing',
            4: 'Lifting',
            5: 'Pausing',
            6: 'Paused',
            7: 'Stopping',
            8: 'Stopped',
            9: 'Complete',
            10: 'File Checking',
            13: 'Printing',
            16: 'Heating',
            20: 'Bed Leveling'
          } %}
          {{ status_map.get(status, 'Unknown (' ~ status ~ ')') }}
      ofs_print_status_code:
        friendly_name: "OFS Print Status Code"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').printStatus }}"

      # === Jam Detection ===
      ofs_filament_stopped:
        friendly_name: "OFS Filament Stopped"
        value_template: "{{ state_attr('sensor.ofs_status', 'stopped') }}"
      ofs_hard_jam_proximity:
        friendly_name: "OFS Hard Jam %"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').hardJamPercent | round(0) }}"
        unit_of_measurement: "%"
      ofs_soft_jam_proximity:
        friendly_name: "OFS Soft Jam %"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').softJamPercent | round(0) }}"
        unit_of_measurement: "%"
      ofs_grace_active:
        friendly_name: "OFS Grace Active"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').graceActive }}"
      ofs_grace_state:
        friendly_name: "OFS Grace State"
        value_template: >
          {% set state = state_attr('sensor.ofs_status', 'elegoo').graceState %}
          {% set state_map = {
            0: 'Idle',
            1: 'Start Grace',
            2: 'Resume Grace',
            3: 'Active',
            4: 'Jammed'
          } %}
          {{ state_map.get(state, 'Unknown (' ~ state ~ ')') }}

      # === Filament Tracking ===
      ofs_expected:
        friendly_name: "OFS Expected Filament"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').expectedFilament | round(2) }}"
        unit_of_measurement: "mm"
      ofs_actual:
        friendly_name: "OFS Actual Filament"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').actualFilament | round(2) }}"
        unit_of_measurement: "mm"
      ofs_expected_delta:
        friendly_name: "OFS Expected Delta"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').expectedDelta | round(2) }}"
        unit_of_measurement: "mm"
      ofs_deficit:
        friendly_name: "OFS Current Deficit"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').currentDeficitMm | round(2) }}"
        unit_of_measurement: "mm"
      ofs_deficit_threshold:
        friendly_name: "OFS Deficit Threshold"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').deficitThresholdMm | round(2) }}"
        unit_of_measurement: "mm"
      ofs_deficit_ratio:
        friendly_name: "OFS Deficit Ratio"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').deficitRatio | round(3) }}"
      ofs_pass_ratio:
        friendly_name: "OFS Pass Ratio"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').passRatio | round(3) }}"
      ofs_ratio_threshold:
        friendly_name: "OFS Ratio Threshold"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').ratioThreshold | round(3) }}"
      ofs_expected_rate:
        friendly_name: "OFS Expected Rate"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').expectedRateMmPerSec | round(2) }}"
        unit_of_measurement: "mm/s"

      # === Physical Sensors ===
      ofs_filament_runout:
        friendly_name: "OFS Filament Runout"
        value_template: "{{ state_attr('sensor.ofs_status', 'filamentRunout') }}"
      ofs_pulses:
        friendly_name: "OFS Movement Pulses"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').movementPulses }}"
      ofs_telemetry_available:
        friendly_name: "OFS Telemetry Available"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').telemetryAvailable }}"

      # === Print Progress ===
      ofs_current_layer:
        friendly_name: "OFS Current Layer"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').currentLayer }}"
      ofs_total_layers:
        friendly_name: "OFS Total Layers"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').totalLayer }}"
      ofs_progress:
        friendly_name: "OFS Progress"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').progress }}"
        unit_of_measurement: "%"
      ofs_current_z:
        friendly_name: "OFS Z Height"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').currentZ | round(2) }}"
        unit_of_measurement: "mm"
      ofs_current_ticks:
        friendly_name: "OFS Current Ticks"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').currentTicks }}"
      ofs_total_ticks:
        friendly_name: "OFS Total Ticks"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').totalTicks }}"
      ofs_print_speed:
        friendly_name: "OFS Print Speed"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').PrintSpeedPct }}"
        unit_of_measurement: "%"

      # === Device Info ===
      ofs_mac:
        friendly_name: "OFS MAC Address"
        value_template: "{{ state_attr('sensor.ofs_status', 'mac') }}"
      ofs_ip:
        friendly_name: "OFS IP Address"
        value_template: "{{ state_attr('sensor.ofs_status', 'ip') }}"
      ofs_mainboard_id:
        friendly_name: "OFS Mainboard ID"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').mainboardID }}"

      # === Settings (read-only) ===
      ofs_ui_refresh_interval:
        friendly_name: "OFS UI Refresh Interval"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').uiRefreshIntervalMs }}"
        unit_of_measurement: "ms"
      ofs_telemetry_stale_timeout:
        friendly_name: "OFS Telemetry Stale Timeout"
        value_template: "{{ state_attr('sensor.ofs_status', 'elegoo').flowTelemetryStaleMs }}"
        unit_of_measurement: "ms"
```

Replace `<YOUR-OFS-IP>` with your OFS device's IP address.

## Step 2: Add Automations

Add these to your `automations.yaml` file:

```yaml
- alias: "OFS: Notify on Filament Jam"
  description: "Send notification when a filament jam is detected"
  trigger:
    - platform: state
      entity_id: sensor.ofs_filament_stopped
      to: "True"
  condition: []
  action:
    - service: notify.mobile_app_<YOUR_PHONE>
      data:
        title: "Filament Jam Detected!"
        message: >
          Print paused at layer {{ states('sensor.ofs_current_layer') }}/{{ states('sensor.ofs_total_layers') }}.
          Hard jam: {{ states('sensor.ofs_hard_jam_proximity') }}%,
          Soft jam: {{ states('sensor.ofs_soft_jam_proximity') }}%
        data:
          tag: ofs_jam
          priority: high
          ttl: 0

- alias: "OFS: Notify on Print Complete"
  description: "Send notification when print completes"
  trigger:
    - platform: state
      entity_id: sensor.ofs_print_status
      to: "Complete"
  condition: []
  action:
    - service: notify.mobile_app_<YOUR_PHONE>
      data:
        title: "Print Complete!"
        message: >
          Your print has finished.
          Total layers: {{ states('sensor.ofs_total_layers') }},
          Filament used: {{ states('sensor.ofs_actual') }} mm
        data:
          tag: ofs_complete
          priority: high
          ttl: 0
```

Replace `<YOUR_PHONE>` with your mobile app service name (e.g., `mobile_app_pixel_7`).

To find your mobile app service name:
1. Go to **Developer Tools → Services**
2. Search for "notify.mobile_app"
3. Your device will be listed

## Step 3: Restart Home Assistant

After saving the configuration:

1. Go to **Developer Tools → YAML → Check Configuration**
2. If valid, restart Home Assistant

## Step 4: Verify Sensors

1. Go to **Developer Tools → States**
2. Search for "ofs"
3. You should see sensors with current values

### Troubleshooting

If sensors show "unavailable":

1. **Test connectivity** - Run this from HA shell or the host:
   ```bash
   curl http://<YOUR-OFS-IP>/sensor_status
   ```

2. **Docker networking** - If HA runs in Docker, ensure `network_mode: host` is set in your docker-compose.yml:
   ```yaml
   homeassistant:
     network_mode: host
   ```

3. **Debug templates** - Go to **Developer Tools → Template** and test:
   ```jinja
   {% set ofs = state_attr('sensor.ofs_status', 'elegoo') %}
   elegoo attribute exists: {{ ofs is not none }}
   stopped attribute: {{ state_attr('sensor.ofs_status', 'stopped') }}
   ```

## Step 5: Add Dashboard Card

1. Go to your Dashboard
2. Click **⋮ → Edit Dashboard**
3. Click **+ Add Card → Entities**
4. Search for "ofs" and add the sensors you want
5. Save

### Example Entities Card (Full)

```yaml
type: entities
title: Filament Sensor
entities:
  - entity: sensor.ofs_printer_connected
    name: Printer Connected
  - entity: sensor.ofs_print_status
    name: Status
  - entity: sensor.ofs_printing
    name: Printing
  - type: divider
  - entity: sensor.ofs_filament_stopped
    name: Jam Detected
  - entity: sensor.ofs_hard_jam_proximity
    name: Hard Jam %
  - entity: sensor.ofs_soft_jam_proximity
    name: Soft Jam %
  - entity: sensor.ofs_grace_active
    name: Grace Active
  - type: divider
  - entity: sensor.ofs_current_layer
    name: Layer
  - entity: sensor.ofs_progress
    name: Progress
  - entity: sensor.ofs_current_z
    name: Z Height
  - type: divider
  - entity: sensor.ofs_expected
    name: Expected
  - entity: sensor.ofs_actual
    name: Actual
  - entity: sensor.ofs_pulses
    name: Pulses
```

### Example Glance Card

```yaml
type: glance
title: Filament Sensor
entities:
  - entity: sensor.ofs_printer_connected
    name: Connected
  - entity: sensor.ofs_print_status
    name: Status
  - entity: sensor.ofs_filament_stopped
    name: Jam
  - entity: sensor.ofs_hard_jam_proximity
    name: Hard Jam
  - entity: sensor.ofs_soft_jam_proximity
    name: Soft Jam
  - entity: sensor.ofs_progress
    name: Progress
```

## Available Data Fields

The `/sensor_status` endpoint returns the following data:

| Field | Type | Description |
|-------|------|-------------|
| `stopped` | bool | Filament motion stopped (jam detected) |
| `filamentRunout` | bool | Physical runout sensor triggered |
| `mac` | string | OFS device MAC address |
| `ip` | string | OFS device IP address |
| `elegoo.mainboardID` | string | Printer mainboard identifier |
| `elegoo.isWebsocketConnected` | bool | Connected to printer |
| `elegoo.printStatus` | int | Print state code (see table below) |
| `elegoo.isPrinting` | bool | Actively printing |
| `elegoo.currentLayer` | int | Current layer number |
| `elegoo.totalLayer` | int | Total layers |
| `elegoo.progress` | int | Print progress % |
| `elegoo.currentTicks` | int | Current tick count |
| `elegoo.totalTicks` | int | Total ticks |
| `elegoo.PrintSpeedPct` | int | Print speed percentage |
| `elegoo.currentZ` | float | Current Z height (mm) |
| `elegoo.expectedFilament` | float | Expected extrusion (mm) |
| `elegoo.actualFilament` | float | Measured extrusion (mm) |
| `elegoo.expectedDelta` | float | Last expected delta (mm) |
| `elegoo.telemetryAvailable` | bool | Telemetry data available |
| `elegoo.currentDeficitMm` | float | Current deficit (mm) |
| `elegoo.deficitThresholdMm` | float | Deficit threshold (mm) |
| `elegoo.deficitRatio` | float | Deficit ratio |
| `elegoo.passRatio` | float | Pass ratio |
| `elegoo.ratioThreshold` | float | Ratio threshold setting |
| `elegoo.hardJamPercent` | float | Hard jam proximity (0-100%) |
| `elegoo.softJamPercent` | float | Soft jam proximity (0-100%) |
| `elegoo.movementPulses` | int | Raw pulse count |
| `elegoo.graceActive` | bool | Grace period active |
| `elegoo.graceState` | int | Grace state code |
| `elegoo.expectedRateMmPerSec` | float | Expected flow rate (mm/s) |
| `elegoo.uiRefreshIntervalMs` | int | UI refresh interval (ms) |
| `elegoo.flowTelemetryStaleMs` | int | Telemetry stale timeout (ms) |

### Print Status Codes

| Code | Status |
|------|--------|
| 0 | Idle |
| 1 | Homing |
| 2 | Descending |
| 3 | Exposing |
| 4 | Lifting |
| 5 | Pausing |
| 6 | Paused |
| 7 | Stopping |
| 8 | Stopped |
| 9 | Complete |
| 10 | File Checking |
| 13 | Printing |
| 16 | Heating |
| 20 | Bed Leveling |

### Grace State Codes

| Code | State |
|------|-------|
| 0 | Idle |
| 1 | Start Grace |
| 2 | Resume Grace |
| 3 | Active |
| 4 | Jammed |

## Additional Automation Examples

### Notify at 90% Complete

```yaml
- alias: "OFS: Notify Print Almost Done"
  trigger:
    - platform: numeric_state
      entity_id: sensor.ofs_progress
      above: 90
  action:
    - service: notify.mobile_app_<YOUR_PHONE>
      data:
        title: "Print Almost Done"
        message: "Layer {{ states('sensor.ofs_current_layer') }}/{{ states('sensor.ofs_total_layers') }} ({{ states('sensor.ofs_progress') }}%)"
```

### Notify on Filament Runout

```yaml
- alias: "OFS: Notify on Filament Runout"
  trigger:
    - platform: state
      entity_id: sensor.ofs_filament_runout
      to: "True"
  action:
    - service: notify.mobile_app_<YOUR_PHONE>
      data:
        title: "Filament Runout!"
        message: "The filament spool may be empty. Check the printer."
        data:
          priority: high
```

### Notify on Printer Disconnect

```yaml
- alias: "OFS: Notify on Printer Disconnect"
  trigger:
    - platform: state
      entity_id: sensor.ofs_printer_connected
      to: "False"
      for:
        minutes: 2
  condition:
    - condition: state
      entity_id: sensor.ofs_printing
      state: "True"
  action:
    - service: notify.mobile_app_<YOUR_PHONE>
      data:
        title: "Printer Disconnected!"
        message: "Lost connection to printer while printing."
        data:
          priority: high
```
