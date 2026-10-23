#include "telemetry.h"

#include <string.h>
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"

static const char *TAG = "telemetry";
static uint8_t s_peer[ESP_NOW_ETH_ALEN];
static uint8_t s_lmk[ESP_NOW_KEY_LEN];
static bool s_ready;
static uint32_t s_sequence;

static int hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool parse_mac(const char *text, uint8_t mac[6])
{
    if (!text || strlen(text) != 17) return false;
    for (size_t i = 0; i < 6; i++) {
        size_t at = i * 3;
        int hi = hex_nibble(text[at]), lo = hex_nibble(text[at + 1]);
        if (hi < 0 || lo < 0 || (i < 5 && text[at + 2] != ':')) return false;
        mac[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool parse_lmk(const char *text, uint8_t key[16])
{
    if (!text || strlen(text) != 32) return false;
    for (size_t i = 0; i < 16; i++) {
        int hi = hex_nibble(text[i * 2]), lo = hex_nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        key[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static void send_cb(const uint8_t *mac, esp_now_send_status_t status)
{
    (void)mac;
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "ESP-NOW MAC delivery failed");
    }
}

void pb_telemetry_init(void)
{
    if (!parse_mac(CONFIG_PB_ESPNOW_PEER_MAC, s_peer) ||
        !parse_lmk(CONFIG_PB_ESPNOW_LMK_HEX, s_lmk)) {
        ESP_LOGW(TAG, "ESP-NOW TX disabled: configure a peer MAC and 16-byte LMK in local sdkconfig");
        return;
    }
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed");
        return;
    }
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_peer, sizeof(s_peer));
    memcpy(peer.lmk, s_lmk, sizeof(s_lmk));
    peer.channel = 0; /* follow the AP's current STA channel */
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW peer registration failed");
        esp_now_deinit();
        return;
    }
    esp_now_register_send_cb(send_cb);
    s_ready = true;
    ESP_LOGI(TAG, "encrypted ESP-NOW telemetry ready; node_id=%u", CONFIG_PB_NODE_ID);
}

bool pb_telemetry_ready(void)
{
    return s_ready;
}

void pb_telemetry_send(const pb_pack_sample_t *a, const pb_pack_sample_t *b,
                       uint32_t energy_a_mwh, uint32_t energy_b_mwh,
                       bool wifi_connected)
{
    if (!s_ready) return;
    pb_telemetry_packet_t packet = {0};
    packet.magic = PB_TELEMETRY_MAGIC;
    packet.version = PB_TELEMETRY_VERSION;
    packet.type = PB_TELEMETRY_TYPE_STATUS;
    packet.node_id = CONFIG_PB_NODE_ID;
    packet.sequence = s_sequence++;
    packet.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
    packet.energy_a_mwh = energy_a_mwh;
    packet.energy_b_mwh = energy_b_mwh;
    if (a) {
        if (a->bus_valid) packet.flags |= PB_FLAG_PACK_A_BUS_VALID;
        if (a->current_valid) packet.flags |= PB_FLAG_PACK_A_CURRENT_VALID;
        packet.pack_a_mv = a->bus_mv;
        packet.pack_a_ma = a->current_ma;
    }
    if (b) {
        if (b->bus_valid) packet.flags |= PB_FLAG_PACK_B_BUS_VALID;
        if (b->current_valid) packet.flags |= PB_FLAG_PACK_B_CURRENT_VALID;
        packet.pack_b_mv = b->bus_mv;
        packet.pack_b_ma = b->current_ma;
    }
    if (wifi_connected) packet.flags |= PB_FLAG_WIFI_CONNECTED;
    esp_err_t err = esp_now_send(s_peer, (const uint8_t *)&packet, sizeof(packet));
    if (err != ESP_OK) ESP_LOGW(TAG, "ESP-NOW enqueue failed: %s", esp_err_to_name(err));
}
