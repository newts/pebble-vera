# pebble-vera

A Pebble watchapp (built for Pebble Time 2 / emery) that reads a Z-Wave
thermostat through a Vera controller and changes its heat setpoint. It is a
port of a CircuitPython MagTag gadget that does the same thing.

## Controls

| Button | Action |
| --- | --- |
| Up | Setpoint +1° |
| Down | Setpoint −1° |
| Select | Setpoint = current room temperature + 2° |
| Back | Exit (sends any pending change first) |

- Changes are sent 2 seconds after the last press, so several presses become
  one request. A pending change shows as `Set 71° *`.
- Background is **white** when the heater is idle and **red** when it is heating.
- Refreshes every 10 seconds while open.
- Closes itself after 60 seconds without a button press.

## Setup

1. Build and install: `pebble build && pebble install --phone <phone-ip>`
   (`pebble-clay` is pulled from npm during the build).
2. Open the app's settings in the Pebble phone app and enter your Vera's IP
   address and the thermostat's device number (default 36).

The phone makes the HTTP requests to the Vera (port 3480), so the phone has to
be on the same network as the Vera, or on a VPN into it.

## How it works

- `src/c/main.c` — watch UI, buttons, timers. Sends `Cmd=1` (poll) or
  `Cmd=2` + `NewSetpoint` (set) to the phone.
- `src/pkjs/vera.js` — Luup requests: `variableget` for
  `CurrentTemperature`, heat `CurrentSetpoint` and `ModeState`, and the
  `SetCurrentSetpoint` action on `TemperatureSetpoint1_Heat`.
- `src/pkjs/index.js` — message handling and the Clay settings page. After a
  set, it re-reads the Vera 3 seconds later to confirm.

Temperatures are rounded to whole degrees, same as the original.
