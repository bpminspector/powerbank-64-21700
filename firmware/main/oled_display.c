#include "oled_display.h"

#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_FRAME_SIZE (OLED_WIDTH * OLED_HEIGHT / 8)
#define DISPLAY_PAGE_COUNT 4

static const char *TAG = "oled";
static i2c_master_dev_handle_t s_oled;
static uint8_t s_frame[OLED_FRAME_SIZE];
static uint8_t s_transfer[OLED_FRAME_SIZE + 1];

static const uint8_t *glyph(char c)
{
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t digits[10][5] = {
        {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
        {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
    };
    static const uint8_t letters[26][5] = {
        {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
        {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    };
    static const uint8_t dot[5] = {0, 0x60, 0x60, 0, 0};
    static const uint8_t dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t plus[5] = {0x08, 0x08, 0x3e, 0x08, 0x08};
    static const uint8_t colon[5] = {0, 0x36, 0x36, 0, 0};
    static const uint8_t slash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
    static const uint8_t percent[5] = {0x63, 0x13, 0x08, 0x64, 0x63};
    static const uint8_t exclamation[5] = {0, 0, 0x5f, 0, 0};
    static const uint8_t question[5] = {0x02, 0x01, 0x51, 0x09, 0x06};

    static const uint8_t lowercase_p[5] = {0x7c, 0x14, 0x14, 0x14, 0x08};
    static const uint8_t lowercase_a[5] = {0x20, 0x54, 0x54, 0x54, 0x78};
    static const uint8_t lowercase_s[5] = {0x48, 0x54, 0x54, 0x54, 0x20};
    static const uint8_t lowercase_h[5] = {0x7f, 0x08, 0x04, 0x04, 0x78};

    switch (c) {
    case 'p': return lowercase_p;
    case 'a': return lowercase_a;
    case 's': return lowercase_s;
    case 'h': return lowercase_h;
    default: break;
    }
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    switch (c) {
    case '.': return dot;
    case '-': return dash;
    case '+': return plus;
    case ':': return colon;
    case '/': return slash;
    case '%': return percent;
    case '!': return exclamation;
    case '?': return question;
    default: return blank;
    }
}

static void draw_pixel(int x, int y)
{
    if (x >= 0 && x < OLED_WIDTH && y >= 0 && y < OLED_HEIGHT) {
        s_frame[(y / 8) * OLED_WIDTH + x] |= (uint8_t)(1U << (y % 8));
    }
}

static void draw_hline(int x, int y, int width)
{
    for (int dx = 0; dx < width; dx++) draw_pixel(x + dx, y);
}

static void draw_vline(int x, int y, int height)
{
    for (int dy = 0; dy < height; dy++) draw_pixel(x, y + dy);
}

static void draw_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        draw_pixel(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int twice_error = 2 * error;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void draw_rect(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    draw_hline(x, y, width);
    draw_hline(x, y + height - 1, width);
    draw_vline(x, y, height);
    draw_vline(x + width - 1, y, height);
}

static void draw_circle(int cx, int cy, int radius, bool filled)
{
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int distance = x * x + y * y;
            if (filled ? distance <= radius * radius
                       : distance <= radius * radius &&
                             distance >= (radius - 1) * (radius - 1)) {
                draw_pixel(cx + x, cy + y);
            }
        }
    }
}

static void draw_bolt_icon(int x, int y, int scale)
{
    static const int points[][2] = {
        {4, 0}, {0, 5}, {3, 5}, {1, 10}, {7, 4}, {4, 4}, {4, 0},
    };
    for (int i = 0; i < 6; i++) {
        draw_line(x + points[i][0] * scale, y + points[i][1] * scale,
                  x + points[i + 1][0] * scale, y + points[i + 1][1] * scale);
    }
}

static void draw_current_icon(int x, int y, int scale)
{
    draw_hline(x + scale, y + 3 * scale, 7 * scale);
    draw_line(x + scale, y + 3 * scale, x + 3 * scale, y + scale);
    draw_line(x + scale, y + 3 * scale, x + 3 * scale, y + 5 * scale);
    draw_line(x + 8 * scale, y + 3 * scale, x + 6 * scale, y + scale);
    draw_line(x + 8 * scale, y + 3 * scale, x + 6 * scale, y + 5 * scale);
}

static void draw_thermometer_icon(int x, int y)
{
    draw_vline(x + 3, y, 6);
    draw_vline(x + 5, y, 6);
    draw_hline(x + 3, y, 3);
    draw_hline(x + 3, y + 6, 3);
    draw_circle(x + 4, y + 7, 2, false);
    draw_vline(x + 4, y + 3, 5);
}

static void draw_status_icon(int x, int y, bool ready)
{
    draw_circle(x, y, 3, ready);
    if (!ready) draw_circle(x, y, 1, true);
}

static void draw_text(int x, int y, const char *text)
{
    for (; *text && x < OLED_WIDTH; ++text, x += 6) {
        const uint8_t *columns = glyph(*text);
        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                if (!(columns[col] & (1U << row))) continue;
                draw_pixel(x + col, y + row);
            }
        }
    }
}

static void draw_text_scaled(int x, int y, const char *text, int scale)
{
    for (; *text && x < OLED_WIDTH; ++text, x += 6 * scale) {
        const uint8_t *columns = glyph(*text);
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (!(columns[col] & (1U << row))) continue;
                for (int dx = 0; dx < scale; dx++) {
                    for (int dy = 0; dy < scale; dy++) {
                        draw_pixel(x + col * scale + dx, y + row * scale + dy);
                    }
                }
            }
        }
    }
}

static void draw_battery_unknown_icon(void)
{
    draw_rect(103, 1, 18, 14);
    draw_rect(105, 3, 14, 10);
    draw_rect(121, 5, 3, 6);
    draw_text(109, 4, "?");
}

static void draw_header(const char *title, bool sensor_ready)
{
    draw_text(3, 4, title);
    draw_status_icon(69, 8, sensor_ready);
    draw_text(77, 4, sensor_ready ? "OK" : "ERR");
    draw_battery_unknown_icon();
    draw_hline(0, 14, OLED_WIDTH);
    draw_hline(0, 15, OLED_WIDTH);
}

static void draw_footer(int screen_index)
{
    draw_hline(0, 56, OLED_WIDTH);
    char page[8];
    snprintf(page, sizeof(page), "%d/4", screen_index + 1);
    draw_text(2, 57, page);
    draw_text(26, 57, "1 UP  4 DOWN  2 OK");
}

static void draw_splash_frame(uint32_t frame)
{
    memset(s_frame, 0, sizeof(s_frame));
    draw_text(0, 4, "DJ POWER UP");

    for (int i = 0; i < 4; i++) {
        uint32_t phase = (frame * 3 + (uint32_t)i * 5) % 16;
        int height = 3 + (int)(phase <= 8 ? phase : 16 - phase) * 2;
        int left_x = 4 + i * 5;
        int right_x = 123 - i * 5;
        for (int y = OLED_HEIGHT - 2; y >= OLED_HEIGHT - 2 - height; y--) {
            for (int dx = 0; dx < 3; dx++) {
                draw_pixel(left_x + dx, y);
                draw_pixel(right_x - dx, y);
            }
        }
    }

    for (int i = 0; i < 17; i++) {
        uint32_t phase = (frame * 2 + (uint32_t)i * 3) % 14;
        int height = 2 + (int)(phase <= 7 ? phase : 14 - phase);
        int x = 24 + i * 5;
        for (int y = 62; y > 62 - height; y--) {
            for (int dx = 0; dx < 3; dx++) draw_pixel(x + dx, y);
        }
    }

    draw_text_scaled(34, 20, "PASHA", 2);
    draw_text_scaled(28, 36, "POWER!", 2);

    const int sparkle_x[] = {27, 98, 25, 101, 29, 96};
    const int sparkle_y[] = {21, 21, 36, 36, 51, 51};
    for (int i = 0; i < 6; i++) {
        uint32_t phase = (frame + (uint32_t)i * 3) % 8;
        int x = sparkle_x[i];
        int y = sparkle_y[i];
        if (phase < 4) {
            draw_pixel(x, y - 2);
            draw_pixel(x, y + 2);
            draw_pixel(x - 2, y);
            draw_pixel(x + 2, y);
        } else {
            draw_pixel(x - 1, y - 1);
            draw_pixel(x + 1, y - 1);
            draw_pixel(x - 1, y + 1);
            draw_pixel(x + 1, y + 1);
        }
    }
}

static esp_err_t write_commands(const uint8_t *commands, size_t count)
{
    uint8_t data[32];
    if (count + 1 > sizeof(data)) return ESP_ERR_INVALID_SIZE;
    data[0] = 0x00;
    memcpy(&data[1], commands, count);
    return i2c_master_transmit(s_oled, data, count + 1, 100);
}

esp_err_t pb_display_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_STATE;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_PB_OLED_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &s_oled), TAG,
                        "OLED I2C device add failed");

    static const uint8_t init_commands[] = {
        0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40,
        0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x12,
        0x81, 0x7f, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0xaf,
    };
    esp_err_t err = write_commands(init_commands, sizeof(init_commands));
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(s_oled);
        s_oled = NULL;
        return err;
    }
    ESP_LOGI(TAG, "SSD1306-compatible 128x64 OLED initialized at 0x%02x",
             CONFIG_PB_OLED_I2C_ADDRESS);
    return ESP_OK;
}

static esp_err_t flush_frame(void)
{
    s_transfer[0] = 0x40;
    memcpy(&s_transfer[1], s_frame, sizeof(s_frame));
    const uint8_t window[] = {0x21, 0x00, 0x7f, 0x22, 0x00, 0x07};
    ESP_RETURN_ON_ERROR(write_commands(window, sizeof(window)), TAG,
                        "OLED refresh window failed");
    return i2c_master_transmit(s_oled, s_transfer, sizeof(s_transfer), 100);
}

void pb_display_show_splash(uint32_t duration_ms)
{
    if (!s_oled || duration_ms == 0) return;

    const TickType_t frame_delay = pdMS_TO_TICKS(100);
    uint32_t frame_count = duration_ms / 100;
    if (frame_count == 0) frame_count = 1;
    for (uint32_t frame = 0; frame < frame_count; frame++) {
        draw_splash_frame(frame);
        esp_err_t err = flush_frame();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "OLED splash refresh failed: %s", esp_err_to_name(err));
            return;
        }
        vTaskDelay(frame_delay);
    }
}

void pb_display_update(const pb_pack_sample_t *sample,
                      const pb_temp_raw_sample_t *temperature,
                      const pb_dht_sample_t *dht,
                      uint64_t energy_mwh, uint64_t discharge_mah,
                      bool sensor_ready, int screen_index)
{
    if (!s_oled) return;
    memset(s_frame, 0, sizeof(s_frame));
    char line[32];
    static const char *const page_titles[DISPLAY_PAGE_COUNT] = {
        "OVERVIEW", "LIVE DATA", "COUNTERS", "SYSTEM",
    };
    const char *title = screen_index >= 0 &&
                        screen_index < (int)(sizeof(page_titles) / sizeof(page_titles[0]))
                        ? page_titles[screen_index] : "SYSTEM";
    draw_header(title, sensor_ready);
    switch (screen_index) {
    case 0:
        if (sample && sample->bus_valid) {
            snprintf(line, sizeof(line), "%lu.%02luV",
                     (unsigned long)(sample->bus_mv / 1000),
                     (unsigned long)((sample->bus_mv % 1000) / 10));
            draw_rect(3, 18, 122, 28);
            draw_bolt_icon(9, 23, 2);
            draw_text(28, 21, "PACK VOLTAGE");
            draw_text_scaled(28, 29, line, 2);
        } else {
            draw_rect(3, 18, 122, 28);
            draw_circle(14, 32, 5, false);
            draw_text(12, 28, "!");
            draw_text(26, 29, "VOLTAGE SENSOR ERROR");
        }
        draw_hline(3, 47, 122);
        draw_current_icon(5, 49, 1);
        if (sample && sample->current_valid) {
            snprintf(line, sizeof(line), "%+ldmA", (long)sample->current_ma);
            draw_text(17, 49, line);
        } else {
            draw_text(17, 49, "N/A");
        }
        draw_vline(65, 49, 5);
        draw_text(73, 49, "P");
        if (sample && sample->power_valid) {
            int64_t power_mw = sample->power_mw;
            uint64_t magnitude = (uint64_t)(power_mw < 0 ? -power_mw : power_mw);
            snprintf(line, sizeof(line), "%s%llu.%02lluW",
                     power_mw < 0 ? "-" : "",
                     (unsigned long long)(magnitude / 1000),
                     (unsigned long long)((magnitude % 1000) / 10));
            draw_text(84, 49, line);
        } else {
            draw_text(84, 49, "N/A");
        }
        break;
    case 1:
        if (sample && sample->bus_valid) {
            snprintf(line, sizeof(line), "%lu.%02luV",
                     (unsigned long)(sample->bus_mv / 1000),
                     (unsigned long)((sample->bus_mv % 1000) / 10));
        } else {
            snprintf(line, sizeof(line), "ERR");
        }
        draw_bolt_icon(5, 18, 2);
        draw_text_scaled(22, 18, line, 2);
        draw_hline(4, 32, 120);
        draw_current_icon(5, 34, 1);
        if (sample && sample->bus_valid) {
            snprintf(line, sizeof(line), "SHUNT %+lduV", (long)sample->shunt_uv);
            draw_text(18, 34, line);
        } else {
            draw_text(18, 34, "SHUNT N/A");
        }
        draw_hline(4, 41, 120);
        draw_current_icon(5, 42, 1);
        if (sample && sample->current_valid) {
            snprintf(line, sizeof(line), "CURRENT %+ldmA", (long)sample->current_ma);
            draw_text(18, 42, line);
        } else {
            draw_text(18, 42, "CURRENT N/A");
        }
        draw_hline(4, 48, 120);
        if (sample && sample->power_valid) {
            snprintf(line, sizeof(line), "POWER %+ldmW", (long)sample->power_mw);
            draw_text(18, 49, line);
        } else {
            draw_text(18, 49, "POWER N/A");
        }
        break;
    case 2:
        draw_rect(3, 18, 59, 28);
        draw_rect(66, 18, 59, 28);
        draw_bolt_icon(8, 23, 1);
        draw_text(20, 22, "ENERGY");
        snprintf(line, sizeof(line), "%llu.%01lluWh",
                 (unsigned long long)(energy_mwh / 1000),
                 (unsigned long long)((energy_mwh % 1000) / 100));
        draw_text(7, 34, line);
        draw_current_icon(71, 23, 1);
        draw_text(84, 22, "USED");
        snprintf(line, sizeof(line), "%llu.%02lluAh",
                 (unsigned long long)(discharge_mah / 1000),
                 (unsigned long long)((discharge_mah % 1000) / 10));
        draw_text(70, 34, line);
        draw_text(34, 49, "2OK RESET");
        break;
    default:
        draw_rect(3, 18, 122, 12);
        draw_status_icon(12, 24, sensor_ready);
        draw_text(22, 21, sensor_ready ? "INA226 CONNECTED" : "INA226 FAULT");
        draw_hline(4, 32, 120);
        draw_thermometer_icon(6, 35);
        if (dht && dht->valid) {
            int temp_abs = dht->temperature_c10 < 0
                               ? -dht->temperature_c10 : dht->temperature_c10;
            snprintf(line, sizeof(line), "DHT T %s%d.%dC",
                     dht->temperature_c10 < 0 ? "-" : "",
                     temp_abs / 10, temp_abs % 10);
        } else {
            snprintf(line, sizeof(line), "DHT T N/A");
        }
        draw_text(20, 34, line);
        if (dht && dht->valid) {
            snprintf(line, sizeof(line), "HUM %d.%d%%",
                     dht->humidity_pct10 / 10, dht->humidity_pct10 % 10);
        } else {
            snprintf(line, sizeof(line), "HUM N/A");
        }
        draw_text(20, 42, line);
        if (temperature && temperature->valid[0] && temperature->valid[1]) {
            snprintf(line, sizeof(line), "N1:%d N2:%d",
                     temperature->raw[0], temperature->raw[1]);
        } else {
            snprintf(line, sizeof(line), "NTC ADC N/A");
        }
        draw_text(20, 49, line);
        break;
    }

    draw_footer(screen_index >= 0 && screen_index < DISPLAY_PAGE_COUNT ? screen_index : 3);
    esp_err_t err = flush_frame();
    if (err != ESP_OK) ESP_LOGW(TAG, "OLED refresh failed: %s", esp_err_to_name(err));
}
