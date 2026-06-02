# ESP32 Wireless pH / ORP Monitor

A small ESP32 firmware project for wireless liquid monitoring. The device reads a pH probe amplifier and an ORP probe amplifier through ESP32 ADC inputs, serves a local web dashboard, and supports first-boot Wi-Fi configuration through a temporary Access Point.

This is a clean public demo/reconstruction of a real embedded monitoring workflow: sensor acquisition, analog calibration, Wi-Fi onboarding, local dashboard, and JSON API.

## Features

- ESP32 Wi-Fi setup Access Point on first boot
- Browser-based setup page for Wi-Fi credentials
- Live pH and ORP dashboard
- Browser-based pH two-point calibration using known buffer liquids
- Browser-based ORP one-point offset calibration using a known ORP solution
- JSON API endpoint for integration with other systems
- ADC averaging for more stable readings
- Calibration parameters stored in ESP32 NVS flash
- Reset Wi-Fi configuration from dashboard
- mDNS support: `http://ph-orp-monitor.local`
- No cloud dependency

## Hardware concept

```text
pH probe -> pH amplifier board -> ESP32 ADC pin GPIO34
ORP probe -> ORP amplifier board -> ESP32 ADC pin GPIO35
ESP32 -> Wi-Fi -> phone/laptop browser dashboard
```

## Example wiring

| Signal | ESP32 pin | Notes |
|---|---:|---|
| pH amplifier analog output | GPIO34 | ADC input only |
| ORP amplifier analog output | GPIO35 | ADC input only |
| GND | GND | Common ground with amplifier boards |
| Amplifier VCC | 3.3V or 5V | Depends on the amplifier module |

> Important: make sure the amplifier analog output never exceeds the safe ESP32 ADC input range. Use a voltage divider or level shifting if your amplifier outputs 5V.

## First boot setup

1. Flash the firmware.
2. On first boot, the ESP32 creates an Access Point:

```text
ESP32-PH-ORP-SETUP
```

3. Connect with a phone/laptop.
4. Open:

```text
http://192.168.4.1
```

5. Enter Wi-Fi SSID/password.
6. Save. The ESP32 reboots and joins your Wi-Fi.
7. Open the dashboard using the serial monitor IP address or:

```text
http://ph-orp-monitor.local
```

## pH calibration workflow

pH probes and amplifier boards must be calibrated against known pH buffer liquids. This firmware uses a two-point calibration model:

```text
pH = slope * voltage + offset
```

The slope and offset are calculated automatically from two known liquids.

Typical calibration pairs:

```text
pH 7.00 and pH 4.00
pH 7.00 and pH 10.00
```

Recommended process:

1. Open the dashboard.
2. Go to `Calibration`.
3. Put the pH probe in the first known buffer, for example pH 7.00.
4. Wait until the ADC reading is stable.
5. Enter `7.00` and press `Capture pH Point 1`.
6. Rinse the probe.
7. Put the probe in the second known buffer, for example pH 4.00.
8. Wait until the ADC reading is stable.
9. Enter `4.00` and press `Capture pH Point 2 + Calculate`.
10. The firmware stores both measured ADC voltages and calculates the calibration slope/offset.

The calibration values are stored in ESP32 NVS flash and survive reboot.

## ORP calibration workflow

ORP is implemented as a simple one-point offset calibration:

```text
ORP_mV = raw_adc_mV + offset
```

Recommended process:

1. Put the ORP probe in a known ORP calibration solution.
2. Enter the known mV value.
3. Press `Capture ORP Calibration`.
4. The firmware calculates and stores the offset.

## API endpoints

### Live readings

```http
GET /api/readings
```

Example response:

```json
{
  "ph": 7.04,
  "ph_voltage": 2.5142,
  "ph_mv": 2514.2,
  "orp_mv": 384.5,
  "orp_adc_mv": 384.5,
  "samples": 30,
  "ph_slope": -5.7,
  "ph_offset": 21.34,
  "mode": "STA",
  "ip": "192.168.1.50"
}
```

### Device status

```http
GET /api/status
```

## Building with PlatformIO

```bash
pio run
pio upload
pio device monitor
```

## Files

```text
platformio.ini
src/main.cpp
README.md
```
