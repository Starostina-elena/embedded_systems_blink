# ESP32 Blink (GPIO27)

Простой пример мигания светодиодом на ESP32, подключенном к ноге D27.

Требования:
- ESP-IDF установлен и настроен

Сборка и прошивка (в директории проекта):

```bash
# (опционально) Установите таргет, если нужно:
idf.py set-target esp32
idf.py build
# Замените порт на ваш (например /dev/tty.SLAB_USBtoUART или /dev/ttyUSB0)
idf.py -p /dev/ttyUSB0 flash monitor
```

Файл `main/main.c` использует `GPIO_NUM_27`.
