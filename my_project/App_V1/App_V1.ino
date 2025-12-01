#include <Arduino.h>
#include <esp_display_panel.hpp>

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include <lvgl.h>
#include "lvgl_v8_port.h"
//#include <demos/lv_demos.h>

//LV_IMG_DECLARE(Desktop_5);

using namespace esp_panel::drivers;
using namespace esp_panel::board;

const char* ssid = "SSID";
const char* password = "PASS";
const String apiKey = "API_KEY";

lv_obj_t *label_temp;
lv_obj_t *label_pressure;
lv_obj_t *label_humidity;

// obiekty zegara
lv_obj_t *clock_container;
lv_obj_t *label_clock;

/**
 * To use the built-in examples and demos of LVGL uncomment the includes below respectively.
 */
 // #include <demos/lv_demos.h>
 // #include <examples/lv_examples.h>

void fetchWeather(lv_obj_t *label_czas) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.openweathermap.org/data/2.5/weather?q=Zdunska%20Wola,PL&appid=" + apiKey + "&units=metric&lang=pl";
    http.begin(url);

    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();

      StaticJsonDocument<1024> doc;
      deserializeJson(doc, payload);

      float temp = doc["main"]["temp"];
      int pressure = doc["main"]["pressure"];
      int humidity = doc["main"]["humidity"];

      char buf[128];
      sprintf(buf, "Temp: %.1f°C\nWilg.: %d%%\nCisn.: %dhPa", temp, humidity, pressure);
      lv_label_set_text(label_temp, buf);

      // aktualny czas z NTP
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        char czas_buf[32];
        strftime(czas_buf, sizeof(czas_buf), "Aktualizacja: %H:%M", &timeinfo);
        lv_label_set_text(label_czas, czas_buf);
      } else {
        lv_label_set_text(label_czas, "Aktualizacja: brak czasu");
      }

    } else {
      Serial.println("Blad pobierania danych");
    }
    http.end();
  } else {
    lv_label_set_text(label_temp, "Brak polaczenia WiFi");
    lv_label_set_text(label_czas, "Aktualizacja: --:--");
  }
}


void setup()
{
    String title = "LVGL porting example";

    Serial.begin(115200);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");

    Serial.println("Initializing board");
    Board *board = new Board();
    board->init();

    #if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    // When avoid tearing function is enabled, the frame buffer number should be set in the board driver
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    /**
     * As the anti-tearing feature typically consumes more PSRAM bandwidth, for the ESP32-S3, we need to utilize the
     * "bounce buffer" functionality to enhance the RGB data bandwidth.
     * This feature will consume `bounce_buffer_size * bytes_per_pixel * 2` of SRAM memory.
     */
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif
    assert(board->begin());

    Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());

    Serial.println("Creating UI");
    /* Lock the mutex due to the LVGL APIs are not thread-safe */
    lvgl_port_lock(-1);

    //ustaw tło (np. ciemne)
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x222222), 0);  // ciemne tło

    // lv_obj_t *bg_img = lv_img_create(lv_scr_act());
    // lv_img_set_src(bg_img, &my_background);
    // lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0); // lub użyj lv_obj_center(bg_img);
    // lv_obj_move_background(bg_img);  // przenosi tło za inne obiekty

    // LV_IMG_DECLARE(Desktop_1);
    // lv_image_set_src(img, &Desktop_1);

    // lv_obj_t *img = lv_img_create(lv_scr_act());
    // lv_img_set_src(img, &Desktop_5);
    // lv_obj_center(img);  // opcjonalnie, wyśrodkuj

     /**
     * Create the simple labels
     */
    // lv_obj_t *label_1 = lv_label_create(lv_scr_act());
    // lv_label_set_text(label_1, "Hello World!");
    // lv_obj_set_style_text_font(label_1, &lv_font_montserrat_30, 0);
    // lv_obj_align(label_1, LV_ALIGN_CENTER, 0, -20);

    // lv_obj_t *label_2 = lv_label_create(lv_scr_act());
    // lv_label_set_text_fmt(
    //     label_2, "ESP32_Display_Panel (%d.%d.%d)",
    //     ESP_PANEL_VERSION_MAJOR, ESP_PANEL_VERSION_MINOR, ESP_PANEL_VERSION_PATCH
    // );
    // lv_obj_set_style_text_font(label_2, &lv_font_montserrat_16, 0);
    // lv_obj_align_to(label_2, label_1, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // lv_obj_t *label_3 = lv_label_create(lv_scr_act());
    // lv_label_set_text_fmt(label_3, "LVGL (%d.%d.%d)", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    // lv_obj_set_style_text_font(label_3, &lv_font_montserrat_16, 0);
    // lv_obj_align_to(label_3, label_2, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
  

    // Tworzymy kontener
    lv_obj_t *container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(container, 240, 200);
    lv_obj_center(container);
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, 10, 0); // odstępy pionowe
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0); // przezroczyste tło

    // Etykieta z nazwą miasta
    lv_obj_t *label_miasto = lv_label_create(container);
    lv_label_set_text(label_miasto, "Zdunska Wola");
    lv_obj_set_style_text_align(label_miasto, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label_miasto, lv_color_white(), 0);

    // Etykieta temperatury, wilgotności i ciśnienia
    label_temp = lv_label_create(container);
    lv_label_set_text(label_temp, "Pobieranie...");
    lv_obj_set_style_text_align(label_temp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label_temp, lv_color_white(), 0);

    // Etykieta z godziną
    lv_obj_t *label_czas = lv_label_create(container);
    lv_obj_set_style_text_align(label_czas, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label_czas, "Aktualizacja: --:--");
    lv_obj_set_style_text_color(label_czas, lv_color_white(), 0);

    // Przycisk
    lv_obj_t *btn = lv_btn_create(container);
    lv_obj_set_size(btn, 120, 40);
    lv_obj_center(btn); // lub zostaw bez centrowania — Flex ułoży automatycznie

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Odswiez");
    lv_obj_center(btn_label);

    // Obsługa kliknięcia
    lv_obj_add_event_cb(btn, [](lv_event_t * e){
        lv_obj_t *czas_lbl = (lv_obj_t *)lv_event_get_user_data(e);
        fetchWeather(czas_lbl);
    }, LV_EVENT_CLICKED, label_czas);

    fetchWeather(label_czas); // przekazujemy wskaźnik labela

    // Kontener zegara
    clock_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(clock_container, 150, 50);
    lv_obj_align(clock_container, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(clock_container, lv_color_hex(0x333333), 0); // ciemne tło
    lv_obj_set_style_radius(clock_container, 10, 0); // zaokrąglone rogi
    lv_obj_set_style_bg_opa(clock_container, LV_OPA_80, 0); // półprzezroczystość
    lv_obj_set_style_border_width(clock_container, 0, 0); // bez ramki

    // Etykieta zegara
    label_clock = lv_label_create(clock_container);
    lv_obj_center(label_clock);
    lv_obj_set_style_text_color(label_clock, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_clock, &lv_font_montserrat_16, 0);
    lv_label_set_text(label_clock, "--:--:--\n--.--.----");

    /**
     * Try an example. Don't forget to uncomment header.
     * See all the examples online: https://docs.lvgl.io/master/examples.html
     * source codes: https://github.com/lvgl/lvgl/tree/e7f88efa5853128bf871dde335c0ca8da9eb7731/examples
     */
    //  lv_example_btn_1();

    /**
     * Or try out a demo.
     * Don't forget to uncomment header and enable the demos in `lv_conf.h`. E.g. `LV_USE_DEMO_WIDGETS`
     */
    // lv_demo_widgets();
    // lv_demo_benchmark();
    // lv_demo_music();
    // lv_demo_stress();

    /* Release the mutex */
    lvgl_port_unlock();
}

void loop() {
  
    // static unsigned long lastUpdate = 0;
    // static bool blink = true;  // do animacji dwukropka

    // if (millis() - lastUpdate > 1000) {
    // lastUpdate = millis();

    // struct tm timeinfo;
    // if (getLocalTime(&timeinfo)) {
    //     char buf[32];
    //     strftime(buf, sizeof(buf), "%H:%M:%S\n%d.%m.%Y", &timeinfo);
    //     lv_label_set_text(label_clock, buf);
    // }
//}

    delay(30); // mniejszy delay = lepszy LVGL
}