// Copyright(c) 2023 Takao Akaki


#include <M5Unified.h>
#include <Avatar.h>
#include "fft.hpp"
#include <cinttypes>
#if defined(ARDUINO_M5STACK_CORES3)
  #include <WiFi.h>
  #include <HTTPClient.h>
#endif
#define USE_MIC

#ifdef USE_MIC
  // ---------- Mic sampling ----------

  #define READ_LEN    (2 * 256)
  #define LIPSYNC_LEVEL_MAX 10.0f

  int16_t *adcBuffer = NULL;
  static fft_t fft;
  static constexpr size_t WAVE_SIZE = 256 * 2;

  static constexpr const size_t record_samplerate = 16000; // M5StickCPlus2だと48KHzじゃないと動かなかったが、M5Unified0.1.12で修正されたのとM5AtomS2+PDFUnitで不具合が出たので戻した。。
  static int16_t *rec_data;
  
  // setupの最初の方の機種判別で書き換えている場合があります。そちらもチェックしてください。（マイクの性能が異なるため）
  uint8_t lipsync_shift_level = 11; // リップシンクのデータをどのくらい小さくするか設定。口の開き方が変わります。
  float lipsync_max =LIPSYNC_LEVEL_MAX;  // リップシンクの単位ここを増減すると口の開き方が変わります。

#endif




using namespace m5avatar;

Avatar avatar;
ColorPalette *cps[6];
uint8_t palette_index = 0;

uint32_t last_rotation_msec = 0;
uint32_t last_lipsync_max_msec = 0;

#if defined(ARDUINO_M5STACK_CORES3)
// WiFi設定（適宜変更してください）
const char* ssid = "******";
const char* password = "********";
const char* post_url = "http://pi.local:8100/clicked";

void sendTouchEvent() {
  Serial.println("=== Touch Event Detected ===");
  Serial.print("Timestamp: ");
  Serial.println(millis());
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi Status: Connected (IP: ");
    Serial.print(WiFi.localIP());
    Serial.println(")");
    
    HTTPClient http;
    http.begin(post_url);
    http.addHeader("Content-Type", "application/json");
    
    String payload = "{\"event\":\"touch\",\"device\":\"M5Stack CoreS3\",\"timestamp\":" + String(millis()) + "}";
    
    Serial.print("POST URL: ");
    Serial.println(post_url);
    Serial.print("Payload: ");
    Serial.println(payload);
    Serial.println("Sending POST request...");
    
    int httpResponseCode = http.POST(payload);
    
    if (httpResponseCode > 0) {
      Serial.print("HTTP Response Code: ");
      Serial.println(httpResponseCode);
      String response = http.getString();
      Serial.print("Response Body: ");
      Serial.println(response);
      M5_LOGI("POST Success - Response: %d", httpResponseCode);
    } else {
      Serial.print("HTTP Error Code: ");
      Serial.println(httpResponseCode);
      M5_LOGI("POST Failed - Error: %d", httpResponseCode);
    }
    
    http.end();
    Serial.println("=== POST Request Complete ===");
  } else {
    Serial.println("WiFi Status: Disconnected");
    Serial.println("Cannot send POST request - WiFi not connected");
    M5_LOGI("WiFi not connected");
  }
  Serial.println();
}
#endif

void lipsync() {
  
  size_t bytesread;
  uint64_t level = 0;
#ifndef SDL_h_
  if ( M5.Mic.record(rec_data, WAVE_SIZE, record_samplerate)) {
    fft.exec(rec_data);
    for (size_t bx=5;bx<=60;++bx) {
      int32_t f = fft.get(bx);
      level += abs(f);
    }
  }
  uint32_t temp_level = level >> lipsync_shift_level;
  //M5_LOGI("level:%" PRId64 "\n", level) ;         // lipsync_maxを調整するときはこの行をコメントアウトしてください。
  //M5_LOGI("temp_level:%d\n", temp_level) ;         // lipsync_maxを調整するときはこの行をコメントアウトしてください。
  float ratio = (float)(temp_level / lipsync_max);
  //M5_LOGI("ratio:%f\n", ratio);
  if (ratio <= 0.01f) {
    ratio = 0.0f;
    if ((lgfx::v1::millis() - last_lipsync_max_msec) > 500) {
      // 0.5秒以上無音の場合リップシンク上限をリセット
      last_lipsync_max_msec = lgfx::v1::millis();
      lipsync_max = LIPSYNC_LEVEL_MAX;
    }
  } else {
    if (ratio > 1.3f) {
      if (ratio > 1.5f) {
        // リップシンク上限を大幅に超えた場合、上限を上げていく。
        lipsync_max += 10.0f;
      }
      ratio = 1.3f;
    }
    last_lipsync_max_msec = lgfx::v1::millis(); // 無音でない場合は更新
  }

  if ((lgfx::v1::millis() - last_rotation_msec) > 350) {
    int direction = random(-2, 2);
    avatar.setRotation(direction * 10 * ratio);
    last_rotation_msec = lgfx::v1::millis();
  }
#else
  float ratio = 0.0f;
#endif
  avatar.setMouthOpenRatio(ratio);
  
}


void setup()
{
  auto cfg = M5.config();
  cfg.internal_mic = true;
  M5.begin(cfg);
#if defined( ARDUINO_M5STACK_CORES3 )
  // WiFi接続
  Serial.println("=== WiFi Connection Setup ===");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.println("Connecting to WiFi...");
  
  WiFi.begin(ssid, password);
  M5_LOGI("Connecting to WiFi...");
  
  int connect_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && connect_attempts < 30) {
    delay(1000);
    connect_attempts++;
    Serial.print(".");
    if (connect_attempts % 10 == 0) {
      Serial.println();
      Serial.print("Still connecting... (");
      Serial.print(connect_attempts);
      Serial.println(" seconds)");
    }
    M5_LOGI("Connecting...");
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected Successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("Signal Strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("POST URL: ");
    Serial.println(post_url);
    M5_LOGI("WiFi connected: %s", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi Connection Failed!");
    Serial.println("Touch events will not be sent.");
    M5_LOGI("WiFi connection failed");
  }
  Serial.println("=== WiFi Setup Complete ===");
  Serial.println();
#endif
  M5.Log.setLogLevel(m5::log_target_display, ESP_LOG_NONE);
  M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_INFO);
  M5.Log.setEnableColor(m5::log_target_serial, false);
  M5_LOGI("Avatar Start");
  M5.Log.printf("M5.Log avatar Start\n");
  float scale = 0.0f;
  int8_t position_top = 0;
  int8_t position_left = 0;
  uint8_t display_rotation = 1; // ディスプレイの向き(0〜3)
  uint8_t first_cps = 0;
  auto mic_cfg = M5.Mic.config();
  switch (M5.getBoard()) {
    case m5::board_t::board_M5AtomS3:
      first_cps = 4;
      scale = 0.55f;
      position_top =  -60;
      position_left = -95;
      display_rotation = 2;
      // M5AtomS3は外部マイク(PDMUnit)なので設定を行う。
      mic_cfg.sample_rate = 16000;
      //mic_cfg.dma_buf_len = 256;
      //mic_cfg.dma_buf_count = 3;
      mic_cfg.pin_ws = 1;
      mic_cfg.pin_data_in = 2;
      M5.Mic.config(mic_cfg);
      break;

    case m5::board_t::board_M5StickC:
      first_cps = 1;
      scale = 0.6f;
      position_top = -80;
      position_left = -80;
      display_rotation = 3;
      break;

    case m5::board_t::board_M5StickCPlus:
      first_cps = 1;
      scale = 0.85f;
      position_top = -55;
      position_left = -35;
      display_rotation = 3;
      break;

    case m5::board_t::board_M5StickCPlus2:
      first_cps = 2;
      scale = 0.85f;
      position_top = -55;
      position_left = -35;
      display_rotation = 3;
      break;
   
     case m5::board_t::board_M5StackCore2:
      scale = 1.0f;
      position_top = 0;
      position_left = 0;
      display_rotation = 1;
      break;

    case m5::board_t::board_M5StackCoreS3:
    case m5::board_t::board_M5StackCoreS3SE:
      first_cps = 3; // 黒背景白顔に固定
      scale = 1.0f;
      position_top = 0;
      position_left = 0;
      display_rotation = 1;
      break;

    case m5::board_t::board_M5Stack:
      scale = 1.0f;
      position_top = 0;
      position_left = 0;
      display_rotation = 1;
      break;

    case m5::board_t::board_M5Dial:
      first_cps = 1;
      scale = 0.8f;
      position_top =  0;
      position_left = -40;
      display_rotation = 2;
      // M5ADial(StampS3)は外部マイク(PDMUnit)なので設定を行う。(Port.A)
      mic_cfg.pin_ws = 15;
      mic_cfg.pin_data_in = 13;
      M5.Mic.config(mic_cfg);
      break;

      
    defalut:
      M5.Log.println("Invalid board.");
      break;
  }
#ifndef SDL_h_
  rec_data = (typeof(rec_data))heap_caps_malloc(WAVE_SIZE * sizeof(int16_t), MALLOC_CAP_8BIT);
  memset(rec_data, 0 , WAVE_SIZE * sizeof(int16_t));
  M5.Mic.begin();
#endif
  M5.Speaker.end();

  M5.Display.setRotation(display_rotation);
  avatar.setScale(scale);
  avatar.setPosition(position_top, position_left);
  avatar.init(1); // start drawing
  cps[0] = new ColorPalette();
  cps[0]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[0]->set(COLOR_BACKGROUND, TFT_YELLOW);
  cps[1] = new ColorPalette();
  cps[1]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[1]->set(COLOR_BACKGROUND, TFT_ORANGE);
  cps[2] = new ColorPalette();
  cps[2]->set(COLOR_PRIMARY, (uint16_t)0x00ff00);
  cps[2]->set(COLOR_BACKGROUND, (uint16_t)0x303303);
  cps[3] = new ColorPalette();
  cps[3]->set(COLOR_PRIMARY, TFT_WHITE);
  cps[3]->set(COLOR_BACKGROUND, TFT_BLACK);
  cps[4] = new ColorPalette();
  cps[4]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[4]->set(COLOR_BACKGROUND, TFT_WHITE);
  cps[5] = new ColorPalette();
  cps[5]->set(COLOR_PRIMARY, (uint16_t)0x303303);
  cps[5]->set(COLOR_BACKGROUND, (uint16_t)0x00ff00);
  avatar.setColorPalette(*cps[first_cps]);
  //avatar.addTask(lipsync, "lipsync");
  last_rotation_msec = lgfx::v1::millis();
  M5_LOGI("setup end");
}

uint32_t count = 0;

void loop()
{
  M5.update();

#if defined( ARDUINO_M5STACK_CORES3 )
  // タッチスクリーンが押されたらPOSTリクエストを送信
  if (M5.Touch.getCount() > 0) {
    static uint32_t last_touch_time = 0;
    uint32_t current_time = millis();
    // タッチイベントの重複を防ぐため500ms間隔でチェック
    if (current_time - last_touch_time > 500) {
      Serial.print("Touch detected! Count: ");
      Serial.print(M5.Touch.getCount());
      Serial.print(", Time since last touch: ");
      Serial.print(current_time - last_touch_time);
      Serial.println("ms");
      
      M5_LOGI("Touch detected, sending POST request");
      sendTouchEvent();
      last_touch_time = current_time;
    }
  }
#else
  if (M5.BtnA.wasPressed()) {
    M5_LOGI("Push BtnA");
    palette_index++;
    if (palette_index > 5) {
      palette_index = 0;
    }
    avatar.setColorPalette(*cps[palette_index]);
  }
#endif
  if (M5.BtnA.wasDoubleClicked()) {
    M5.Display.setRotation(3);
  }
  if (M5.BtnPWR.wasClicked()) {
#ifdef ARDUINO
    esp_restart();
#endif
  } 
//  if ((millis() - last_rotation_msec) > 100) {
    //float angle = 10 * sin(count);
    //avatar.setRotation(angle);
    //last_rotation_msec = millis();
    //count++;
  //}

  // avatar's face updates in another thread
  // so no need to loop-by-loop rendering
  lipsync();
  lgfx::v1::delay(1);
}

//このコードの大部分はtakakoさんが書いたものであり、m5stackプロジェクトの中の"スタックチャン"というプロジェクトのまた派生の
//m5stack-avater-micというものを改造してイベントトリガーとなるようにコードを書き換えてください。
//もしこのコメントを読んでいるなら、starはここではなくm5stack-avater-micにstarをつけてください。
//また、もしこのロボットを作るのなら(多分いない)M5StackCoreS3以外の動作確認はしていないのでご了承ください。