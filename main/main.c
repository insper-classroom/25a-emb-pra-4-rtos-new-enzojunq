#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "pico/stdlib.h"
#include "ssd1306.h"
#include "gfx.h"
#include <stdio.h>

#define TRIGGER_PIN 2              
#define ECHO_PIN 3                 
#define TRIGGER_PULSE_US 10/1000        
#define MEASUREMENT_INTERVAL_MS 60 
#define MAX_PULSE_US 30000         
#define TIME_TO_CM 58.0            


SemaphoreHandle_t xSemaphoreTrigger; 
QueueHandle_t xQueueTime;            
QueueHandle_t xQueueDistance;        
QueueHandle_t xQueueRisingTime;     


void pin_callback(uint gpio, int events) {
    int now = to_us_since_boot(get_absolute_time());
    int rising_time_local;
    
    if (events & GPIO_IRQ_EDGE_RISE) {
        
        xQueueSendFromISR(xQueueRisingTime, &now, 0);
    } else if (events & GPIO_IRQ_EDGE_FALL) {
        
        if (xQueueReceiveFromISR(xQueueRisingTime, &rising_time_local, 0) == pdPASS) {
            int pulse_duration = now - rising_time_local;
            
            xQueueSendFromISR(xQueueTime, &pulse_duration, 0);
        }
    }
}


void trigger_task(void *p) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);

    while (1) {
        gpio_put(TRIGGER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(TRIGGER_PULSE_US));
        gpio_put(TRIGGER_PIN, 0);


        xSemaphoreGive(xSemaphoreTrigger);

        vTaskDelay(pdMS_TO_TICKS(MEASUREMENT_INTERVAL_MS));
    }
}


void echo_task(void *p) {
    uint32_t pulse_duration;
    float distance;

    while (1) {
        if (xQueueReceive(xQueueTime, &pulse_duration, pdMS_TO_TICKS(100)) == pdPASS) {
            if (pulse_duration > MAX_PULSE_US) {
                distance = -1.0; 
            } else {
                distance = pulse_duration / TIME_TO_CM;
            }
        } else {
            distance = -1.0; 
        }
        xQueueSend(xQueueDistance, &distance, 0);
    }
}


void oled_task(void *p) {
    printf("Initializing OLED driver\n");
    ssd1306_init();
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    float distance;
    char buffer[32];

    while (1) {

        // if (xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (xQueueReceive(xQueueDistance, &distance, pdMS_TO_TICKS(100)) == pdPASS) {
                gfx_clear_buffer(&disp);
                if (distance < 0) {
                    gfx_draw_string(&disp, 0, 0, 1, "Falha");
                } else {
                    sprintf(buffer, "Dist: %.1f cm", distance);
                    gfx_draw_string(&disp, 0, 0, 1, buffer);
                    int bar_length = (int)(distance);
                    if (bar_length > 128)
                        bar_length = 128;
                    gfx_draw_line(&disp, 0, 20, bar_length, 20);
                }
                gfx_show(&disp);
            }
        // }
    }
}


int main() {
    stdio_init_all();
    printf("Iniciando projeto: Sensor HC-SR04 com OLED e RTOS\n");

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_pull_down(ECHO_PIN);

    gpio_set_irq_enabled_with_callback(ECHO_PIN,
                                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true,
                                       &pin_callback);


    xQueueRisingTime = xQueueCreate(10, sizeof(uint32_t));
    xQueueTime = xQueueCreate(10, sizeof(uint32_t));
    xQueueDistance = xQueueCreate(10, sizeof(float));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "TriggerTask", 256, NULL, 1, NULL);
    xTaskCreate(echo_task, "EchoTask", 256, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLEDTask", 4096, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1)
        ;
}