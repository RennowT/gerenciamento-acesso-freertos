#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// Hardware definitions
#define LED_ROOM_1 13
#define LED_ROOM_2 14
#define BUTTON_ROOM_1 26
#define BUTTON_ROOM_2 35

void setup() {
    Serial.begin(115200);
    pinMode(LED_ROOM_1, OUTPUT);
    pinMode(LED_ROOM_2, OUTPUT);
    pinMode(BUTTON_ROOM_1, INPUT_PULLUP);
    pinMode(BUTTON_ROOM_2, INPUT_PULLUP);

    digitalWrite(LED_ROOM_1, LOW);
    digitalWrite(LED_ROOM_2, LOW);

}

void loop() {
    
}
