#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

class SPSCQueue {
public:
  SPSCQueue(std::size_t size)
      : buffer(size), buf_size(size), mask(size - 1), head(0), tail(0) {
    if ((size & (size - 1)) != 0) {
      throw std::invalid_argument("SPSCQueue size has to be a power of 2");
    }
  }

  bool push(const std::vector<uint8_t> &data) {
    const size_t data_size = data.size();

    auto curr_head = head.load(std::memory_order_relaxed);
    auto curr_tail = tail.load(std::memory_order_acquire);

    if (data_size + curr_head - curr_tail > buf_size) {
      // Queue would become full
      return false;
    }

    size_t write_index = curr_head & mask;
    size_t first_chunk = std::min(data_size, buf_size - write_index);

    // Store data
    std::memcpy(buffer.data() + write_index, data.data(), first_chunk);

    // Check if we need to wrap around
    if (first_chunk < data_size) {
      std::memcpy(buffer.data(), data.data() + first_chunk,
                  data_size - first_chunk);
    }

    // Update head pointer
    head.store(curr_head + data_size, std::memory_order_release);
    return true;
  }

  // Reads at most max_size bytes from queue
  bool pop(std::vector<uint8_t> &dest, size_t max_size) {
    auto curr_head = head.load(std::memory_order_acquire);
    auto curr_tail = tail.load(std::memory_order_relaxed);

    if (curr_head == curr_tail) {
      // Queue is empty
      return false;
    }

    size_t read_size = std::min(max_size, curr_head - curr_tail);

    // Prevent dest containing garbage data
    if (dest.size() != read_size) {
      dest.resize(read_size);
    }

    size_t read_index = curr_tail & mask;
    size_t first_chunk = std::min(read_size, buf_size - read_index);

    // Get data
    std::memcpy(dest.data(), buffer.data() + read_index, first_chunk);

    if (first_chunk < read_size) {
      std::memcpy(dest.data() + first_chunk, buffer.data(),
                  read_size - first_chunk);
    }

    // Update tail pointer
    tail.store(curr_tail + read_size, std::memory_order_release);
    return true;
  }

private:
  std::vector<uint8_t> buffer;
  std::size_t buf_size;
  size_t mask;

  // Align as 64 to avoid false sharing on cache lines
  alignas(64) std::atomic<std::size_t> head;
  alignas(64) std::atomic<std::size_t> tail;
};
