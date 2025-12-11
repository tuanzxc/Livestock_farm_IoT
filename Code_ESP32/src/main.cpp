#include <Arduino.h>

#include "secrets/wifi.h"
#include "wifi_connect.h"
#include <WiFiClientSecure.h>

#include "ca_cert_emqx.h"
#include "secrets/mqtt.h"
#include <PubSubClient.h>
#include "MQTT.h"
#include <Ticker.h>
#include <DHT.h> // Đọc cảm biến ESP32
#include <rgb_lcd.h> // Màn hình 16x2

int remote34 = 0, remote35 = 0, remote34_prev = 4095, remote35_prev = 4095; // Sử dụng 2 bộ nút bấm ADC
float temperature = 0, temp_max = 30, temp_min = 20, humidity = 0; // Nhiệt độ, nhiệt độ tối đa, tối thiểu, độ ẩm
bool fire_ss = 1, auto_fan = 0, auto_fire = 0;// Phát hiện lửa, Tự động điều chỉnh nhiệt độ, Tự động chữa cháy
bool brightness = 0, heater = 0, relay14 = 0, relay25 = 0, relay26 = 0, relay27 = 0;
// Ánh sáng (12), Đèn sưởi(13), Quạt (14), Bơm nước chữa cháy (25), Băng chuyền cho ăn (26), Máy bơm nước uống (27)

namespace
{
    //Wifi
    const char *ssid = WiFiSecrets::ssid;
    const char *password = WiFiSecrets::pass;
    const char *client_id = (String("esp32-client") + WiFi.macAddress()).c_str();

    rgb_lcd lcd; // Màn hình lcd 16x2
    DHT dht(DHT11PIN, DHT11); // Cảm biến nhiệt độ, độ ẩm DHT11
    WiFiClientSecure tlsClient;
    PubSubClient mqttClient(tlsClient);

    Ticker dhtTicker; // 2 seconds interval for DHT22 reading then publising MQTT topics
    const char *temperature_topic = "esp32/temperature"; // Gửi giá trị nhiệt độ
    const char *tempmax_topic = "esp32/tempmax"; // Nhận giá trị nhiệt độ tối đa để bật quạt
    const char *tempmin_topic = "esp32/tempmin"; // Nhận giá trị nhiệt độ tối thiểu để bật đèn sưởi
    const char *firess_topic = "esp32/firess"; // Kiểm tra có cháy không
    const char *LED_brightness_topic = "esp32/LED_brightness"; // Gửi và nhận việc bật/tắt đèn
    const char *relay14_topic = "esp32/relay14"; // Gửi và nhận việc bật/tắt quạt
    const char *relay25_topic = "esp32/relay25"; // Gửi và nhận việc bật/tắt máy bơm chữa cháy, còi báo động
    const char *relay26_topic = "esp32/relay26"; // Gửi và nhận việc bật/tắt băng chuyền cho ăn
    const char *relay27_topic = "esp32/relay27"; // Gửi và nhận việc bật/tắt máy bơm nước uống
    const char *heater_topic = "esp32/heater"; // Gửi và nhận việc bật/tắt đèn sưởi
    const char *autofan_topic = "esp32/autofan"; // Gửi và nhận việc tự động điều chỉnh nhiệt độ bằng quạt hay đèn sưởi, có thể tắt chế độ này
    const char *autofire_topic = "esp32/autofire"; // Gửi và nhận việc tự động chữa cháy, có thể tắt chế độ này
}

const char *subs[] = {
    LED_brightness_topic,
    relay14_topic,
    relay25_topic,
    relay26_topic,
    relay27_topic,
    heater_topic,
    autofan_topic,
    autofire_topic,
    tempmax_topic,
    tempmin_topic
};

void dhtReadPublish() // Xử lý giá trị cảm biến và gửi giá trị cảm biến, trạng thái thiết bị qua MQTT 
{
    fire_ss = digitalRead(firess_PIN); // Đọc cảm biến lửa (0 - có cháy, 1 - không cháy)
    remote34 = analogRead(remote34_PIN); // Kiểm tra nút nào được bấm trong 2 bộ nút bấm
    remote35 = analogRead(remote35_PIN); // 0 - 100: SW1, 600 - 900: SW2, 1600 - 2000: SW3, 2700 - 3100: SW4
    temperature = dht.readTemperature(); // Đọc nhiệt độ từ DHT11
    humidity = dht.readHumidity(); // Đọc độ ẩm từ DHT11
    // Nút SW1 từ remote34 dùng để bật tắt quạt, đồng thời tắt chế độ tự động điều chỉnh nhiệt độ
    if (remote34 >= 0 && remote34 < 100) {
        relay14 = !relay14;
        auto_fan = 0;
        digitalWrite(relay14_PIN, relay14);
        mqttClient.publish(relay14_topic, String(relay14).c_str(), false);
        mqttClient.publish(autofan_topic, String(auto_fan).c_str(), false);
        //remote34_prev = remote34;
    }
    // Nút SW2 từ remote34 dùng để bật tắt máy bơm chữa cháy, đồng thời tắt chế độ tự động chữa cháy
    if (remote34 > 600 && remote34 < 900) {
        relay25 = !relay25;
        auto_fire = 0;
        digitalWrite(relay25_PIN, relay25);
        mqttClient.publish(relay25_topic, String(relay25).c_str(), false);
        mqttClient.publish(autofire_topic, String(auto_fire).c_str(), false);
        //remote34_prev = remote34;
    }
    // Nút SW3 từ remote34 dùng để bật tắt băng chuyền cho ăn
    if (remote34 > 1600 && remote34 < 2000) {
        relay26 = !relay26;
        digitalWrite(relay26_PIN, relay26);
        mqttClient.publish(relay26_topic, String(relay26).c_str(), false);
        //remote34_prev = remote34;
    }
    // Nút SW4 từ remote34 dùng để bật tắt máy bơm nước uống
    if (remote34 > 2700 && remote34 < 3100) {
        relay27 = !relay27;
        digitalWrite(relay27_PIN, relay27);
        mqttClient.publish(relay27_topic, String(relay27).c_str(), false);
        //remote34_prev = remote34;
    }
    // Nút SW1 từ remote35 dùng để bật tắt đèn sáng
    if (remote35 >= 0 && remote35 < 100) {
        brightness = !brightness;
        digitalWrite(LED_PIN, brightness);
        mqttClient.publish(LED_brightness_topic, String(brightness).c_str(), false);
        //remote34_prev = remote34;
    }
    // Nút SW2 từ remote35 dùng để bật tắt đèn sưởi, đồng thời tắt chế độ điều chỉnh nhiệt độ tự động
    if (remote35 > 600 && remote35 < 900) {
        heater = !heater;
        auto_fan = 0;
        digitalWrite(heater_PIN, heater);
        mqttClient.publish(heater_topic, String(heater).c_str(), false);
        mqttClient.publish(autofan_topic, String(auto_fan).c_str(), false);
        //remote34_prev = remote34;
    }
    // Nút SW3 từ remote35 dùng để bật tắt chế độ tự động điều chỉnh nhiệt độ
    if (remote35 > 1600 && remote35 < 2000) {
        auto_fan = !auto_fan;
        mqttClient.publish(autofan_topic, String(auto_fan).c_str(), false);
        //remote34_prev = remote34;
    }
    // Nút SW4 từ remote35 dùng để bật tắt chế độ tự động chữa cháy
    if (remote35 > 2700 && remote35 < 3100) {
        auto_fire = !auto_fire;
        digitalWrite(2, 1);
        mqttClient.publish(autofire_topic, String(auto_fire).c_str(), false);
        //remote34_prev = remote34;
    }
    remote35_prev = remote35;

    if (isnan(temperature)) {
        Serial.println("Failed to read from DHT sensor!");
        return;
    }
    if (auto_fan == 1) { // Xử lý chế độ tự động điều chỉnh nhiệt độ
        if (temperature > temp_max) { // Khi nhiệt độ quá cao (>temp_max) thì bật quạt, tắt đèn sưởi
            relay14 = 1;
            heater = 0;
        } else if (temperature < temp_min) { // Khi nhiệt độ quá thấp (<temp_min) thì bật đèn sưởi, tắt quạt
            relay14 = 0;
            heater = 1;
        } else {
            relay14 = 0;
            heater = 0;
        }
        digitalWrite(relay14_PIN, relay14);
        digitalWrite(heater_PIN, heater);
        lcd.setCursor(0, 1);
        lcd.print("FAN:AT"); // Hiển thị lên màn hình LCD, gửi trạng thái của quạt và máy sưởi qua MQTT
        mqttClient.publish(relay14_topic, String(relay14).c_str(), false);
        mqttClient.publish(heater_topic, String(heater).c_str(), false);
    } else {
        lcd.setCursor(0, 1);
        lcd.print("FAN:MN");
    }
    if (auto_fire == 1) { // Xử lý chế độ tự động chữa cháy
        if (fire_ss == 0) { // Khi có cháy, bật máy bơm chữa cháy, bật còi báo động
            relay25 = 1;
        } else {relay25 = 0;}
        digitalWrite(2, 1); // Dùng đèn trên esp32 để debug
        digitalWrite(relay25_PIN, relay25);
        lcd.setCursor(8, 1);
        lcd.print("FIRE:AT"); // Hiển thị lên màn hình LCD, kiểm tra việc chữa cháy qua Node-RED
        mqttClient.publish(relay25_topic, String(relay25).c_str(), false);
    } else {
        digitalWrite(2, 0);
        lcd.setCursor(8, 1);
        lcd.print("FIRE:MN");
    }
    // Hiển thị nhiệt độ, độ ẩm trên màn hình, đồng thời gửi lên Node-RED
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.setCursor(2, 0);
    lcd.print(temperature);
    lcd.setCursor(8, 0);
    lcd.print("H:");
    lcd.setCursor(10, 0);
    lcd.print(humidity);
    mqttClient.publish(temperature_topic, String(temperature).c_str(), false);
    mqttClient.publish(firess_topic, String(fire_ss).c_str(), false);
}
// Hàm mqttCallback giúp ESP32 nhận lệnh từ Node-RED
void mqttCallback(char *topic, uint8_t *payload, unsigned int length)
{
    if (strcmp(topic, LED_brightness_topic) == 0) {// Bật tắt đèn sáng
        char brightnessStr[length + 1];
        memcpy(brightnessStr, payload, length);
        brightnessStr[length] = '\0';
        brightness = atoi(brightnessStr);
        digitalWrite(LED_PIN, brightness);

    }
    if (strcmp(topic, relay14_topic) == 0) {// Bật tắt quạt
        char relay14Str[length + 1];
        memcpy(relay14Str, payload, length);
        relay14Str[length] = '\0';
        relay14 = atoi(relay14Str);
        digitalWrite(relay14_PIN, relay14);
    }
    if (strcmp(topic, relay25_topic) == 0) {// Bật tắt máy bơm chữa cháy
        char relay25Str[length + 1];
        memcpy(relay25Str, payload, length);
        relay25Str[length] = '\0';
        relay25 = atoi(relay25Str);
        digitalWrite(relay25_PIN, relay25);
    }
    if (strcmp(topic, relay26_topic) == 0) {// Bật tắt băng chuyền cho ăn
        char relay26Str[length + 1];
        memcpy(relay26Str, payload, length);
        relay26Str[length] = '\0';
        bool relay26 = atoi(relay26Str);
        digitalWrite(relay26_PIN, relay26);
    }
    if (strcmp(topic, relay27_topic) == 0) {// Bật tắt máy bơm nước uống
        char relay27Str[length + 1];
        memcpy(relay27Str, payload, length);
        relay27Str[length] = '\0';
        relay27 = atoi(relay27Str);
        digitalWrite(relay27_PIN, relay27);
    }
    if (strcmp(topic, heater_topic) == 0) {// Bật tắt đèn sưởi
        char heaterStr[length + 1];
        memcpy(heaterStr, payload, length);
        heaterStr[length] = '\0';
        heater = atoi(heaterStr);
        digitalWrite(heater_PIN, heater);
    }
    if (strcmp(topic, autofan_topic) == 0) {// Bật tắt chế độ tự động điều chỉnh nhiệt độ
        char autofanStr[length + 1];
        memcpy(autofanStr, payload, length);
        autofanStr[length] = '\0';
        auto_fan = atoi(autofanStr);
    }
    if (strcmp(topic, autofire_topic) == 0) {// Bật tắt chế độ tự động chữa cháy
        char autofireStr[length + 1];
        memcpy(autofireStr, payload, length);
        autofireStr[length] = '\0';
        auto_fire = atoi(autofireStr);
    }
    if (strcmp(topic, tempmax_topic) == 0) {// Nhận giá trị nhiệt độ tối đa (cho chế độ tự động điều chỉnh nhiệt độ)
        char tempmaxStr[length + 1];
        memcpy(tempmaxStr, payload, length);
        tempmaxStr[length] = '\0';
        temp_max = atoi(tempmaxStr);
    }
    if (strcmp(topic, tempmin_topic) == 0) {// Nhận giá trị nhiệt độ tối thiểu
        char tempminStr[length + 1];
        memcpy(tempminStr, payload, length);
        tempminStr[length] = '\0';
        temp_min = atoi(tempminStr);
    }
}

void setup()
{
    //Serial.begin(115200);
    delay(10);
    setup_wifi(ssid, password);
    tlsClient.setCACert(ca_cert);
    // Màn hình LCD 16x2
    lcd.begin(16, 2);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Hello");

    pinMode(firess_PIN, INPUT); // Cảm biến lửa
    pinMode(LED_PIN, OUTPUT); // Đèn sáng
    pinMode(relay14_PIN, OUTPUT);// Quạt
    digitalWrite(relay14_PIN, 0);
    pinMode(relay25_PIN, OUTPUT);// Máy bơm chữa cháy, còi báo cháy
    digitalWrite(relay25_PIN, 0);
    pinMode(relay26_PIN, OUTPUT);// Băng chuyền cho ăn
    digitalWrite(relay26_PIN, 0);
    pinMode(relay27_PIN, OUTPUT);// Máy bơm nước uống
    digitalWrite(relay27_PIN, 0);
    pinMode(heater_PIN, OUTPUT);// Đèn sưởi
    digitalWrite(heater_PIN, 0);
    pinMode(2, OUTPUT);// Đèn D2 dùng để kiểm tra chế độ tự động chữa cháy
    digitalWrite(2, auto_fire);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setServer(EMQX::broker, EMQX::port);
    dhtTicker.attach(1, dhtReadPublish);
}

void loop() {
    MQTT::reconnect(mqttClient, client_id, EMQX::username, EMQX::password, subs, 10);
    mqttClient.loop();
    delay(300);
}
