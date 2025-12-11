## Mã nguồn firmware trên chip ESP32

- viết trên nền tảng PlatformIO/Arduino
- Đọc nhiệt độ và độ ẩm từ cảm biến DHT22 sử dụng thư viện adafruit/DHT sensor library
- Kết nối với EMQX broker: phần lớn mã và logic sử dụng lại từ dự án thí nghiệm MQTT experiment 
- Phần logic kết nối lại reconnection đã được viết lại tốt hơn để không bị blocking và hoạt động ổn định, quy hoạch mã này vào file MQTT.h
