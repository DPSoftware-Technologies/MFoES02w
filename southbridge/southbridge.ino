#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#include <WiFi.h>

#include "SouthbridgePico.h"

/*  WiFi credentials  */
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";

/*  Southbridge instance  */
SouthbridgePico sb(
    spi0,  5,  2,  3,  4,   /* audio: CS0=5, SCK=2, MOSI=3, MISO=4  */
    spi1, 13, 10, 11, 12    /* cmd:   CS1=13,SCK=10,MOSI=11,MISO=12  */
);

/*  command handler  */
void handleCommand(const char* json, size_t /*len*/) {
    Serial.print("[CMD] "); Serial.println(json);

    /* parse volume */
    if (strstr(json, "\"cmd\":\"vol\"")) {
        /* extract "val" and apply to DAC */
    }
    /* parse GPS enable */
    if (strstr(json, "\"cmd\":\"gps\"")) {
        /* toggle GPS reporting */
    }
}

/*  Network task  */
void taskNetwork(void* /*arg*/) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.print("WiFi: "); Serial.println(WiFi.localIP());

    for (;;) {
        /* keep-alive ping, mDNS, MQTT, etc. */
        /* Example: send audio stats over UDP */
        char stats[128];
        snprintf(stats, sizeof(stats),
            "{\"ov\":%lu,\"un\":%lu,\"rx\":%lu}",
            (unsigned long)sb.audioOverflows(),
            (unsigned long)sb.audioUnderruns(),
            (unsigned long)sb.framesReceived());

        /* sendUDP(stats); */

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*  GPS task  */
void taskGPS(void* /*arg*/) {
    Serial1.begin(9600);   /* UART0: GPS module */
    char buf[128];
    int  idx = 0;

    for (;;) {
        while (Serial1.available()) {
            char c = (char)Serial1.read();
            if (c == '\n') {
                buf[idx] = '\0';
                idx = 0;
                /* parse NMEA sentence, send back to RPi */
                if (strncmp(buf, "$GPGGA", 6) == 0) {
                    char evt[60];
                    snprintf(evt, sizeof(evt), R"({"evt":"gps","nmea":"%s"})", buf);
                    sb.sendEvent(evt);
                }
            } else if (idx < (int)sizeof(buf) - 1) {
                buf[idx++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/*  Sensor task  */
void taskSensor(void* /*arg*/) {
    /* Example: read an ADC or I²C sensor every 100 ms */
    for (;;) {
        int raw = analogRead(A0);  /* GP26 */
        float voltage = raw * 3.3f / 4095.0f;

        /* send sensor value upstream if significant change */
        static float last_v = 0;
        if (fabsf(voltage - last_v) > 0.05f) {
            char evt[64];
            snprintf(evt, sizeof(evt), R"({"evt":"sensor","v":%.3f})", voltage);
            sb.sendEvent(evt);
            last_v = voltage;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*  GPIO / PWM task  */
void taskGPIO(void* /*arg*/) {
    /* Example: blink LED and drive a servo on GPIO 22 */
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;) {
        digitalWrite(LED_BUILTIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(500));
        digitalWrite(LED_BUILTIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/*  setup  */
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("Southbridge Pico2W starting...");

    /* Start southbridge (creates audio tasks internally) */
    sb.begin(/* use_i2s = */ true);
    sb.onCommand(handleCommand);

    /* Application tasks */
    xTaskCreate(taskNetwork, "net",     1024, nullptr, SB_TASK_PRIO_NETWORK, nullptr);
    xTaskCreate(taskGPS,     "gps",      512, nullptr, SB_TASK_PRIO_GPS,     nullptr);
    xTaskCreate(taskSensor,  "sensor",   256, nullptr, SB_TASK_PRIO_SENSOR,  nullptr);
    xTaskCreate(taskGPIO,    "gpio",     256, nullptr, SB_TASK_PRIO_SENSOR,  nullptr);

    Serial.println("All tasks created. FreeRTOS scheduler running.");
}

void loop() {
    /* arduino-pico's FreeRTOS integration; loop() runs in idle task */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Print diagnostics */
    Serial.printf("[audio] rcv=%lu ov=%lu un=%lu\n",
        (unsigned long)sb.framesReceived(),
        (unsigned long)sb.audioOverflows(),
        (unsigned long)sb.audioUnderruns());
}
