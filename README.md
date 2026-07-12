# Made with Claude.ai


# ESPHome external component: ST7036 (EA DOGM163S-A)

A minimal ESPHome `display` platform for the Sitronix **ST7036** LCD
controller, targeting the **EA DOGM163S-A** (16 characters x 3 lines) over
**4-wire SPI at 3.3V**. ST7036 is not built into ESPHome, so this uses
ESPHome's "external component" mechanism instead of the old `custom_component:`
YAML key (that API was removed from ESPHome core in 2025.2.0).

## Why not just use `lcd_gpio` / `lcd_pcf8574`?

Those built-in components only support classic HD44780-style displays
(1 or 2 lines). The ST7036's 3-line mode needs:
- a different init sequence (bias/booster/follower/contrast commands that
  only exist in the ST7036's "extension" instruction table), and
- different DDRAM row addressing (`0x00/0x10/0x20` instead of HD44780's
  `0x00/0x40`).

Reusing the built-in HD44780 driver would either fail to initialize the
display correctly or scramble line 3, so this component talks to the panel
directly instead.

## 1. Wire the display for 4-wire SPI

| DOGM163S-A pin | Connect to |
|---|---|
| VDD | 3.3V |
| VSS | GND |
| PSB | GND (selects serial mode) |
| E   | 3.3V (must be tied high in serial mode) |
| R/W | GND (unused in serial mode) |
| RS  | ESP GPIO -> `dc_pin` |
| CSB | ESP GPIO -> `cs_pin` |
| DB7 (SI)  | ESP GPIO -> `mosi_pin` |
| DB6 (SCL) | ESP GPIO -> `clk_pin` |
| DB0-DB5 | leave unconnected |

Note: this display's SPI interface is **write-only** (there's no MISO), so
your `spi:` block only needs `clk_pin` and `mosi_pin`.

## 2. Copy the component into your ESPHome config folder

Copy the whole `components/st7036/` folder from this package into an
`components/` folder next to your ESPHome `.yaml` file, so you end up with:

```
<your esphome config dir>/
  your-device.yaml
  components/
    st7036/
      __init__.py
      display.py
      st7036_display.h
      st7036_display.cpp
```

(You can rename the outer folder if you already use `components/` for
something else - just update `external_components.source.path` in your YAML
to match.)

## 3. Add it to your YAML

See `example.yaml` for a full example. The key pieces:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [st7036]

spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23

display:
  - platform: st7036
    cs_pin: GPIO5
    dc_pin: GPIO17
    contrast: 35        # 0-63, tune to taste
    update_interval: 1s
    lambda: |-
      it.print(0, 0, "Hello World!");
      it.print(0, 1, "Line 2");
      it.print(0, 2, "Line 3");
```

`it` in the lambda supports `print(col, row, text)`, `printf(col, row, fmt, ...)`,
and `clear()`, similar to the built-in `lcd_gpio`/`lcd_pcf8574` components.

## 4. Compile and flash

```
esphome run your-device.yaml
```

## Troubleshooting

- **Blank display**: try raising/lowering `contrast` (0-63). Contrast on
  these panels is quite sensitive; also double check PSB is tied to GND and
  E is tied to VDD (both are easy to get backwards).
- **All black blocks**: contrast too high, lower it.
- **Garbled 3rd line / repeats line 1**: this usually means the display
  isn't actually being treated as a 3-line ST7036 panel (e.g. the built-in
  HD44780 driver was used instead) - make sure you're using this `st7036`
  platform, not `lcd_gpio`.
- **Nothing happens at all / no logs about the display**: verify
  `external_components` `path:` actually points at the folder that
  *contains* `st7036/`, not `st7036/` itself.

## Notes / things you may want to tweak

- The SPI timing (mode 3, MSB-first, ~1MHz) and the bias/follower constants
  are set for a 3.3V-powered, 3-line, 4-wire-SPI EA DOGM163S-A. If you're
  running the display at 5V, or wired it up for 4-bit/8-bit parallel
  instead, the init bytes in `st7036_display.cpp` (`CMD_BIAS_SET_3LINE`,
  `CMD_FOLLOWER_CONTROL`, contrast bytes) and this Python config will need
  adjusting - happy to help build that variant too if needed.
- `create_char(location, charmap)` is available in lambdas for defining up
  to 8 custom characters (CGRAM slots 0-7), same idea as the built-in LCD
  components.
