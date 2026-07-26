# ESP32 Cyberdeck (GHOUL OS)

A compact ESP32-powered cyberdeck built around a 1.8" ST7735 TFT display and a 4×4 keypad. The goal was to build something small, responsive, and easy to have neccessary apps within a simple navigation adn ui. Everything runs directly from the ESP32, making it a great little platform for experimenting with embedded projects.

---

# Features

* 1.8" ST7735R TFT Display (128×160) but kept at a 90 deg to get a wide screen
* 4×4 Keypad Navigation( having trouble with the  lower columns tho)
* Carousel based Interface
* Piezo Buzzer Audio Feedback
* Simple ASCII UI
* Runs entirely from ESP32 Flash
* SD Card Support
* Config file can be change if different pinnouts

---

# Wiring

## ST7735 TFT

| TFT Pin    | ESP32 Pin |
| ---------- | --------- |
| VCC        | 3.3V      |
| GND        | GND       |
| LED        | 3.3V      |
| SCK        | GPIO 18   |
| SDA (MOSI) | GPIO 23   |
| CS         | GPIO 5    |
| DC / A0    | GPIO 16   |
| RESET      | GPIO 17   |

> **Important:** Power the display from **3.3V**, not 5V.

---

## 4×4 Keypad

### Rows

| Keypad | ESP32   |
| ------ | ------- |
| L1     | GPIO 32 |
| L2     | GPIO 33 |
| L3     | GPIO 25 |
| L4     | GPIO 14 |

### Columns

| Keypad | ESP32   |
| ------ | ------- |
| L5     | GPIO 13 |
| L6     | GPIO 12 |
| L7     | GPIO 15 |
| L8     | GPIO 2  |

---

## Piezo Buzzer

| Piezo Pin | ESP32   |
| --------- | ------- |
| +         | GPIO 27 |
| -         | GND     |

The piezo provides a short beep while moving through menus, giving the interface a little more feedback without being distracting and also used for the minipiano app

---

## SD Card

The built-in SD card reader should be formatted as:

* **File System:** FAT32
* **Partition Scheme:** MBR (Master Boot Record)
* **Recommended Card Size:** 32GB or smaller (larger cards should also be formatted to FAT32)

---

# Required Libraries

Install these libraries through the Arduino Library Manager before compiling:

* TFT_eSPI
* Adafruit GFX Library
* SD
* SPI
* FS

If you're using custom fonts or images, make sure they are included with the project.

---


# Customizing

The project is designed to be easy to modify base isntead of starting scratch . Feel free to change the fonts, colors, menu layout, graphics, or add your own applications. If you build something cool with it, customize  it as ur wish

---

# License

Released under the MIT License.
