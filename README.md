# Aether SDR

A simple FM radio receiver for the RTL-SDR dongle, written in C++17.

This is a hobby project I created to learn more about C++, multithreading, and Digital Signal Processing (DSP). It implements a basic demodulator directly on top of the `librtlsdr` driver, without relying on large frameworks like GNU Radio.

## Architecture

The application uses a **Producer-Consumer** architecture to separate the hardware reading from the signal processing:

* **Producer Thread:** Reads raw IQ samples from the RTL-SDR dongle via USB.
* **SPSC Queue:** A custom, lock-free Single-Producer Single-Consumer FIFO queue passes data between threads without mutex locking.
* **Consumer Thread:** Performs demodulation, filtering, and de-emphasis, then writes raw audio samples to `stdout`.

## Dependencies

* **librtlsdr** (Driver for the USB dongle)
* **aplay** (or any raw audio player for playback)
* **C++17 compliant compiler** (GCC/Clang)

## Building

```bash
make
```

## Running

The program outputs raw audio data (16-bit signed, Little Endian, Mono) to standard output. 

The target audio rate is 48 KHz.

To play the audio you must pipe it to an audio player:

```bash
# Sample rate set to 1.92 MHz, frequency 95.7 MHz, gain 40 dB
# Defaults are 1.92 MHz, 98.4 MHz and 35 dB
./aether-sdr -s 1.92 -f 95.7 -g 40 | aplay -r 48000 -f S16_LE -t raw -c 1
```

## License
MIT
