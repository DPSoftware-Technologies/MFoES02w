#ifndef APP_H
#include <GFX.h>
#include "i2c_dev.h"
#include "ads1115.h"
#include "usbd_client.h"

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
        UsbdClient usbdc;

        void drawGauge(int x, int y, int r, float value, float minVal, float maxVal);

        // create last status message for display
        char statusMsg[128];
        pthread_t usb_thread;
        static void* usbThreadFunc(void* arg);
        void usbLoop();

        
};
#endif // APP_H