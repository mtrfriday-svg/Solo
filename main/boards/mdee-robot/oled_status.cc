#include "oled_status.h"
#include "config.h"
#include "mdee_robot.h"
#include "logic_battery.h"
#include "oled_font6x9.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <algorithm>
#include <esp_lcd_panel_vendor.h>
#include <cstring>
#include <cstdio>

#define TAG "OledStatus"

OledStatus& OledStatus::GetInstance() {
    static OledStatus inst;
    return inst;
}

bool OledStatus::Init(i2c_master_bus_handle_t bus) {
    if (i2c_master_probe(bus, OLED_I2C_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "OLED not found at 0x%02X", OLED_I2C_ADDR);
        return false;
    }
    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = OLED_I2C_ADDR;
    io_config.scl_speed_hz = 400 * 1000;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 6;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    if (esp_lcd_new_panel_io_i2c(bus, &io_config, &io_) != ESP_OK) return false;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.bits_per_pixel = 1;
    esp_lcd_panel_ssd1306_config_t ssd = {};
    ssd.height = OLED_HEIGHT;
    panel_config.vendor_config = &ssd;
    if (esp_lcd_new_panel_ssd1306(io_, &panel_config, &panel_) != ESP_OK) return false;
    esp_lcd_panel_reset(panel_);
    if (esp_lcd_panel_init(panel_) != ESP_OK) return false;
    esp_lcd_panel_invert_color(panel_, false);
    esp_lcd_panel_disp_on_off(panel_, true);
    ready_ = true;
    xTaskCreate(&OledStatus::Task, "oled", 4096, this, 2, nullptr);
    return true;
}

void OledStatus::SetState(const char* state) {
    std::lock_guard<std::mutex> l(mutex_);
    state_ = state ? state : "";
}

void OledStatus::SetChat(const char* role, const char* content) {
    std::lock_guard<std::mutex> l(mutex_);
    chat_role_ = role ? role : "";
    chat_text_ = content ? content : "";
}

void OledStatus::Task(void* arg) {
    auto* self = static_cast<OledStatus*>(arg);
    while (true) {
        self->Render();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void OledStatus::Clear() { memset(fb_, 0, sizeof(fb_)); }

void OledStatus::SetPixel(int x, int y) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    fb_[(y / 8) * OLED_WIDTH + x] |= (1 << (y % 8));          // SSD1306 page layout
}

void OledStatus::Text(int col, int row, const std::string& s) {
    // 6x9 cells: 21 columns x 7 rows; the 9th pixel row of every cell is blank = space between lines
    int x0 = col * 6, y0 = row * 9;
    for (char ch : s) {
        if (x0 + 6 > OLED_WIDTH || y0 + 9 > OLED_HEIGHT) break;
        unsigned char c = (unsigned char)ch;
        if (c < 32 || c > 126) c = '?';                 // non-ASCII (e.g. Chinese) -> '?'
        const uint16_t* g = kOledFont6x9[c - 32];
        for (int cx = 0; cx < 6; cx++)
            for (int cy = 0; cy < 9; cy++)
                if (g[cx] & (1 << cy)) SetPixel(x0 + cx, y0 + cy);
        x0 += 6;
    }
}

void OledStatus::Flush() {
    esp_lcd_panel_draw_bitmap(panel_, 0, 0, OLED_WIDTH, OLED_HEIGHT, fb_);
}

void OledStatus::Render() {
    if (!ready_) return;
    auto& robot = MdeeRobot::GetInstance();
    auto& lb = LogicBattery::GetInstance();
    char line[40];

    // Wi-Fi
    wifi_ap_record_t ap;
    bool wifi_ok = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
    int bars = !wifi_ok ? 0 : ap.rssi > -55 ? 4 : ap.rssi > -65 ? 3 : ap.rssi > -75 ? 2 : 1;

    std::string state, role, text;
    {
        std::lock_guard<std::mutex> l(mutex_);
        state = state_; role = chat_role_; text = chat_text_;
    }

    Clear();
    // row 0: Wi-Fi bars + assistant state (Listening / Speaking / Standby ...)
    snprintf(line, sizeof(line), "%.*s%.*s %s", bars, "||||", 4 - bars, "....", state.substr(0, 15).c_str());
    Text(0, 0, line);

    // row 1: motor pack (M) and logic pack (L)
    char m[16], l[16];
    float v = robot.BatteryVolts();
    if (robot.LinkUp() && v > 4.0f) snprintf(m, sizeof(m), "M%3.1fV%3d%%", v, robot.BatteryPercent());
    else snprintf(m, sizeof(m), "M --.-V   ");
    if (lb.Connected()) snprintf(l, sizeof(l), "L%3.1fV%3d%%", lb.Volts(), lb.Percent());
    else snprintf(l, sizeof(l), "L --.-V   ");
    snprintf(line, sizeof(line), "%s %s", m, l);
    Text(0, 1, line);

    // row 2: distances + mode, or a blinking alert
    bool blink = (esp_timer_get_time() / 500000) & 1;
    if (!robot.LinkUp()) {
        if (blink) Text(0, 2, "NO BODY LINK!");
    } else if (lb.Critical()) {
        Text(0, 2, "CHARGE NOW! STOPPED");
    } else if (lb.Low()) {
        if (blink) Text(0, 2, "!! LOGIC BATT LOW !!");
    } else {
        int f = robot.FrontCm(), r = robot.RearCm();
        char fs[8], rs[8];
        if (f >= 400) snprintf(fs, sizeof(fs), "--"); else snprintf(fs, sizeof(fs), "%d", f);
        if (r >= 400) snprintf(rs, sizeof(rs), "--"); else snprintf(rs, sizeof(rs), "%d", r);
        snprintf(line, sizeof(line), "F%s R%s %s", fs, rs, MdeeRobot::ModeName(robot.GetMode()));
        Text(0, 2, line);
    }
    for (int x = 0; x < OLED_WIDTH; x++) SetPixel(x, 26);         // thin separator line

    // rows 3-6: what you said / the answer (word-wrapped, 21 characters per line)
    if (text.empty()) {
        esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta == nullptr) sta = esp_netif_get_default_netif();
        esp_netif_ip_info_t ip;
        if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0) {
            snprintf(line, sizeof(line), IPSTR, IP2STR(&ip.ip));
            Text(0, 4, "Phone page:");
            Text(0, 5, std::string("http://") + line);
        } else {
            Text(0, 4, "Waiting for Wi-Fi...");
        }
    }
    std::string head = role == "user" ? "You: " : (role.empty() ? "" : "M'DEE: ");
    std::string full = head + text;
    int row = 3;
    size_t pos = 0;
    while (pos < full.size() && row <= 6) {
        size_t len = std::min<size_t>(21, full.size() - pos);
        if (pos + len < full.size()) {
            size_t sp = full.rfind(' ', pos + len);
            if (sp != std::string::npos && sp > pos) len = sp - pos;
        }
        Text(0, row++, full.substr(pos, len));
        pos += len;
        while (pos < full.size() && full[pos] == ' ') pos++;
    }
    if (pos < full.size() && row > 6) Text(18, 6, "...");
    Flush();
}
