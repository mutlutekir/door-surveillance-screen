import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@kapi-gozetim"]
DEPENDENCIES = ["lvgl", "psram"]

mjpeg_stream_ns = cg.esphome_ns.namespace("mjpeg_stream")
MjpegStream = mjpeg_stream_ns.class_("MjpegStream", cg.Component)

CONF_URL = "url"
CONF_WIDTH = "width"
CONF_HEIGHT = "height"
CONF_STACK_SIZE = "stack_size"
CONF_JPEG_BUFFER = "jpeg_buffer_size"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MjpegStream),
        cv.Required(CONF_URL): cv.string_strict,
        cv.Required(CONF_WIDTH): cv.int_range(min=16, max=800),
        cv.Required(CONF_HEIGHT): cv.int_range(min=16, max=800),
        cv.Optional(CONF_STACK_SIZE, default=16384): cv.int_range(min=4096, max=32768),
        cv.Optional(CONF_JPEG_BUFFER, default=131072): cv.int_range(
            min=16384, max=262144
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_url(config[CONF_URL]))
    cg.add(var.set_size(config[CONF_WIDTH], config[CONF_HEIGHT]))
    cg.add(var.set_stack_size(config[CONF_STACK_SIZE]))
    cg.add(var.set_jpeg_buffer_size(config[CONF_JPEG_BUFFER]))
