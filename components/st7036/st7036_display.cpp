#include "st7036_display.h"
#include "esphome/core/log.h"

#include <cstdarg>
#include <cstdio>
#include <algorithm>

namespace esphome {
namespace st7036 {

static const char *const TAG = "st7036";

// Bump this string any time you want an unmistakable way to confirm, from
// the boot log, exactly which build of this file ended up on the device.
static const char *const DRIVER_BUILD_MARKER = "2026-08-09-explicit-address-per-char-fix";

// --- ST7036 instruction bytes -------------------------------------------
// Reference: Sitronix ST7036 datasheet, "Extension mode" instruction tables
// 0 and 1 (EXT option pin tied low, which is how the EA DOGM series ships).
//
//  Function Set (table select):        0 0 1 DL N DH IS2 IS1
static const uint8_t CMD_FUNCTION_SET_TABLE0 = 0x38;  // DL=1,N=1,DH=0,IS=00 (normal table)
static const uint8_t CMD_FUNCTION_SET_TABLE1 = 0x39;  // DL=1,N=1,DH=0,IS=01 (extension table 1)
//  Bias Set (table 1):                 0 0 0 1 BS 1 0 FX
//  BS=1 -> 1/4 bias (matches the datasheet's own 1/25 duty / 3-line
//  example), FX=1 is REQUIRED on 3-line displays (DOGM163 has the N3
//  option pin wired to VDD on the glass).
static const uint8_t CMD_BIAS_SET_3LINE = 0x1D;
//  Follower Control (table 1):         0 1 1 0 Fon Rab2 Rab1 Rab0
//  Fon=1 (internal follower on), Rab=4 -- this is the datasheet's own
//  "recommended curve" follower value for VDD around 3.0-3.3V with the
//  internal booster+follower enabled.
static const uint8_t CMD_FOLLOWER_CONTROL = 0x6C;
//  Display ON/OFF:                     0 0 0 0 1 D C B
static const uint8_t CMD_DISPLAY_ON = 0x0C;  // display on, cursor off, blink off
//  Clear display / entry mode (table 0, same as HD44780):
static const uint8_t CMD_CLEAR_DISPLAY = 0x01;
static const uint8_t CMD_ENTRY_MODE_SET = 0x06;  // I/D=1 (increment), S=0 (no shift)
//  DDRAM / CGRAM address set (table 0, same as HD44780):
static const uint8_t CMD_SET_DDRAM_ADDR = 0x80;
static const uint8_t CMD_SET_CGRAM_ADDR = 0x40;

// Minimum wait after most ST7036 instructions/data writes before the next
// one is accepted (datasheet: ~26.3us). Applied after every SPI byte, not
// just during setup(), since data writes during normal print() need it too.
static const uint32_t COMMAND_SETTLE_MICROS = 30;

void ST7036Display::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ST7036 LCD...");

  this->spi_setup();
  if (this->dc_pin_ != nullptr) {
    this->dc_pin_->setup();
  }

  this->buffer_.resize(static_cast<size_t>(this->columns_) * this->rows_, ' ');

  // Power/ICON/Contrast control (high byte, table 1):
  //   0 1 0 1 Ion Bon C5 C4
  // Ion=0 (icon off, unused), Bon=1 (booster on -- required for 3.3V/5V
  // single-supply operation), C5/C4 = top 2 bits of the 6-bit contrast.
  const uint8_t power_icon_contrast_high = 0x54 | ((this->contrast_ >> 4) & 0x03);
  // Contrast Set (low byte, table 1):   0 1 1 1 C3 C2 C1 C0
  const uint8_t contrast_low = 0x70 | (this->contrast_ & 0x0F);

  delay(50);  // internal reset needs >40ms after VDD is stable

  this->command_(CMD_FUNCTION_SET_TABLE0);
  this->command_(CMD_FUNCTION_SET_TABLE1);
  this->command_(CMD_BIAS_SET_3LINE);
  this->command_(power_icon_contrast_high);
  this->command_(contrast_low);
  this->command_(CMD_FOLLOWER_CONTROL);
  delay(200);  // datasheet: wait >200ms here for the booster/follower to stabilize
  this->command_(CMD_FUNCTION_SET_TABLE0);  // switch back to the normal instruction table
  this->command_(CMD_DISPLAY_ON);
  this->command_(CMD_CLEAR_DISPLAY);
  delay(2);
  this->command_(CMD_ENTRY_MODE_SET);
}

void ST7036Display::update() {
  if (this->writer_ != nullptr) {
    this->writer_(*this);
  }
}

void ST7036Display::dump_config() {
  ESP_LOGCONFIG(TAG, "ST7036 LCD:");
  ESP_LOGCONFIG(TAG, "  Driver build: %s", DRIVER_BUILD_MARKER);
  ESP_LOGCONFIG(TAG, "  Dimensions: %ux%u", this->columns_, this->rows_);
  ESP_LOGCONFIG(TAG, "  Contrast: %u", this->contrast_);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  CS Pin: ", this->cs_);
}

// Each byte gets its own CS (enable/disable) pulse -- see the note in the
// header on why this matters for the ST7036's serial shift register -- plus
// a settle delay, since the controller needs time to process each write
// before the next one arrives.
void ST7036Display::command_(uint8_t value) {
  if (this->dc_pin_ != nullptr)
    this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(value);
  this->disable();
  delayMicroseconds(COMMAND_SETTLE_MICROS);
}

void ST7036Display::write_data_(uint8_t value) {
  if (this->dc_pin_ != nullptr)
    this->dc_pin_->digital_write(true);
  this->enable();
  this->write_byte(value);
  this->disable();
  delayMicroseconds(COMMAND_SETTLE_MICROS);
}

void ST7036Display::set_cursor_(uint8_t column, uint8_t row) {
  // 3-line DDRAM layout (from the ST7036 datasheet): row 0 = 0x00-0x0F,
  // row 1 = 0x10-0x1F, row 2 = 0x20-0x2F. (This differs from the classic
  // HD44780 2-line layout of 0x00/0x40 -- do not reuse that mapping here.)
  static const uint8_t ROW_OFFSETS[3] = {0x00, 0x10, 0x20};
  uint8_t row_off = ROW_OFFSETS[row < 3 ? row : 2];
  this->command_(CMD_SET_DDRAM_ADDR | (row_off + column));
}

void ST7036Display::print(uint8_t column, uint8_t row, const char *str) {
  if (row >= this->rows_ || column >= this->columns_)
    return;

  // Space left in this row, starting from `column`. The loop below always
  // fills exactly this many cells: characters from `str` first, then spaces
  // for whatever remains. That means every print() call fully overwrites
  // the row instead of leaving old, longer content behind, and a string
  // that's too long is simply clipped at the row edge rather than spilling
  // into the next line's DDRAM address.
  //
  // NOTE: we deliberately do NOT rely on the controller's DDRAM address
  // auto-increment here. The ST7036 datasheet states that CSB falling
  // resets "the shift register and the counter" in serial mode, and since
  // command_()/write_data_() pulse CS around every single byte (required
  // for reliable byte framing on this display), that may reset the DDRAM
  // address counter too, not just the serial bit counter. So each
  // character gets its own explicit "set DDRAM address" command instead of
  // trusting auto-increment to have survived the CS pulse from the
  // previous byte.
  const uint8_t available = this->columns_ - column;

  for (uint8_t written = 0; written < available; written++) {
    char c = (*str != '\0') ? *str++ : ' ';
    this->set_cursor_(column + written, row);
    this->write_data_(static_cast<uint8_t>(c));
    size_t pos = static_cast<size_t>(row) * this->columns_ + column + written;
    if (pos < this->buffer_.size())
      this->buffer_[pos] = static_cast<uint8_t>(c);
  }
}

void ST7036Display::printf(uint8_t column, uint8_t row, const char *format, ...) {
  char buffer[64];
  va_list arg;
  va_start(arg, format);
  int ret = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);
  if (ret > 0)
    this->print(column, row, buffer);
}

void ST7036Display::printf(const char *format, ...) {
  char buffer[64];
  va_list arg;
  va_start(arg, format);
  int ret = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);
  if (ret > 0)
    this->print(0, 0, buffer);
}

void ST7036Display::clear() {
  this->command_(CMD_CLEAR_DISPLAY);
  delay(2);
  std::fill(this->buffer_.begin(), this->buffer_.end(), ' ');
}

void ST7036Display::create_char(uint8_t location, const uint8_t charmap[8]) {
  location &= 0x07;
  this->command_(CMD_SET_CGRAM_ADDR | (location << 3));
  for (uint8_t i = 0; i < 8; i++) {
    this->write_data_(charmap[i]);
  }
}

}  // namespace st7036
}  // namespace esphome
