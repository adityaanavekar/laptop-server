#include "esp_camera.h"     // ESP32 Camera library
#include <WiFi.h>           // Wi-Fi library
#include <HTTPClient.h>     // HTTP client
#include <Preferences.h>    // NVS for boot flag


// Wi-Fi credentials (update these)
const char* ssid = "POCO M4 Pro";
const char* password = "adityaa18";

// Server details (update server_ip to your laptop's Wi-Fi IP)
const char* server_ip = "10.220.83.223";  // Laptop/server IP (e.g., from ipconfig)
const int server_port = 5000;
const char* post_endpoint = "/upload";

// Flash LED pin
#define FLASH_LED_PIN 4

// NVS for boot flag
Preferences preferences;

// Camera configuration (optimized for stability, no DMA overflow)
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SXGA;    // 800x600 (stable; prevents DMA overflow; change to FRAMESIZE_VGA if needed)
  config.jpeg_quality = 10;              // 0-63 (higher = smaller file, less load)
  config.fb_count = 1;                   // 2 buffers for DMA handling
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.xclk_freq_hz = 20000000;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  // AI-Thinker pinout
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sccb_sda = 26;
  config.pin_sccb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    while (1) delay(500);
  }

  // Fix vertical flip (no horizontal mirror)
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);   // Enable vertical flip to correct orientation
    s->set_hmirror(s, 1); // No horizontal mirror
    
    // 💡 Image enhancement settings
    s->set_brightness(s, 2);
    s->set_contrast(s, 2);
    s->set_saturation(s, 1);
    s->set_sharpness(s, 2);    // -2 to +2
    s->set_denoise(s, 1);      // 0 = disable, 1 = enable
    s->set_whitebal(s, 1);     // Enable auto white balance
    s->set_awb_gain(s, 1);     // Enable AWB gain
    s->set_exposure_ctrl(s, 1); // Enable auto exposure
    s->set_gain_ctrl(s, 1);     // Enable auto gain
    s->set_aec2(s, 1);            // Advanced exposure control (longer exposure)
    s->set_ae_level(s, 1);        // -2 to +2; 1 brightens a bit
    s->set_gainceiling(s, GAINCEILING_128X); // Allow high ISO gain
    s->set_lenc(s, 1);         // Lens correction
    s->set_wb_mode(s, 0);      // 0 = Auto, can set 1=Sunny, 2=Cloudy, etc.

    // Optional: special effects for testing
    s->set_special_effect(s, 2); // 0=Normal, 1=Negative, 2=Grayscale, etc.

    Serial.println("Camera sensor configured with enhanced image settings");
  }

  delay(100);  // Stabilize
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("ESP32-CAM starting...");

  // PSRAM check (for debugging)
  Serial.printf("PSRAM: %d bytes\n", ESP.getPsramSize());

  // Init flash LED (off)
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected! IP: " + WiFi.localIP().toString());

  // NVS boot flag (skip first power-on)
  preferences.begin("boot_flag", false);
  // Optional: preferences.clear();  // Uncomment to reset flag for testing
  int boot_count = preferences.getInt("count", 0);
  boot_count++;
  preferences.putInt("count", boot_count);
  preferences.end();
  Serial.printf("Boot count: %d\n", boot_count);

  // Initialize camera
  setupCamera();

  // Capture/send only if not first boot
  if (boot_count >= 1) {
    Serial.println("Capturing (RESET triggered)...");
    digitalWrite(FLASH_LED_PIN, HIGH);  // Flash ON
    delay(900);

   //  Lock exposure before capture
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_exposure_ctrl(s, 0);  // Lock the current exposure
  }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Capture failed");
      digitalWrite(FLASH_LED_PIN, LOW);
      esp_camera_deinit();
      return;
    }
    Serial.printf("Capture OK! Size: %u bytes\n", fb->len);

    delay(450);
    digitalWrite(FLASH_LED_PIN, LOW);  // Flash OFF

    // Send to server
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      String url = "http://" + String(server_ip) + ":" + String(server_port) + String(post_endpoint);
      http.begin(url);
      http.addHeader("Content-Type", "image/jpeg");

      Serial.println("Sending image...");
      int code = http.POST(fb->buf, fb->len);
      if (code > 0) {
        Serial.printf("Send OK! Code: %d\n", code);
      } else {
        Serial.printf("Send failed: %s (Code: %d)\n", http.errorToString(code).c_str(), code);
      }
      http.end();
    } else {
      Serial.println("Wi-Fi lost; can't send");
    }

    esp_camera_fb_return(fb);
    delay(100);
  } else {
    Serial.println("First boot - skipping capture (press RESET to start)");
  }

  // Deinit camera (stops DMA, prevents overflow)
  esp_camera_deinit();
  Serial.println("Camera deinit. Setup complete.");

  // Optional deep sleep (uncomment; wakes on RESET/power cycle)
  // esp_deep_sleep_start();
}

void loop() {
  // Idle - no camera/DMA activity
  Serial.println("Idling... (RESET to capture)");
  delay(10000);  // Prevent watchdog
}