#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#include <WiFi.h>

#include "SouthbridgePico.h"

/*  WiFi credentials  */
const char* WIFI_SSID = "MotorPool24"; // Private WiFi network
const char* WIFI_PASS = "strykernet24";

/*  Southbridge instance  */
/*
 * SouthbridgePico(audio_spi, audio_cs, audio_sck, audio_mosi, audio_miso,
 *                  cmd_spi,   cmd_cs,   cmd_sck,   cmd_mosi,   cmd_miso,
 *                  i2s_data,  i2s_bck,  i2s_ws)
 *
 * i2s_bck and i2s_ws MUST be consecutive GPIO numbers (ws = bck + 1).
 * The arduino-pico I2S library enforces this.
 */
SouthbridgePico sb(
    spi0,  5,  2,  3,  4,   /* SPI0 audio slave: CS0=5, SCK=2, MOSI=3, MISO=4 */
    spi1, 13, 10, 11, 12,   /* SPI1 cmd slave:   CS1=13,SCK=10,MOSI=11,MISO=12 */
    26, 27, 28               /* I2S: DATA=26, BCK=27, WS=28 (BCK+1)             */
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
    Serial.println("Connecting to wifi");
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
    delay(2000);  // Extended delay for USB enumeration
    Serial.println("Southbridge Pico2W starting...");
    Serial.flush();  // ← CRITICAL: Ensure print is sent before FreeRTOS starts

    /* Register command handler BEFORE starting southbridge */
    sb.onCommand(handleCommand);

    Serial.println("Calling sb.begin(true)...");
    Serial.flush();
    
    /* Start southbridge (creates audio tasks internally) */
    sb.begin(true);

    Serial.println("sb.begin() returned. Creating application tasks...");
    Serial.flush();

    delay(500);  // Give southbridge time to stabilize

    /* Application tasks */
    BaseType_t res;
    res = xTaskCreate(taskNetwork, "net", 8192, nullptr, 2, nullptr);
    if (res != pdPASS) Serial.println("ERROR: taskNetwork creation failed");
    else Serial.println("taskNetwork created OK");
    
    res = xTaskCreate(taskGPS, "gps", 1024, nullptr, 2, nullptr);
    if (res != pdPASS) Serial.println("ERROR: taskGPS creation failed");
    else Serial.println("taskGPS created OK");
    
    res = xTaskCreate(taskSensor, "sensor", 512, nullptr, 1, nullptr);
    if (res != pdPASS) Serial.println("ERROR: taskSensor creation failed");
    else Serial.println("taskSensor created OK");
    
    res = xTaskCreate(taskGPIO, "gpio", 512, nullptr, 1, nullptr);
    if (res != pdPASS) Serial.println("ERROR: taskGPIO creation failed");
    else Serial.println("taskGPIO created OK");

    Serial.println("All tasks created. FreeRTOS scheduler running.");
    Serial.flush();
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
