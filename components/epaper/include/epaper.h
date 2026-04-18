#pragma once

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

#define EPAPER_WIDTH   296
#define EPAPER_HEIGHT  128
#define EPAPER_BUF_SIZE (EPAPER_WIDTH * EPAPER_HEIGHT / 8)

typedef struct {
    spi_host_device_t spi_host;
    int pin_mosi;
    int pin_sck;
    int pin_cs;
    int pin_dc;
    int pin_rst;
    int pin_busy;
} epaper_config_t;

typedef struct {
    spi_device_handle_t spi;
    int pin_cs;
    int pin_dc;
    int pin_rst;
    int pin_busy;
    uint8_t buf[EPAPER_BUF_SIZE];
} epaper_t;

esp_err_t epaper_init(epaper_t *dev, const epaper_config_t *cfg);
void epaper_clear(epaper_t *dev, uint8_t color);
void epaper_draw_char(epaper_t *dev, int x, int y, char c);
void epaper_draw_string(epaper_t *dev, int x, int y, const char *str);
void epaper_refresh(epaper_t *dev);
void epaper_sleep(epaper_t *dev);
