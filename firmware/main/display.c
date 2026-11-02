#include "display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#define LCD_WIDTH 128
#define LCD_HEIGHT 128
#define LCD_PIXELS (LCD_WIDTH * LCD_HEIGHT)
#define COLOR_BG 0x0841
#define COLOR_WHITE 0xFFFF
#define COLOR_CYAN 0x07FF
#define COLOR_GREEN 0x07E0
#define COLOR_AMBER 0xFDE0
#define COLOR_RED 0xF800

static const char *TAG = "display";
static spi_device_handle_t s_spi;
static uint16_t *s_frame;

static const uint8_t *glyph(char c)
{
    static const uint8_t blank[7] = {0};
    /* Seven 5-bit rows, with bit 4 at the left edge. */
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
    };
    static const uint8_t dot[7] = {0,0,0,0,0,12,12};
    static const uint8_t colon[7] = {0,12,12,0,12,12,0};
    static const uint8_t dash[7] = {0,0,0,31,0,0,0};
    static const uint8_t slash[7] = {1,2,2,4,8,8,16};
    static const uint8_t plus[7] = {0,4,4,31,4,4,0};
    static const uint8_t question[7] = {14,17,1,2,4,0,4};
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    switch (c) {
    case '.': return dot;
    case ':': return colon;
    case '-': return dash;
    case '/': return slash;
    case '+': return plus;
    case '?': return question;
    default: return blank;
    }
}

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h; row++) {
        if (row < 0 || row >= LCD_HEIGHT) continue;
        for (int col = x; col < x + w; col++) {
            if (col >= 0 && col < LCD_WIDTH) s_frame[row * LCD_WIDTH + col] = color;
        }
    }
}

static void draw_text(int x, int y, const char *text, uint16_t color, unsigned scale)
{
    for (; *text && x < LCD_WIDTH; text++, x += (int)(6 * scale)) {
        const uint8_t *rows = glyph(*text);
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (rows[row] & (1U << (4 - col)))
                    fill_rect(x + col * (int)scale, y + row * (int)scale,
                              (int)scale, (int)scale, color);
            }
        }
    }
}

static void draw_value_line(int y, char pack, const pb_pack_sample_t *sample)
{
    char line[24];
    if (!sample || !sample->bus_valid) {
        snprintf(line, sizeof(line), "%c SENSOR --", pack);
        draw_text(5, y, line, COLOR_AMBER, 1);
        return;
    }
    if (sample->current_valid) {
        snprintf(line, sizeof(line), "%c %lu.%02luV %ldmA", pack,
                 (unsigned long)(sample->bus_mv / 1000),
                 (unsigned long)((sample->bus_mv % 1000) / 10),
                 (long)sample->current_ma);
    } else {
        snprintf(line, sizeof(line), "%c %lu.%02luV --A", pack,
                 (unsigned long)(sample->bus_mv / 1000),
                 (unsigned long)((sample->bus_mv % 1000) / 10));
    }
    draw_text(5, y, line, COLOR_WHITE, 1);
}

/* Draw a big two-digit percentage, scale 3. */
static void draw_soc_big(int x, int y, unsigned pct, uint16_t color)
{
    char buf[8];
    if (pct > 100) pct = 100;
    int n = snprintf(buf, sizeof(buf), "%u%%", pct);
    draw_text(x, y, buf, color, 3);
    (void)n;
}

/* Horizontal battery-style bar. */
static void draw_bar(int x, int y, int w, int h, unsigned pct, uint16_t fill)
{
    fill_rect(x, y, w, h, 0x2126);
    int inner = (int)((uint32_t)w * pct / 100);
    if (inner > 0) fill_rect(x + 1, y + 1, inner - 2, h - 2, fill);
}

static const char *state_name(pb_system_state_t st)
{
    switch (st) {
    case PB_STATE_OFF: return "OFF";
    case PB_STATE_READY: return "READY";
    case PB_STATE_RUN: return "RUN";
    case PB_STATE_FAULT: return "FAULT";
    case PB_STATE_CHARGING: return "CHRG";
    default: return "--";
    }
}

esp_err_t pb_display_init(void)
{
    s_frame = heap_caps_calloc(LCD_PIXELS, sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_frame) return ESP_ERR_NO_MEM;

    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_PB_LCD_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bl_cfg), TAG, "backlight GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_PB_LCD_BL_GPIO, 1), TAG, "backlight enable failed");

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = CONFIG_PB_LCD_SCLK_GPIO,
        .mosi_io_num = CONFIG_PB_LCD_MOSI_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_PIXELS * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG,
                        "SPI bus init failed");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CONFIG_PB_LCD_DC_GPIO,
        .cs_gpio_num = CONFIG_PB_LCD_CS_GPIO,
        .pclk_hz = 20 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 2,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                                  &io_cfg, &io), TAG, "LCD IO init failed");
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONFIG_PB_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9107(io, &panel_cfg, &s_panel), TAG,
                        "GC9107 driver init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "LCD reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "LCD init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 0, 0), TAG, "LCD gap setup failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "LCD display enable failed");

    ESP_LOGI(TAG, "GC9107 128x128 SPI display ready");
    return ESP_OK;
}

void pb_display_update(const pb_pack_sample_t *a, const pb_pack_sample_t *b,
                       unsigned energy_a_mwh, unsigned energy_b_mwh,
                       bool wifi_connected, bool espnow_ready,
                       const pb_output_state_t *outs, int screen_index)
{
    if (!s_panel || !s_frame) return;
    for (unsigned i = 0; i < LCD_PIXELS; i++) s_frame[i] = COLOR_BG;
    fill_rect(0, 0, LCD_WIDTH, 16, 0x2126);
    char header[24];
    snprintf(header, sizeof(header), "PB64 NODE %u", CONFIG_PB_NODE_ID);
    draw_text(5, 4, header, COLOR_CYAN, 1);
    fill_rect(5, 25, 118, 1, 0x5ACB);

    if (screen_index == 0) {
        /* Screen 1 — Main: big SoC, V/A/W, output dots */
        unsigned soc = 0;
        if (a && a->bus_valid) soc += (unsigned)((a->bus_mv - 3200) * 100 / 1000);
        if (b && b->bus_valid) soc += (unsigned)((b->bus_mv - 3200) * 100 / 1000);
        if (soc > 100) soc = 100;
        draw_soc_big(20, 32, soc, COLOR_GREEN);
        char line[24];
        int32_t total_ma = (a && a->current_valid ? a->current_ma : 0) +
                           (b && b->current_valid ? b->current_ma : 0);
        uint32_t bus_mv = a && a->bus_valid ? a->bus_mv : (b && b->bus_valid ? b->bus_mv : 0);
        if (bus_mv) {
            snprintf(line, sizeof(line), "%lu.%02luV %ldmA",
                     (unsigned long)(bus_mv / 1000),
                     (unsigned long)((bus_mv % 1000) / 10),
                     (long)total_ma);
            draw_text(5, 70, line, COLOR_WHITE, 1);
        }
        snprintf(line, sizeof(line), "E %lu/%luWh",
                 (unsigned long)(energy_a_mwh / 1000),
                 (unsigned long)(energy_b_mwh / 1000));
        draw_text(5, 84, line, COLOR_GREEN, 1);
        if (outs) {
            draw_text(5, 100, state_name(outs->state),
                      outs->state == PB_STATE_RUN ? COLOR_GREEN :
                      outs->state == PB_STATE_CHARGING ? COLOR_AMBER : COLOR_WHITE, 1);
            int dots[PB_OUT_COUNT] = {0, 0, 0};
            for (int i = 0; i < PB_OUT_COUNT; i++) dots[i] = outs->out_on[i] ? 1 : 0;
            int x = 5;
            for (int i = 0; i < PB_OUT_COUNT; i++) {
                fill_rect(x, 114, 8, 8, dots[i] ? COLOR_GREEN : 0x2126);
                x += 16;
            }
        }
    } else if (screen_index == 1) {
        /* Screen 2 — Per-block detail */
        draw_text(5, 30, "BLOCK A", COLOR_CYAN, 1);
        draw_value_line(44, 'A', a);
        draw_text(5, 74, "BLOCK B", COLOR_CYAN, 1);
        draw_value_line(88, 'B', b);
        if (a && a->bus_valid) {
            unsigned soc = (unsigned)((a->bus_mv - 3200) * 100 / 1000);
            draw_bar(5, 110, 118, 8, soc, COLOR_GREEN);
        }
    } else if (screen_index == 2) {
        /* Screen 3 — Power meter */
        draw_text(5, 30, "POWER METER", COLOR_CYAN, 1);
        int32_t total_ma = (a && a->current_valid ? a->current_ma : 0) +
                           (b && b->current_valid ? b->current_ma : 0);
        uint32_t bus_mv = a && a->bus_valid ? a->bus_mv : (b && b->bus_valid ? b->bus_mv : 0);
        if (bus_mv) {
            uint32_t mw = (uint32_t)bus_mv * (uint32_t)(total_ma < 0 ? -total_ma : total_ma) / 1000;
            char line[24];
            snprintf(line, sizeof(line), "%ld mA", (long)total_ma);
            draw_text(5, 56, line, COLOR_WHITE, 1);
            snprintf(line, sizeof(line), "%lu W", (unsigned long)mw);
            draw_text(5, 74, line, COLOR_AMBER, 1);
        }
        snprintf(line, sizeof(line), "Wh A %lu  B %lu",
                 (unsigned long)(energy_a_mwh / 1000),
                 (unsigned long)(energy_b_mwh / 1000));
        draw_text(5, 98, line, COLOR_WHITE, 1);
        draw_text(5, 114, "hold OK to reset", COLOR_RED, 1);
    } else {
        /* Screen 4 — Settings / state */
        draw_text(5, 30, "SETTINGS", COLOR_CYAN, 1);
        if (outs) {
            char line[24];
            snprintf(line, sizeof(line), "state %s", state_name(outs->state));
            draw_text(5, 52, line, COLOR_WHITE, 1);
            snprintf(line, sizeof(line), "chg %s chrg %s",
                     outs->charger_present ? "Y" : "N",
                     outs->charging ? "Y" : "N");
            draw_text(5, 68, line, COLOR_WHITE, 1);
        }
        snprintf(line, sizeof(line), "12V %s 24V %s USB %s",
                 outs && outs->out_on[PB_OUT_12V] ? "ON" : "off",
                 outs && outs->out_on[PB_OUT_24V] ? "ON" : "off",
                 outs && outs->out_on[PB_OUT_USB] ? "ON" : "off");
        draw_text(5, 88, line, COLOR_WHITE, 1);
        draw_text(5, 114, "hold LEFT=home", COLOR_RED, 1);
    }

    draw_text(5, 126, "<  >  OK", COLOR_CYAN, 1);

    for (unsigned i = 0; i < LCD_PIXELS; i++) s_frame[i] = __builtin_bswap16(s_frame[i]);
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, s_frame);
    if (err != ESP_OK) ESP_LOGW(TAG, "LCD refresh failed: %s", esp_err_to_name(err));
}
