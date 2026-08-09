#pragma once

#include <functional>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace st7036 {

// EA DOGM163S-A / ST7036 4-wire SPI timing:
//  - SPI Mode 3 (clock idles high, data sampled on trailing edge)
//  - MSB first
//  - CSB must pulse low->high around EACH byte (this resets the
//    controller's internal serial shift register/bit counter), which is
//    why command_()/write_data_() below call enable()/disable() per byte
//    instead of wrapping a whole multi-byte transaction.
class ST7036Display : public PollingComponent,
                       public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_HIGH,
                                              spi::CLOCK_PHASE_TRAILING, spi::DATA_RATE_1MHZ> {
 public:
  void set_dc_pin(GPIOPin *dc_pin) { this->dc_pin_ = dc_pin; }
  void set_dimensions(uint8_t columns, uint8_t rows) {
    this->columns_ = columns;
    this->rows_ = rows;
  }
  // 6-bit contrast value (0-63).
  void set_contrast(uint8_t contrast) { this->contrast_ = contrast & 0x3F; }
  void set_writer(std::function<void(ST7036Display &)> &&writer) { this->writer_ = std::move(writer); }

  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  /// Print text at the given column/row (0-indexed). Always writes the full
  /// row: the string is truncated if it's too long, and padded with spaces
  /// if it's shorter than the row, so old content never lingers on screen.
  void print(uint8_t column, uint8_t row, const char *str);
  void print(uint8_t column, uint8_t row, const std::string &str) { this->print(column, row, str.c_str()); }
  void print(const char *str) { this->print(0, 0, str); }
  void print(const std::string &str) { this->print(0, 0, str.c_str()); }

  void printf(uint8_t column, uint8_t row, const char *format, ...) __attribute__((format(printf, 4, 5)));
  void printf(const char *format, ...) __attribute__((format(printf, 2, 3)));

  /// Clear the display and reset the on-chip cursor to the top-left.
  void clear();

  /// Define one of the 8 custom characters (CGRAM slots 0-7). charmap must
  /// point to 8 bytes, each with the lower 5 bits used as one character row.
  void create_char(uint8_t location, const uint8_t charmap[8]);

 protected:
  void command_(uint8_t value);
  void write_data_(uint8_t value);
  void set_cursor_(uint8_t column, uint8_t row);

  GPIOPin *dc_pin_{nullptr};
  uint8_t columns_{16};
  uint8_t rows_{3};
  uint8_t contrast_{35};
  std::vector<uint8_t> buffer_;
  std::function<void(ST7036Display &)> writer_{nullptr};
};

}  // namespace st7036
}  // namespace esphome
