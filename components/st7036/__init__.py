"""ST7036 character LCD component (e.g. EA DOGM163S-A, 3-line x 16-char, over 4-wire SPI).

This is a minimal external component for ESPHome. The ST7036 is not natively
supported by ESPHome, so this implements just enough of the controller's
instruction set (extension/instruction-table-1 commands for bias, booster,
follower and contrast, plus 3-line DDRAM addressing) to drive the display.
"""

import esphome.codegen as cg

CODEOWNERS = ["@your-github-handle"]

st7036_ns = cg.esphome_ns.namespace("st7036")
