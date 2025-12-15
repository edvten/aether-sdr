#include "SPSCQueue.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <ios>
#include <iostream>
#include <rtl-sdr.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

// Global flag to stop execution of threads
std::atomic<bool> running(true);

static constexpr int TARGET_AUDIO_RATE = 48000;

class SdrDevice {
public:
  SdrDevice(int index = 0) {
    if (rtlsdr_open(&dev, index) != 0) {
      throw std::runtime_error("Failed to open RTL-SDR device.");
    }
    std::cerr << "Device opened successfully.\n";
  }

  ~SdrDevice() {
    if (dev) {
      rtlsdr_close(dev);
      std::cerr << "Device closed safely.\n";
    }
  }

  // Disabling copy constructors to avoid double-free
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

class AudioProcessor {
public:
  AudioProcessor(int decimation_rate)
      : decimation_counter(0), decimation_sum(0.0f),
        decimation_rate(decimation_rate),
        prev_sample(std::complex<float>(1.0f, 0.0f)),
        previous_filtered_sample(0.0f) {

    // Calculations of alpha based on:
    // https://en.wikipedia.org/wiki/Low-pass_filter#Discrete-time_realization
    // Which links to:
    // https://en.wikipedia.org/wiki/Exponential_smoothing#Time_constant
    // Giving us the formula used below.
    // 50 micro seconds is the default time-constant in Europe:
    // https://www.fmradiobroadcast.com/article/detail/fm-emphasis.html
    const float time_constant = 50e-6f;
    float dt = 1.0f / TARGET_AUDIO_RATE;
    alpha = 1.0f - std::exp(-dt / time_constant);
  }

  std::vector<int16_t> process(const std::vector<uint8_t> &raw_iq) {
    // IQ sampling gives us the factor 2.
    // We accumulate decimation_rate samples and filter them to become 1
    // Hence our output buffer is smaller than the input buffer by a factor of
    // (2 * decimation_rate)
    std::vector<int16_t> output_buffer;
    output_buffer.reserve(raw_iq.size() / (2 * decimation_rate));

    // Note i += 2 since we jump from I sample to I sample
    for (size_t i = 0; i < raw_iq.size(); i += 2) {
      // Convert uint8_t sample to float:
      // https://k3xec.com/packrat-processing-iq/
      float real = ((float)raw_iq[i] - 127.5f) / 127.5f;
      float imag = ((float)raw_iq[i + 1] - 127.5f) / 127.5f;
      std::complex<float> current_sample = std::complex<float>(real, imag);

      // We only care about the change in phase from the previous sample.
      // Hence, we can perform complex multiplication with the complex conjugate
      // of the previous sample to create a new complex number who's phase is
      // the difference between the current sample and the previous sample:
      // r1 * e^(i*p1) * conj(r2 * e^(i*p2)) = r1 * e^(i*p1) * r2 * e^(-i*p2) =
      // = r1 * r2 * e^(i(p1 - p2)). arctan is then used to extract this phase.
      std::complex<float> delta_sample =
          current_sample * std::conj(prev_sample);
      float delta_phase = std::atan2(delta_sample.imag(), delta_sample.real());

      // Remember the current sample
      prev_sample = current_sample;

      // Moving average filter update
      decimation_sum += delta_phase;
      decimation_counter++;

      // If we have collected decimation_rate samples
      if (decimation_counter == decimation_rate) {
        // Calculate the average value
        float audio_sample = decimation_sum / (float)decimation_rate;

        // Reset decimation counters and sum
        decimation_counter = 0;
        decimation_sum = 0.0f;

        // de-emphasis like in below:
        // rtl_fm.c: void deemph_filter(struct demod_state *fm)
        float filtered_sample = (alpha * audio_sample) +
                                ((1.0f - alpha) * previous_filtered_sample);
        previous_filtered_sample = filtered_sample;

        // Amplify the filtered audio sample
        float amplified_sample = filtered_sample * 16000.0f;
        // Clamp values to prevent integer overflow when casting to int16
        amplified_sample = std::clamp(amplified_sample, -32768.0f, 32767.0f);

        // Append to our output buffer
        output_buffer.push_back(static_cast<int16_t>(amplified_sample));
      }
    }

    return output_buffer;
  }

private:
  // Decimation moving average variables
  int decimation_counter;
  float decimation_sum;
  int decimation_rate;
  // Previous IQ sample
  std::complex<float> prev_sample;
  // Previous De-emphasised sample
  float previous_filtered_sample;
  // constant for de-emphasis in europe
  float alpha;
};

void producer_thread(SdrDevice &sdr, SPSCQueue &queue) {
  std::vector<uint8_t> buffer(SdrDevice::BUF_SIZE);

  while (running) {
    sdr.read_sync(buffer);

    while (running && !queue.push(buffer)) {
      // Naive busy wait
      // Sleep to make it less naive
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }
}
void consumer_thread(AudioProcessor &AP, SPSCQueue &queue) {
  std::vector<uint8_t> buffer(SdrDevice::BUF_SIZE);
  buffer.reserve(SdrDevice::BUF_SIZE);

  while (running) {
    if (queue.pop(buffer, SdrDevice::BUF_SIZE)) {
      std::vector<int16_t> audio = AP.process(buffer);

      // Write to stdout
      std::cout.write(reinterpret_cast<const char *>(audio.data()),
                      audio.size() * sizeof(int16_t));

    } else {
      // Queue empty
      // Naive sleep to save CPU
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}

void print_help() {
  std::cerr << "Usage: aether-sdr [OPTIONS]\n"
            << "\n"
            << "Options:\n"
            << "  -h Show this help message\n"
            << "  -s <sample rate (MHz)> Set the sample rate\n"
            << "  -f <frequency (MHz)> Set the frequency\n"
            << "  -g <gain(dB)> Set the tuner gain\n";
}

int main(int argc, char *argv[]) {
  // Don't sync with C output streams
  std::ios_base::sync_with_stdio(false);
  // Do not automatically flush output buffer when accessing stdin
  std::cin.tie(NULL);

  // Defaults
  int sample_rate = 1920000; // 1.92 MHz
  int frequency = 98400000;  // 98.4 MHz
  int gain_db = 35;          // 35 db

  int opt;
  while ((opt = getopt(argc, argv, "hs:f:g:")) != -1) {
    switch (opt) {
    case 'h':
      print_help();
      return 0;
      break;
    case 's':
      // We expect sample rate in MHz
      // Add 0.5f to fix truncation
      sample_rate = static_cast<int>(std::round(std::stof(optarg) * 1e6));
      std::cerr << "Set sample rate to: " << sample_rate << " Hz\n";
      break;
    case 'f':
      // Expect frequency in MHz
      // Add 0.5f to fix truncation
      frequency = static_cast<int>(std::round(std::stof(optarg) * 1e6));
      std::cerr << "Set frequency to: " << frequency << " Hz\n";
      break;
    case 'g':
      // We expect integer gains
      gain_db = std::stoi(optarg);
      std::cerr << "Set gain to: " << gain_db << " dB\n";
      break;
    default:
      print_help();
      return 1;
    }
  }

  int decimation_rate = sample_rate / TARGET_AUDIO_RATE;

  if (decimation_rate < 1)
    decimation_rate = 1;
  if (sample_rate % TARGET_AUDIO_RATE != 0) {
    std::cerr << "Warning: Sample rate " << sample_rate
              << " Hz is not a multiple of " << TARGET_AUDIO_RATE << " Hz. \n"
              << "Audio will technically play at "
              << (sample_rate / decimation_rate)
              << " Hz but calculations are based on " << TARGET_AUDIO_RATE
              << " Hz\n";
  }

  try {
    SdrDevice sdr(0);
    sdr.configure(sample_rate, frequency, gain_db);

    AudioProcessor AP(decimation_rate);

    SPSCQueue queue(1 << 20);

    std::cerr << "Starting producer and consumer threads... \n"
              << "Press enter to stop.\n";

    std::thread prod(producer_thread, std::ref(sdr), std::ref(queue));
    std::thread cons(consumer_thread, std::ref(AP), std::ref(queue));

    // Wait for user input
    std::cin.get();

    running = false;

    prod.join();
    cons.join();
  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
