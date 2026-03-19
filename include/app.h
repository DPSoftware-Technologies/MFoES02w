#ifndef APP_H
#include <GFX.h>
#include "i2c_dev.h"
#include "ads1115.h"

#define APP_H
class App {
    public:
        App();
        ~App();
        
        void init();
        int run();
    private:
        LinuxGFX gfx;
        
        I2CBus i2c;
        ADS1115 adc;

        void drawGauge(int x, int y, int r, float value, float minVal, float maxVal);
};
#endif // APP_H