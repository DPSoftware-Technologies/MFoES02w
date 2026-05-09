#pragma once

#include <GFX.h>
#include <cmath>
#include <cstdio>

struct ColorThreshold {
    float threshold;
    uint32_t color;
};

uint32_t getColorForValue(float value, uint32_t defaultColor, const std::vector<ColorThreshold>& thresholds);

void drawBarContainer(LinuxGFX& gfx, int x, int y, int w, int h, float minVal, float maxVal, const std::vector<float>& values, char const* label, bool vertical = true, uint32_t barColor = 0xFF00FF00, const std::vector<ColorThreshold>& thresholds = {});

void drawBar(LinuxGFX& gfx, int x, int y, int w, int h, float value, float minVal, float maxVal, char const* label, bool vertical = true, bool showPercent = false, uint32_t barColor = 0xFF00FF00, const std::vector<ColorThreshold>& thresholds = {});