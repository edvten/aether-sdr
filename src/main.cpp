#include <cstdint>
#include <exception>
#include <iostream>
#include <rtl-sdr.h>
#include <stdexcept>
#include <vector>

class SdrDevice {
public:
  SdrDevice(int index = 0) {
    if (rtlsdr_open(&dev, index) != 0) {
      throw std::runtime_error("Failed to open RTL-SDR device.");
    }
    std::cout << "Device opened successfully.\n";
  }

  ~SdrDevice() {
    if (dev) {
      rtlsdr_close(dev);
      std::cout << "Device closed safely.\n";
    }
  }

  // Disabling copy constructors to avoid double-free
  // https://www.geeksforgeeks.org/cpp/explicitly-defaulted-deleted-functions-c-11/
  SdrDevice(const SdrDevice &) = delete;
  SdrDevice &operator=(const SdrDevice &) = delete;

  void configure(int sample_rate, int frequency, int gain_db) {
    rtlsdr_set_sample_rate(dev, sample_rate);
    rtlsdr_set_center_freq(dev, frequency);
    rtlsdr_set_tuner_gain_mode(dev, 1);       // Manual Gain
    rtlsdr_set_tuner_gain(dev, gain_db * 10); // Gain is given by tenths of db

    // Clear buffer
    rtlsdr_reset_buffer(dev);
  }

  void read_sync(std::vector<uint8_t> &buffer) {
    int bytes_read = 0;
    int result =
        rtlsdr_read_sync(dev, buffer.data(), buffer.size(), &bytes_read);

    if (result < 0) {
      throw std::runtime_error("Error reading from device.\n");
    }

    if (bytes_read != static_cast<int>(buffer.size())) {
      std::cerr << "Warning: Short read (" << bytes_read << "bytes)\n";
    }
  }
public:
  // DEFAULT_BUF_LENGTH in rtl_sdr.c source code
  static constexpr int BUF_SIZE = 16 * 16384;

private:
  rtlsdr_dev_t *dev = nullptr;
};

int main() {
  try {
    SdrDevice sdr(0);

    sdr.configure(2048000, 99300000, 35);

    std::vector<uint8_t> buffer(SdrDevice::BUF_SIZE);

    sdr.read_sync(buffer);

    for (const auto &byte : buffer) {
      std::cout << static_cast<int>(byte) << "\n";
    }
  } catch (const std::exception &e) {
    std::cerr << "Fatal Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
