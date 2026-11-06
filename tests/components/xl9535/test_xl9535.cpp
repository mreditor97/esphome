#include <gtest/gtest.h>

#include "esphome/components/xl9535/xl9535.h"

namespace esphome::xl9535::testing {

// Models the XL9535 register map: input, output, polarity inversion and configuration,
// each a pair of registers for port 0 and port 1. Every transfer addresses one register.
class FakeBus : public i2c::I2CBus {
 public:
  static constexpr uint8_t ADDRESS = 0x20;

  i2c::ErrorCode write_readv(uint8_t address, const uint8_t *write_buffer, size_t write_count, uint8_t *read_buffer,
                             size_t read_count) override {
    EXPECT_EQ(address, ADDRESS);
    if (this->fail)
      return i2c::ERROR_TIMEOUT;

    EXPECT_GE(write_count, 1u);
    const uint8_t reg = write_buffer[0];
    EXPECT_LT(reg, sizeof(this->registers));

    if (read_count != 0) {
      this->reads++;
      EXPECT_EQ(read_count, 1u);
      read_buffer[0] = this->registers[reg];
      return i2c::ERROR_OK;
    }

    this->writes++;
    EXPECT_EQ(write_count, 2u);
    this->registers[reg] = write_buffer[1];
    return i2c::ERROR_OK;
  }

  // Power-on defaults: outputs high, no inversion, every pin an input.
  uint8_t registers[8]{0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF};

  unsigned reads{0};
  unsigned writes{0};
  bool fail{false};
};

class XL9535Test : public ::testing::Test {
 protected:
  void SetUp() override {
    this->component_.set_i2c_bus(&this->bus_);
    this->component_.set_i2c_address(FakeBus::ADDRESS);
  }

  XL9535GPIOPin make_pin_(uint8_t number, bool inverted = false) {
    XL9535GPIOPin pin;
    pin.set_parent(&this->component_);
    pin.set_pin(number);
    pin.set_flags(gpio::FLAG_OUTPUT);
    pin.set_inverted(inverted);
    return pin;
  }

  FakeBus bus_;
  XL9535Component component_;
};

TEST_F(XL9535Test, SetupFailsWhenTheDeviceIsAbsent) {
  this->bus_.fail = true;
  this->component_.setup();

  EXPECT_TRUE(this->component_.is_failed());
}

// The MCU can reboot while the expander keeps power, so outputs set before the reboot must
// not change until a pin is written.
TEST_F(XL9535Test, SetupKeepsTheOutputLevelsTheChipHolds) {
  this->bus_.registers[XL9535_OUTPUT_PORT_0_REGISTER] = 0x04;
  this->bus_.registers[XL9535_OUTPUT_PORT_1_REGISTER] = 0x80;
  this->component_.setup();
  ASSERT_FALSE(this->component_.is_failed());

  this->make_pin_(1).digital_write(true);

  EXPECT_EQ(this->bus_.registers[XL9535_OUTPUT_PORT_0_REGISTER], 0x06);
  EXPECT_EQ(this->bus_.registers[XL9535_OUTPUT_PORT_1_REGISTER], 0x80);
}

TEST_F(XL9535Test, WritingAPinIsOneWriteAndNoRead) {
  this->component_.setup();
  XL9535GPIOPin pin = this->make_pin_(5);
  const unsigned reads = this->bus_.reads;
  const unsigned writes = this->bus_.writes;

  pin.digital_write(false);
  pin.digital_write(true);

  EXPECT_EQ(this->bus_.reads, reads);
  EXPECT_EQ(this->bus_.writes, writes + 2);
}

// Pins 0-7 are port 0 and pins 10-17 are port 1.
TEST_F(XL9535Test, EachPinMapsToItsOwnPortAndBit) {
  this->bus_.registers[XL9535_OUTPUT_PORT_0_REGISTER] = 0x00;
  this->bus_.registers[XL9535_OUTPUT_PORT_1_REGISTER] = 0x00;
  this->component_.setup();

  for (uint8_t number : {0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17}) {
    XL9535GPIOPin pin = this->make_pin_(number);
    pin.digital_write(true);
    const uint8_t bit = 1 << (number % 10);
    EXPECT_EQ(this->bus_.registers[XL9535_OUTPUT_PORT_0_REGISTER], number < 10 ? bit : 0) << "pin " << int(number);
    EXPECT_EQ(this->bus_.registers[XL9535_OUTPUT_PORT_1_REGISTER], number < 10 ? 0 : bit) << "pin " << int(number);
    pin.digital_write(false);
  }
}

TEST_F(XL9535Test, InvertedPinWritesTheOppositeLevel) {
  this->component_.setup();
  XL9535GPIOPin pin = this->make_pin_(13, true);

  pin.digital_write(true);
  EXPECT_EQ(this->bus_.registers[XL9535_OUTPUT_PORT_1_REGISTER], 0xF7);
  pin.digital_write(false);
  EXPECT_EQ(this->bus_.registers[XL9535_OUTPUT_PORT_1_REGISTER], 0xFF);
}

TEST_F(XL9535Test, PinModeSetsTheConfigBit) {
  this->component_.setup();

  this->make_pin_(3).setup();
  EXPECT_EQ(this->bus_.registers[XL9535_CONFIG_PORT_0_REGISTER], 0xF7);

  this->component_.pin_mode(3, gpio::FLAG_INPUT);
  EXPECT_EQ(this->bus_.registers[XL9535_CONFIG_PORT_0_REGISTER], 0xFF);
}

TEST_F(XL9535Test, DigitalReadUsesTheInputRegister) {
  this->component_.setup();
  this->bus_.registers[XL9535_INPUT_PORT_1_REGISTER] = 0x04;

  EXPECT_TRUE(this->component_.digital_read(12));
  EXPECT_FALSE(this->component_.digital_read(11));
}

}  // namespace esphome::xl9535::testing
