# Aether SDR

A simple FM radio receiver for the RTL-SDR dongle, written in C++17.

This is a hobby project I created to learn more about C++, multithreading, and Digital Signal Processing (DSP). It implements a basic demodulator directly on top of the `librtlsdr` driver, without relying on large frameworks like GNU Radio.

## Architecture

The application uses a **Forked Producer-Consumer** architecture to separate the hardware reading from the signal processing:

* **Producer Thread:** Reads raw IQ samples from the RTL-SDR dongle via USB. It pushes data to two separate queues:
    * **Audio Queue (Critical):** Blocking. If full, the producer waits to ensure no audio samples are lost.
    * **GUI Queue (Lossy):** Non-blocking. If full, packets are dropped to ensure the visualization never stalls the audio.
* **Audio Callback (Consumer):** Managed by `miniaudio`. It wakes up periodically to demodulate data and fill the system audio buffer in real-time.
* **Visualizer:** Uses **Raylib** to render the raw signal data.

## Dependencies

* **librtlsdr** (Driver for the USB dongle)
* **libraylib-dev** (Graphics library for the visualizer)
* **miniaudio** (Included as a single-header library)
* **C++17 compliant compiler** (GCC/Clang)

## Building

```bash
make
```

## Running

The program opens a GUI window displaying the raw signal and outputs audio to the default device.

```bash
# Sample rate set to 1.92 MHz, frequency 95.7 MHz, gain 40 dB
./aether-sdr -s 1.92 -f 95.7 -g 40
# Defaults are 1.92 MHz, 98.4 MHz and 35 dB
./aether-sdr
```

## License
MIT
