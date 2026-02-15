#pragma once
#include <cstddef>
#include <cstdint>
#include <raylib.h>
#include <string>
#include <vector>

class GUIWindow {
public:
  GUIWindow(int width, int height, const std::string &title) {
    InitWindow(width, height, title.c_str());
    SetTargetFPS(60);
  }

  ~GUIWindow() { CloseWindow(); }

  GUIWindow(const GUIWindow &) = delete;
  GUIWindow &operator=(const GUIWindow &) = delete;

  bool should_close() { return WindowShouldClose(); }

  void draw(std::vector<uint8_t> &buffer, std::size_t bytes_read) {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    DrawText("AETHER SDR", 10, 10, 20, DARKGRAY);

    if (bytes_read > 0) {
      for (size_t i = 0; i < bytes_read - 1; i++) {
        // Point 1
        int x1 = static_cast<int>(i * 1);
        // Scale 0-255 to 0-600 pixels
        int y1 = 600 - (buffer[i] * 600 / 255);

        // Point 2 (Next sample)
        int x2 = static_cast<int>((i + 1) * 1);
        int y2 = 600 - (buffer[i + 1] * 600 / 255);

        DrawLine(x1, y1, x2, y2, GREEN);
      }
    }
    DrawFPS(10, 80);
    EndDrawing();
  }
};
