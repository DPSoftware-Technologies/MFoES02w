#include "UIwidget.h"

uint32_t getColorForValue(float value, uint32_t defaultColor, const std::vector<ColorThreshold>& thresholds) {
    uint32_t selectedColor = defaultColor;
    
    for (const auto& ct : thresholds) {
        if (value >= ct.threshold) {
            selectedColor = ct.color;
        } else {
            // Since list is sorted, we can stop once value is lower than threshold
            break; 
        }
    }
    return selectedColor;
}

void drawBarContainer(LinuxGFX& gfx, int x, int y, int w, int h, float minVal, float maxVal, const std::vector<float>& values, char const* label, bool vertical, uint32_t barColor, const std::vector<ColorThreshold>& thresholds) {
    if (values.empty()) return;

    int numBars = values.size();

    if (vertical) {
        gfx.setText(x + w / 2, y + h + 5, label);
        int barWidth = w / numBars;
        gfx.drawRect(x, y, w, h, 0xFFFFFFFF);

        for (int i = 0; i < numBars; i++) {
            float barPct = (values[i] - minVal) / (maxVal - minVal);
            uint32_t activeColor = getColorForValue(barPct * 100.0f, barColor, thresholds);
            int barHeight = static_cast<int>(barPct * h);
            gfx.fillRect(x + (i * barWidth), y + h - barHeight, barWidth, barHeight, activeColor);
        }
    } else {
        gfx.setText(x, y + h / 2, label);
        int graphX = x + 40;
        gfx.drawRect(graphX, y, w, h, 0xFFFFFFFF);

        int barHeight = h / numBars;
        for (int i = 0; i < numBars; i++) {
            float barPct = (values[i] - minVal) / (maxVal - minVal);
            uint32_t activeColor = getColorForValue(barPct * 100.0f, barColor, thresholds);
            int bar_length = static_cast<int>(barPct * w);
            gfx.fillRect(graphX, y + (i * barHeight), bar_length, std::ceil(static_cast<float>(barHeight)), activeColor);
        }
    }
}

void drawBar(LinuxGFX& gfx, int x, int y, int w, int h, float value, float minVal, float maxVal, char const* label, bool vertical, bool showPercent, uint32_t barColor, const std::vector<ColorThreshold>& thresholds) {
    float percent = ((value - minVal) / (maxVal - minVal));
    int percentInt = static_cast<int>(percent * 100);
    char percentBuf[8];
    snprintf(percentBuf, sizeof(percentBuf), "%d%%", percentInt);

    uint32_t activeColor = getColorForValue(percent * 100.0f, barColor, thresholds);

    if (vertical) {
        gfx.drawRect(x, y, w, h, 0xFFFFFFFF);
        gfx.setText(x + w / 2, y + h + 5, label);
        int barHeight = static_cast<int>(percent * h);
        gfx.fillRect(x, y + h - barHeight, w, barHeight, activeColor);

        if (showPercent && barHeight > 10) {
            gfx.setText(x + w / 2, y + h - (barHeight / 2), percentBuf);
        }
    } else {
        int graphX = x + 40;
        gfx.setText(x, y + h / 2, label);
        gfx.drawRect(graphX, y, w, h, 0xFFFFFFFF);
        int barLength = static_cast<int>(percent * w);
        gfx.fillRect(graphX, y, barLength, h, activeColor);

        if (showPercent && barLength > 20) {
            gfx.setText(graphX + (barLength / 2), y + h / 2, percentBuf);
        }
    }
}