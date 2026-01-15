/////////////////////////////////////////////////////
// Core Arduino API, needed for setup(), loop(), Serial, delay(), etc.
#include <Arduino.h>
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
// Espressif hardware abstracion layer
// Needed for LCD, RGB / SPI buses, touch controllers, backlight
// Provides Board, LCD, BusRGB
#include <esp_display_panel.hpp>
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
// Application layer
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h> // NTP time + struct tm
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
// GUI engine
#include <lvgl.h>
// Glue layer
#include "lvgl_v8_port.h"
/////////////////////////////////////////////////////

//LV_IMG_DECLARE(Desktop_5); // Declare an image compiled into firmware

/////////////////////////////////////////////////////
// Namespaces
using namespace esp_panel::drivers;
using namespace esp_panel::board;
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
// Wi-Fi credentials
//const char* ssid = "SSID";
//const char* password = "PASS";
//const String apiKey = "API_KEY";
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
static bool btn_styles_initialized = false;
static bool clk_styles_initialized = false;
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
// Button styles definitions
static lv_style_t style_btn;
static lv_style_t style_button_pressed;
static lv_style_t style_button_red;
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
// Clock styles definitions
static lv_style_t style_clk;
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
// Global handles to LVGL objects
// Weather
lv_obj_t *label_temp;
lv_obj_t *label_pressure;
lv_obj_t *label_humidity;

// Clock
lv_obj_t *clock_container;
lv_obj_t *label_clock;
/////////////////////////////////////////////////////

static float temperature = 25.5; // sensor readout
static float g_temperature; // global temperature
static SemaphoreHandle_t temp_mutex;

static float humidity = 50.0; // sensor readout
static float g_humidity; // global humidity
static SemaphoreHandle_t humidity_mutex;

static float pressure = 1020.0; // sensor readout
static float g_pressure; // global temperature
static SemaphoreHandle_t pressure_mutex;

// Weather fetching and updates UI
/*void fetchWeather(lv_obj_t *label_czas) {
  if (WiFi.status() == WL_CONNECTED) { // Only if Wi-Fi is connected
    HTTPClient http; // Creates an HTTP client instance
    String url = "https://api.openweathermap.org/data/2.5/weather?q=Zdunska%20Wola,PL&appid=" + apiKey + "&units=metric&lang=pl";
    http.begin(url);

    int httpCode = http.GET(); // Gets an http status code
    if (httpCode > 0) { // If http request is succesful
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
}*/


void clock_timer_cb(lv_timer_t *t)
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S\n%d.%m.%Y", &timeinfo);

    lv_label_set_text(label_clock, buf);
}

/////////////////////////////////////////////////////
// LVGL timer callback
// Runs inside the LVGL task and after LVGL initialization
// Container for creating UI objects
void ui_init_cb(lv_timer_t *t)
{
  Serial.println("ui_init_cb is running"); // debugging message

  //lv_obj_t *scr = lv_scr_act(); // Gets the current active screen
  //if (!scr) { // When the screen does not exist, prevents NULL dereference
    //Serial.println("scr == NULL");
    //return;
    //}
    
    //lv_obj_set_style_bg_color(
      //scr,
      //lv_color_hex(0x202020),
      //LV_PART_MAIN
    //);
    

    createAppUi();

    lv_timer_create(clock_timer_cb, 1000, NULL);

    


    lv_timer_del(t);
}
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
void lcdTask(void *arg)
{
  delay(500);   // for Arduino & interrupts
  Board *board = (Board *)arg;
  
  Serial.println("Starting LCD after delay"); // for debugging
  assert(board->begin());auto lcd = board->getLCD();
  
  /* FORCE single framebuffer for RGB */
  lcd->configFrameBufferNumber(1);
  
  /* OPTIONAL but recommended on S3 RGB */
  #if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB
    auto bus = lcd->getBus();
    if (bus && bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB){
      static_cast<esp_panel::drivers::BusRGB *>(bus)
      ->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
      }
  #endif


    Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());
    Serial.println("lvgl_port_init returned");


    // Schedule UI creation INSIDE LVGL task
    // lv_timer_create(timer_cb, period_ms, user_data)
    lv_timer_create(ui_init_cb, 10, NULL);
    lv_timer_create(temperatureTimer, 1000, NULL);
    lv_timer_create(humidityTimer, 1000, NULL);
    lv_timer_create(pressureTimer, 1000, NULL);



    vTaskDelete(NULL);
}
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
// Style helper functions
static lv_color_t darken(const lv_color_filter_dsc_t * dsc, lv_color_t color, lv_opa_t opa)
{
    LV_UNUSED(dsc);
    return lv_color_darken(color, opa);
}
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
// Style initialization
static void button_style_init(void)
{
  // To avoid buttons re-initiallization each time the UI is created
  if (btn_styles_initialized)
    return;
  btn_styles_initialized = true;
    /*Create a simple button style*/
    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 10);
    lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
    lv_style_set_bg_color(&style_btn, lv_palette_lighten(LV_PALETTE_GREY, 3));
    lv_style_set_bg_grad_color(&style_btn, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_VER);

    lv_style_set_border_color(&style_btn, lv_color_black());
    lv_style_set_border_opa(&style_btn, LV_OPA_20);
    lv_style_set_border_width(&style_btn, 2);

    lv_style_set_text_color(&style_btn, lv_color_black());

    /*Create a style for the pressed state.
     *Use a color filter to simply modify all colors in this state*/
    static lv_color_filter_dsc_t color_filter;
    lv_color_filter_dsc_init(&color_filter, darken);
    lv_style_init(&style_button_pressed);
    lv_style_set_color_filter_dsc(&style_button_pressed, &color_filter);
    lv_style_set_color_filter_opa(&style_button_pressed, LV_OPA_20);

    /*Create a red style. Change only some colors.*/
    lv_style_init(&style_button_red);
    lv_style_set_bg_color(&style_button_red, lv_palette_main(LV_PALETTE_RED));
    lv_style_set_bg_grad_color(&style_button_red, lv_palette_lighten(LV_PALETTE_RED, 3));
}
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
// Style initialization
static void clock_style_init(void)
{
  // To avoid clock re-initiallization each time the UI is created
  if (clk_styles_initialized)
    return;
  clk_styles_initialized = true;
    /*Create a simple clock style*/
    lv_style_init(&style_clk);
    lv_style_set_bg_color(&style_clk, lv_color_hex(0x333333)); // ciemne tło
    lv_style_set_bg_opa(&style_clk, LV_OPA_80); // półprzezroczystość
    lv_style_set_radius(&style_clk, 10); // zaokrąglone rogi
    lv_style_set_border_width(&style_clk, 0); // bez ramki
}
/////////////////////////////////////////////////////


/////////////////////////////////////////////////////
void createControlButtons(lv_obj_t *parent)
{
  /*Initialize the style*/
  button_style_init();

  /*Create a button and use the new styles*/
  lv_obj_t *btn_current_info = lv_btn_create(parent);
  /* Remove the styles coming from the theme
   * Note that size and position are also stored as style properties
   * so lv_obj_remove_style_all will remove the set size and position too */
  lv_obj_remove_style_all(btn_current_info);
  lv_obj_set_size(btn_current_info, 160, 50);
  lv_obj_add_style(btn_current_info, &style_btn, 0);
  lv_obj_add_style(btn_current_info, &style_button_pressed, LV_STATE_PRESSED);

  /*Add a label to the button*/
  lv_obj_t *current_info = lv_label_create(btn_current_info);
  lv_label_set_text(current_info, "Increase temp");
  lv_obj_center(current_info);

  lv_obj_add_event_cb(btn_current_info, btn_event_cb, LV_EVENT_CLICKED, NULL);
}
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
void createClockWidget(lv_obj_t *parent)
{
  /*Initialize the style*/
  clock_style_init();

  // Create clock container
  clock_container = lv_obj_create(parent);
  lv_obj_remove_style_all(clock_container);
  lv_obj_add_style(clock_container, &style_clk, 0);

  lv_obj_set_size(clock_container, 180, 70);
  lv_obj_align(clock_container, LV_ALIGN_BOTTOM_RIGHT, -10, -10);

  // Create clock label
  label_clock = lv_label_create(clock_container);
  lv_label_set_text(label_clock, "--:--:--\n--.--.----");

  lv_obj_set_style_text_color(label_clock, lv_color_white(), 0);
  lv_obj_set_style_text_align(label_clock, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label_clock);
}
/////////////////////////////////////////////////////


/////////////////////////////////////////////////////
// Button event callback
static void btn_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *btn = lv_event_get_target(e);

  if (code == LV_EVENT_CLICKED) {
    Serial.println("Button clicked");

    // Example: change global temperature
    xSemaphoreTake(temp_mutex, portMAX_DELAY);
    g_temperature += 0.5f;
    xSemaphoreGive(temp_mutex);
  }
}
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
void createAppUi()
{
      lv_obj_t *scr = lv_obj_create(lv_scr_act());

      lv_obj_set_style_bg_color(
      scr,
      lv_color_hex(0x202020),
      LV_PART_MAIN
    );

    lv_obj_set_size(scr, lv_pct(50), lv_pct(60));
    lv_obj_center(scr);
    lv_obj_set_layout(scr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);

    label_temp = lv_label_create(scr);
    lv_label_set_text(label_temp, "Pobieranie temperatury...");
    lv_obj_set_style_text_color(label_temp, lv_color_white(), 0);

    label_humidity = lv_label_create(scr);
    lv_label_set_text(label_humidity, "Pobieranie wilgotnosci...");
    lv_obj_set_style_text_color(label_humidity, lv_color_white(), 0);

    label_pressure = lv_label_create(scr);
    lv_label_set_text(label_pressure, "Pobieranie cisnienia...");
    lv_obj_set_style_text_color(label_pressure, lv_color_white(), 0);

    //label_clock = lv_label_create(scr);
    //lv_label_set_text(label_clock, "--:--:--");
    //lv_obj_set_style_text_color(label_clock, lv_color_white(), 0);

    createClockWidget(scr);


    createControlButtons(scr);

}
/////////////////////////////////////////////////////



/////////////////////////////////////////////////////
void temperatureTask(void *arg)
{
  float local_temp = temperature;
  for(;;)
  {
    float new_temp_readout = local_temp;
    local_temp += 0.1f;

    xSemaphoreTake(temp_mutex, portMAX_DELAY); // invoke
    g_temperature = local_temp;
    xSemaphoreGive(temp_mutex); // release

    vTaskDelay(pdMS_TO_TICKS(2000)); // readout every 2 seconds
  }
}
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
void humidityTask(void *arg)
{
  for(;;)
  {
    float new_humidity_readout = humidity;

    xSemaphoreTake(humidity_mutex, portMAX_DELAY); // invoke
    g_humidity = new_humidity_readout;
    xSemaphoreGive(humidity_mutex); // release

    vTaskDelay(pdMS_TO_TICKS(2000)); // readout every 2 seconds
  }
}
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
void pressureTask(void *arg)
{
  for(;;)
  {
    float new_pressure_readout = pressure;

    xSemaphoreTake(pressure_mutex, portMAX_DELAY); // invoke
    g_pressure = new_pressure_readout;
    xSemaphoreGive(pressure_mutex); // release

    vTaskDelay(pdMS_TO_TICKS(2000)); // readout every 2 seconds
  }
}
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
//
// Timer for temperature refreshes
// Runs inside the LVGL task
//
void temperatureTimer(lv_timer_t *t)
{
  float temp_copy;

  xSemaphoreTake(temp_mutex, portMAX_DELAY);
  temp_copy = g_temperature;
  xSemaphoreGive(temp_mutex);

  char buff[64];
  snprintf(buff, sizeof(buff), "Temperatura: %.2f C", temp_copy);

  lv_label_set_text(label_temp, buff);
}
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
//
// Timer for humidity refreshes
// Runs inside the LVGL task
//
void humidityTimer(lv_timer_t *t)
{
  float humidity_copy;

  xSemaphoreTake(humidity_mutex, portMAX_DELAY);
  humidity_copy = g_humidity;
  xSemaphoreGive(humidity_mutex);

  char buff[64];
  snprintf(buff, sizeof(buff), "Wilgotnosc: %.2f %%", humidity_copy);

  lv_label_set_text(label_humidity, buff);
}
/////////////////////////////////////////////////////

/////////////////////////////////////////////////////
//
// Timer for pressure task
// Runs inside the LVGL task
//
void pressureTimer(lv_timer_t *t)
{
  float pressure_copy;

  xSemaphoreTake(pressure_mutex, portMAX_DELAY);
  pressure_copy = g_pressure;
  xSemaphoreGive(pressure_mutex);

  char buff[64];
  snprintf(buff, sizeof(buff), "Cisnienie: %.1f hPa", pressure_copy);

  lv_label_set_text(label_pressure, buff);
}
/////////////////////////////////////////////////////


void setup()
{
  Serial.begin(115200);
  delay(200);

  static Board *board = new Board();
  board->init();

  temp_mutex = xSemaphoreCreateMutex();
  humidity_mutex = xSemaphoreCreateMutex();
  pressure_mutex = xSemaphoreCreateMutex();



  // BaseType_t xTaskCreatePinnedToCore(
    // TaskFunction_t pvTaskCode, // pointer to the task entry function (tasks have to never return or be terminate by vTaskDelete())
    // const char *constpcName, // descriptive name for the task
    // const uint32_t usStackDepth, // size of the task stack (number of bytes)
    // void *constpvParameters, // pointer, will be used as a parameter for the created task
    // UBaseType_t uxPriority, // priority of the task, can have privilige at a choosen level
    // TaskHandle_t *constpvCreatedTask, // used to pas back the handle by wich the task can be referenced
    // const BaseType_t xCoreID) // tskNO_AFINITY - not pinned (up to scheduler), 0 or 1 - core
  xTaskCreatePinnedToCore(
    lcdTask, // pointer to the task entry function
    "lcdTask", // name of the task
    8192, // size of the task stack
    board, // pointer, used as a parameter for the created task
    1, // priority 1
    NULL, // handle by which the task can be referenced
    0   // core 0
    );

    xTaskCreatePinnedToCore(
    temperatureTask, // pointer to the task entry function
    "temperatureTask", // name of the task
    4096, // size of the task stack
    board, // pointer, used as a parameter for the created task
    1, // priority 1
    NULL, // handle by which the task can be referenced
    0   // core 0
    );

    
    xTaskCreatePinnedToCore(
    humidityTask, // pointer to the task entry function
    "humidityTask", // name of the task
    4096, // size of the task stack
    board, // pointer, used as a parameter for the created task
    1, // priority 1
    NULL, // handle by which the task can be referenced
    0   // core 0
    );

    
    xTaskCreatePinnedToCore(
    pressureTask, // pointer to the task entry function
    "pressureTask", // name of the task
    4096, // size of the task stack
    board, // pointer, used as a parameter for the created task
    1, // priority 1
    NULL, // handle by which the task can be referenced
    0   // core 0
    );




    //WiFi.begin(ssid, password);
    //while (WiFi.status() != WL_CONNECTED) delay(500); // Blocked until connected
    
    //configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");



   // #if LVGL_PORT_AVOID_TEARING_MODE
   //auto lcd = board->getLCD();
    // When avoid tearing function is enabled, the frame buffer number should be set in the board driver
    //lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
//#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    //auto lcd_bus = lcd->getBus();
    /**
     * As the anti-tearing feature typically consumes more PSRAM bandwidth, for the ESP32-S3, we need to utilize the
     * "bounce buffer" functionality to enhance the RGB data bandwidth.
     * This feature will consume `bounce_buffer_size * bytes_per_pixel * 2` of SRAM memory.
     */
    //if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
     //   static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    //}
//#endif
//#endif
    //assert(board->begin());

    //Serial.println("Initializing LVGL");
    //lvgl_port_init(board->getLCD(), board->getTouch());

    //Serial.println("Creating UI");
    /* Lock the mutex due to the LVGL APIs are not thread-safe */
    //lvgl_port_lock(-1);
    //lv_obj_set_style_bg_color(lv_scr_act(),
                          //lv_color_hex(0x222222),
                          //LV_PART_MAIN);

    //ustaw tło (np. ciemne)
    //lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x222222), 0);  // ciemne tło

    // lv_obj_t *bg_img = lv_img_create(lv_scr_act());
    // lv_img_set_src(bg_img, &my_background);
    // lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0); // lub użyj lv_obj_center(bg_img);
    // lv_obj_move_background(bg_img);  // przenosi tło za inne obiekty

    // LV_IMG_DECLARE(Desktop_1);
    // lv_image_set_src(img, &Desktop_1);

    // lv_obj_t *img = lv_img_create(lv_scr_act());
    // lv_img_set_src(img, &Desktop_5);
    // lv_obj_center(img);  // opcjonalnie, wyśrodkuj


    // lv_obj_t *label_3 = lv_label_create(lv_scr_act());
    // lv_label_set_text_fmt(label_3, "LVGL (%d.%d.%d)", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    // lv_obj_set_style_text_font(label_3, &lv_font_montserrat_16, 0);
    // lv_obj_align_to(label_3, label_2, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
  

    // Tworzymy kontener
    //lv_obj_t *container = lv_obj_create(lv_scr_act());
    //lv_obj_set_size(container, 240, 200);
    //lv_obj_center(container);
    //lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    //lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    //lv_obj_set_style_pad_row(container, 10, 0); // odstępy pionowe
    //lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0); // przezroczyste tło

    // Etykieta z nazwą miasta
    //lv_obj_t *label_miasto = lv_label_create(container);
    //lv_label_set_text(label_miasto, "Zdunska Wola");
    //lv_obj_set_style_text_align(label_miasto, LV_TEXT_ALIGN_CENTER, 0);
    //lv_obj_set_style_text_color(label_miasto, lv_color_white(), 0);

    // Etykieta temperatury, wilgotności i ciśnienia
    //label_temp = lv_label_create(container);
    //lv_label_set_text(label_temp, "Pobieranie...");
    //lv_obj_set_style_text_align(label_temp, LV_TEXT_ALIGN_CENTER, 0);
    //lv_obj_set_style_text_color(label_temp, lv_color_white(), 0);

    // Etykieta z godziną
    //lv_obj_t *label_czas = lv_label_create(container);
    //lv_obj_set_style_text_align(label_czas, LV_TEXT_ALIGN_CENTER, 0);
    //lv_label_set_text(label_czas, "Aktualizacja: --:--");
    //lv_obj_set_style_text_color(label_czas, lv_color_white(), 0);

    // Przycisk
    //lv_obj_t *btn = lv_btn_create(container);
    //lv_obj_set_size(btn, 120, 40);
    //lv_obj_center(btn); // lub zostaw bez centrowania — Flex ułoży automatycznie

    //lv_obj_t *btn_label = lv_label_create(btn);
    //lv_label_set_text(btn_label, "Odswiez");
    //lv_obj_center(btn_label);

    // Obsługa kliknięcia
    //lv_obj_add_event_cb(btn, [](lv_event_t * e){
    //    lv_obj_t *czas_lbl = (lv_obj_t *)lv_event_get_user_data(e);
    //    fetchWeather(czas_lbl);
    //}, LV_EVENT_CLICKED, label_czas);

    //fetchWeather(label_czas); // przekazujemy wskaźnik labela

    // Kontener zegara
    //clock_container = lv_obj_create(lv_scr_act());
    //lv_obj_set_size(clock_container, 150, 50);
    //lv_obj_align(clock_container, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    //lv_obj_set_style_bg_color(clock_container, lv_color_hex(0x333333), 0); // ciemne tło
    //lv_obj_set_style_radius(clock_container, 10, 0); // zaokrąglone rogi
    //lv_obj_set_style_bg_opa(clock_container, LV_OPA_80, 0); // półprzezroczystość
    //lv_obj_set_style_border_width(clock_container, 0, 0); // bez ramki

    // Etykieta zegara
    //label_clock = lv_label_create(clock_container);
    //lv_obj_center(label_clock);
    //lv_obj_set_style_text_color(label_clock, lv_color_white(), 0);
    //lv_obj_set_style_text_font(label_clock, &lv_font_montserrat_16, 0);
    //lv_label_set_text(label_clock, "--:--:--\n--.--.----");

    /**
     * Try an example. Don't forget to uncomment header.
     * See all the examples online: https://docs.lvgl.io/master/examples.html
     * source codes: https://github.com/lvgl/lvgl/tree/e7f88efa5853128bf871dde335c0ca8da9eb7731/examples
     */
    //  lv_example_btn_1();



    /* Release the mutex */
    //lvgl_port_unlock();

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


    //delay(30); // mniejszy delay = lepszy LVGL
    delay(1000);
}