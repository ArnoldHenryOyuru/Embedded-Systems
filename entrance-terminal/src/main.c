/*
 * Standalone demo for the entrance module.
 * Copy into your project's main/ folder to test Member 1's work on its own in Wokwi.
 * In the integrated build, the app/storage module initialises the SPI bus instead.
 */
#include "driver/spi_master.h"
#include "esp_log.h"
#include "entrance.h"
#include "feedback.h"

#define PIN_SPI_MOSI 23
#define PIN_SPI_MISO 19
#define PIN_SPI_SCK  18

void app_main(void) {
    entrance_init();
    feedback_init();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SPI_MOSI,
        .miso_io_num = PIN_SPI_MISO,
        .sclk_io_num = PIN_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    entrance_rfid_init();
    entrance_start();
    feedback_start();
}
