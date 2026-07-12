import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import spi
from esphome.const import CONF_ID, CONF_LAMBDA

from . import st7036_ns

DEPENDENCIES = ["spi"]

CONF_DC_PIN = "dc_pin"
CONF_CONTRAST = "contrast"

ST7036Display = st7036_ns.class_("ST7036Display", cg.PollingComponent, spi.SPIDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ST7036Display),
            cv.Required(CONF_DC_PIN): pins.gpio_output_pin_schema,
            # ST7036 contrast is a 6-bit value (0-63). If the display looks
            # blank or all-blocks after flashing, tune this up/down.
            cv.Optional(CONF_CONTRAST, default=35): cv.int_range(min=0, max=63),
            cv.Optional(CONF_LAMBDA): cv.lambda_,
        }
    )
    .extend(cv.polling_component_schema("1s"))
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    dc_pin = await cg.gpio_pin_expression(config[CONF_DC_PIN])
    cg.add(var.set_dc_pin(dc_pin))
    cg.add(var.set_contrast(config[CONF_CONTRAST]))
    # EA DOGM163S-A is fixed at 16 columns x 3 rows.
    cg.add(var.set_dimensions(16, 3))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA],
            [(ST7036Display.operator("ref"), "it")],
            return_type=cg.void,
        )
        cg.add(var.set_writer(lambda_))
