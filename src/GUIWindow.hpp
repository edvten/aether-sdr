#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <raylib.h>

// Remove warnings caused by raygui.h
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#define RAYGUI_IMPLEMENTATION
#include "../include/raygui.h"

#pragma GCC diagnostic pop
#include "../include/miniaudio.h"
#include <string>
#include <vector>

class GUIWindow {
private:
  int sample_rate;
  int center_freq;

public:
  GUIWindow(int width, int height, const std::string &title, int s_rate,
            int c_freq)
      : sample_rate(s_rate), center_freq(c_freq) {
    InitWindow(width, height, title.c_str());
    SetTargetFPS(60);
  }

  ~GUIWindow() { CloseWindow(); }

  GUIWindow(const GUIWindow &) = delete;
  GUIWindow &operator=(const GUIWindow &) = delete;

  bool should_close() { return WindowShouldClose(); }

  void draw(std::vector<uint8_t> &rawIQ_buffer, std::vector<float> &magnitudes,
            std::size_t bytes_read, float *volume_level) {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    // Top UI Start
    int ui_y = 10;
    int ui_height = 20;
    int ui_y_end = ui_height + 2 * ui_y;

    const char *title = "AETHER SDR";
    int title_x = 10;
    DrawText(title, title_x, ui_y, ui_height, DARKGRAY);

    int title_width = MeasureText(title, ui_height);
    DrawFPS(title_x + title_width + 15, ui_y);

    int screen_width = GetScreenWidth();
    int slider_width = 120;
    int slider_x = screen_width - slider_width - 50;

    GuiSliderBar((Rectangle){(float)slider_x, (float)ui_y, (float)slider_width,
                             (float)ui_height},
                 "Volume", TextFormat("%.2f", *volume_level), volume_level, 0,
                 1);

    DrawLine(0, ui_y_end, screen_width, ui_y_end, BLACK);
    // Top UI End

    float screen_widthf = static_cast<float>(GetScreenWidth());
    float screen_heightf = static_cast<float>(GetScreenHeight());

    float graph_y_real_estate = screen_heightf - ui_y_end;
    float graph_y_real_estate_middle = graph_y_real_estate / 2.0f + ui_y_end;

    float rawIQ_bottom_y = graph_y_real_estate_middle;
    float rawIQ_top_y = ui_y_end;

    float fft_bottom_y = screen_heightf;
    float fft_top_y = graph_y_real_estate_middle;

    draw_rawIQ(rawIQ_buffer, bytes_read, volume_level, screen_widthf,
               rawIQ_bottom_y, rawIQ_top_y);
    draw_FFT(magnitudes, screen_widthf, fft_bottom_y, fft_top_y);

    // Graph labels
    int padding_x = 10;
    int padding_y = 10;
    int font_size = 20;

    DrawText("Raw IQ Samples", padding_x,
             static_cast<int>(rawIQ_top_y) + padding_y, font_size, DARKGREEN);

    DrawText("FFT Magnitude (dB)", padding_x,
             static_cast<int>(fft_top_y) + padding_y, font_size, DARKBLUE);

    EndDrawing();
  }

  void draw_FFT(const std::vector<float> &magnitudes, float screen_width,
                float bottom_y, float top_y) {
    size_t fft_n = magnitudes.size();

    float graph_height = bottom_y - top_y;

    float min_db = -40.0f;
    float max_db = 60.0f;

    // Vertical Grid
    int freq_step = 500000; // TODO: make this dynamic
    int start_freq = center_freq - (sample_rate / 2);
    int end_freq = center_freq + (sample_rate / 2);

    // Ceiling integer division:
    // (https://stackoverflow.com/questions/2745074/fast-ceiling-of-an-integer-division-in-c-c)
    // Then multiply with freq_step to get corresponding frequency
    int grid_start = ((start_freq + freq_step - 1) / freq_step) * freq_step;

    for (int f = grid_start; f <= end_freq; f += freq_step) {
      float freq_offset = static_cast<float>(f - center_freq);
      // Fraction of screen width. (freq_offset / sample_rate) produces value in
      // [-0.5, 0.5], adding 0.5 gives us [0.0, 1.0]
      float frac = (freq_offset / sample_rate) + 0.5f;
      // Multiply fraction of screen witdthwith with screen width to get
      // x-coordinate
      int x = static_cast<int>(frac * screen_width);

      DrawLine(x, top_y, x, bottom_y, LIGHTGRAY);

      const char *label = TextFormat("%.2f", f / 1e6f);
      int text_width = MeasureText(label, 10);
      int text_x = std::clamp(x - (text_width / 2), 5,
                              static_cast<int>(screen_width) - text_width - 5);

      DrawText(label, text_x, bottom_y - 25, 10, DARKGRAY);
    }

    // Horizontal Grid
    for (int db = static_cast<int>(min_db); db <= static_cast<int>(max_db);
         db += 20) {
      float normalized_db = (db - min_db) / (max_db - min_db);
      int y = static_cast<int>(bottom_y - (normalized_db * graph_height));

      DrawLine(0, y, screen_width, y, LIGHTGRAY);
      DrawText(TextFormat("%d dB", db), 5, y - 15, 10, DARKGRAY);
    }

    // Center Marker
    int center_x = static_cast<int>(screen_width / 2);
    DrawLine(center_x, top_y, center_x, bottom_y, RED);
    const char *center_label = TextFormat("CF: %.3f MHz", center_freq / 1e6f);
    int center_text_width = MeasureText(center_label, 10);
    DrawText(center_label, center_x - (center_text_width / 2), bottom_y - 35,
             10, MAROON);

    // Magnitude data
    float x_step = screen_width / static_cast<float>(fft_n - 1);
    float db_range = max_db - min_db;

    // Calculate the first point
    float first_db = std::clamp(magnitudes[0], min_db, max_db);
    int prev_y = static_cast<int>(
        bottom_y - (((first_db - min_db) / db_range) * graph_height));

    // Loop over every other point
    for (size_t i = 1; i < fft_n; i++) {
      int x1 = static_cast<int>((i - 1) * x_step);
      int x2 = static_cast<int>(i * x_step);

      // Calculate the new point
      float db = std::clamp(magnitudes[i], min_db, max_db);
      int current_y = static_cast<int>(
          bottom_y - (((db - min_db) / db_range) * graph_height));

      DrawLine(x1, prev_y, x2, current_y, BLUE);

      prev_y = current_y;
    }
  }

  void draw_rawIQ(std::vector<uint8_t> &rawIQ_buffer, std::size_t bytes_read,
                  float *volume_level, float screen_width, float bottom_y,
                  float top_y) {
    float graph_height = bottom_y - top_y;
    // The maximum pixels the signal can travel up or down from the center
    float max_amplitude = graph_height / 2.0f;
    float center_y = top_y + max_amplitude;

    float x_step = screen_width / static_cast<float>(bytes_read - 1);

    // Calculate the first point
    float val1 = (rawIQ_buffer[0] - 127.5f) / 127.5f;
    int prev_y =
        static_cast<int>(center_y - (val1 * max_amplitude * (*volume_level)));

    // Clamp to boundaries
    prev_y =
        std::clamp(prev_y, static_cast<int>(top_y), static_cast<int>(bottom_y));

    for (size_t i = 1; i < bytes_read; i++) {
      int x1 = static_cast<int>((i - 1) * x_step);
      int x2 = static_cast<int>(i * x_step);

      // Calculate the new point
      float val = (rawIQ_buffer[i] - 127.5f) / 127.5f;
      int current_y =
          static_cast<int>(center_y - (val * max_amplitude * (*volume_level)));

      // Clamp to boundaries
      current_y = std::clamp(current_y, static_cast<int>(top_y),
                             static_cast<int>(bottom_y));

      DrawLine(x1, prev_y, x2, current_y, GREEN);

      prev_y = current_y;
    }
  }
};
