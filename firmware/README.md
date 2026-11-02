# PowerBank64 ESP32-S3 firmware

The current firmware is a **read-only monitor** for an ESP32-S3 controller. It
reads one INA226 on the shared battery/load line, displays its measurements on
an I2C OLED, keeps cumulative discharge counters in NVS, and uses three
capacitive touch buttons to navigate the display.

It does not control power outputs, battery selection, USB fast-charge behavior,
fans, or the illuminated output switches. Those functions need confirmed
hardware wiring and protection before firmware control is added.

## Current measurements and screens

- One INA226: bus voltage and shunt voltage; current and power are shown only
  after configuring the actual shunt value.
- Total measured discharge energy (Wh) and discharged capacity (Ah) since
  counter reset; both survive MCU reboot. Counters are persisted about once per
  five minutes, so the latest unsaved interval can be lost if power is removed.
- Four display pages: an icon-led overview with large pack voltage, live
  measurements, framed energy/capacity counters, and sensor/system status.
- At startup the OLED shows a four-second animated DJ splash screen. During
  monitoring, the yellow top band shows the current page title, a compact INA
  status icon, and a battery outline with an unknown marker. Pack series-cell
  count and voltage-to-state-of-charge calibration are not configured, so the
  firmware does not invent a charge percentage. The overview uses separate
  current and power indicators; page dividers and navigation hints stay clear
  of the display's fixed two-color boundary.
- Touch modules with VCC/I/O/GND outputs are read as digital buttons on GPIO
  1/2/4 (active-high by default). To scroll down to the next page, tap button
  4; tap button 1 to go up/back; button 2 is OK/next. The display footer repeats
  these controls. Holding or double-tapping button 1 also goes to the previous
  page; double-tapping OK resets the counters.
- If capacity or a temperature sensor is not configured, runtime and
  temperature are explicitly shown as unavailable rather than estimated.
- Two ADC1 inputs read the NTC divider signals on GPIO 5 and 6 and show their
  averaged raw ADC counts on the System page. Celsius conversion is intentionally
  disabled until the thermistor specifications and divider resistor values are
  confirmed.
- A DHT22 on GPIO 47 reports ambient temperature and relative humidity on the
  System page. The bare four-pin sensor needs a 4.7 kOhm DATA pull-up to 3.3 V
  and a 100 nF VCC-to-GND bypass capacitor; a three-pin module may already have
  the pull-up fitted. See [the wiring diagram](../DHT22_WIRING.svg).

One INA226 can report only the current through its own shunt. For total system
power, place the measured line so all loads of interest pass through that
shunt. It cannot distinguish battery A from B or attribute load to the 12 V,
24 V, or USB branches. The Baseus USB board continues to manage its own ports
and display; this firmware does not read its telemetry.

## Local Wi-Fi JSON log forwarding

The firmware reads `WIFI_RSID` (or `WIFI_SSID`), `WIFI_PASSWORD`, and
`WIFI_LOCAL_ADMIN_SERVER_IP` from the repository-root `.env` during CMake
configuration. Credentials are written only into the ignored build directory;
do not commit `.env` or the generated firmware binary.

Run the local collector on the configured laptop address:

```sh
python3 tools/powerbank64_log_server.py
```

Open `http://<WIFI_LOCAL_ADMIN_SERVER_IP>:8080/logs` for JSON records, or
`/health` to check the collector. The server binds only to that configured
address, accepts logs from devices on the trusted LAN, and retains recent
records in `~/.local/state/powerbank64/logs.jsonl`. Port 8080 is used so the
server can run without root privileges. The HTTP collector has no authentication
or encryption; use it only on a trusted private Wi-Fi network.

The ESP32 forwards its existing ESP-IDF log lines in small HTTP POST batches.
The monitor remains read-only and does not control battery selection or power
outputs. Connect the ESP32 to USB only for this initial test; leave all battery
packs and power modules disconnected.

## Hardware assumptions to verify

- Board target is ESP32-S3; defaults select 8 MB flash for an N8R2-class module.
  PSRAM is not required by this monitor. Confirm the exact board/module marking.
- The OLED is an SSD1306-compatible 128×64 I2C display, confirmed by the user.
  Address `0x3C` is the default (`0x3D` is also configurable); verify the
  address and supply voltage before connecting it.
- Preliminary signal map for the ESP32-S3 N8R2 module:

  | Module signal | ESP32-S3 GPIO | Notes |
  |---|---:|---|
  | OLED SDA + INA226 SDA | 8 | Shared I2C data |
  | OLED SCL + INA226 SCL | 9 | Shared I2C clock |
  | UP / OK / DOWN touch-module outputs | 1 / 2 / 4 | Digital input, active-high by default |
  | NTC divider outputs | 5 / 6 | ADC1 raw readings; temperature not calibrated |
  | Fan driver input (reserved) | 7 | Logic signal only; never power the fan from GPIO |
  | DHT22 data | 47 | 3.3 V logic; bare sensor needs 4.7 kOhm pull-up |

  These are GPIO numbers, not physical header pin positions. Match them to the
  labels on your exact dev board; avoid GPIOs reserved by the board or module.
  Connect module grounds to ESP32 GND. Power the OLED and INA226 logic side from
  3.3 V unless their exact boards explicitly specify otherwise. I2C signals
  must not be pulled up to 5 V.
- The INA226 address defaults to `0x40`. Set the shunt resistance from the
  actual board marking or measurement; leave it at zero until confirmed.
  Current polarity can be inverted in menuconfig if measured discharge current
  is negative.
- Two NTC thermistors may be read through dividers on GPIO 5 and 6. Their exact
  R25/B values and fixed divider resistor values are not known yet, so the
  firmware shows raw ADC counts only and does not report Celsius.
- Connect a bare four-pin DHT22 with its grille facing you and pins downward:
  VCC, DATA, NC, GND from left to right. Use 3.3 V, a 4.7 kOhm resistor from
  DATA to 3.3 V, and 100 nF between VCC and GND near the sensor. Do not connect
  NC. A three-pin module usually labels VCC/DATA/GND; check for its onboard
  pull-up before adding another. No diode is needed.
- Use suitable external I2C pull-ups to 3.3 V if the modules do not already
  provide them. ESP32 GPIOs are 3.3 V only.

### Wiring boundaries for assembly

- OLED and INA226 share SDA/SCL and GND. The INA226's high-current shunt path
  is separate from its I2C logic wiring; do not route pack current through an
  unverified module/shunt. Confirm the INA board's current rating and shunt
  arrangement before connecting either XT60 pack.
- Keep the OLED/INA I2C bus on GPIO 8/9 and the touch buttons on GPIO 1/2/4.
  The NTC ADC signals belong on GPIO 5/6; do not connect NTCs to the I2C wires
  or directly between 3.3 V and a GPIO. For each thermistor, use a fixed
  resistor from 3.3 V to the ADC node and the NTC from that node to GND. Both
  ADC nodes must stay between 0 and 3.3 V. Confirm the thermistor markings and
  fixed resistor values before enabling calibrated temperature conversion.
- The GPIO 7 fan signal must drive a 3.3 V-compatible logic-level MOSFET or
  driver, not the fan itself. Power the 5 V fan from the 5 V regulator; join
  its supply ground with ESP32 GND. Reserve a gate resistor and a gate-to-GND
  pull-down so the fan stays off while the ESP32 boots. The first control
  version should switch the fan fully on/off with temperature hysteresis, not
  PWM, to avoid adding avoidable audible switching noise. Do not use an IRF520
  module unless its driver is explicitly verified to switch fully from a 3.3 V
  GPIO.
- Power VCC/I/O/GND touch modules from 3.3 V and connect I/O to the assigned
  GPIO and GND to ESP32 GND. Never connect a 5 V module output to an ESP32 GPIO.
  The illuminated power-output switches and their LEDs are not connected to
  the ESP32 in this monitor build.

Do not treat firmware monitoring as a substitute for BMS, fuses, thermal
cutoffs, or correctly rated wiring. No power path should depend on this
firmware to remain safe after reset or loss of the ESP32.

The planned MotherBox logic supply is a separate LM2596 buck module adjusted
to 5 V from the protected battery bus. Verify the actual module input range,
output under load, and temperature before connecting it to the DevKit's
documented 5V/VIN input. Do not feed the 3V3 pin with 5 V, and do not connect
the external 5 V rail and computer USB simultaneously until the exact DevKit's
power-path behavior is verified.

## Configure, build, and flash

Requirements: ESP-IDF **v5.4.4** and its ESP32-S3 toolchain.

From `firmware/` in a terminal with ESP-IDF activated:

```sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p PORT flash monitor
```

In **PowerBank64 monitor**, set the verified I2C pins, OLED address, INA226 address,
and measured shunt resistance. If this checkout already has a generated
`sdkconfig`, verify that its flash size is set to 8 MB for an N8R2 module;
`sdkconfig.defaults` only supplies defaults and does not replace existing
settings.

The current environment used to prepare this firmware did not have `idf.py`
available, so the project has not yet been built or tested on hardware.
