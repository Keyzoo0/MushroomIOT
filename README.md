# MushroomIOT

**Rancang Bangun Sistem Budidaya Jamur Tiram dengan Parameter Suhu, Kelembaban, dan Kelembapan Tanah Berbasis IoT**

Sistem monitoring & kontrol budidaya jamur tiram berbasis **ESP32** dengan antarmuka **web server** (LittleFS) yang menampilkan data realtime, grafik, serta kontrol aktuator otomatis/manual.

Tugas Akhir — Program Studi D3 Teknik Komputer, Jurusan Teknik Komputer, **Politeknik Negeri Sriwijaya**, Palembang, 2026.

| | |
|---|---|
| **Nama** | Muhammad Dzakwan Al Buchori |
| **NIM** | 062330701497 |
| **Prodi** | D3 Teknik Komputer |

---

## Fitur

- **Monitoring realtime**: suhu & kelembapan udara (SHT31), kelembapan media tanam, serta estimasi kadar air serbuk gergaji (baglog) beserta status *Kering / Ideal / Basah*.
- **Grafik FIFO 3 menit** (60 titik @3 detik) tersimpan di `grafik.json` pada LittleFS.
- **Kontrol per-aktuator** dengan mode **AUTO** (if-else berdasarkan threshold) atau **MANUAL** (ON/OFF dari web):
  - Relay **Fan** (pendingin) — ON saat suhu di atas batas.
  - Relay **Lampu** (pemanas) — ON saat suhu di bawah batas.
  - Relay **Mist Maker** (penambah kelembapan) — ON saat kelembapan udara di bawah batas.
- **Pengaturan via web** (tab Settings): threshold auto, kalibrasi ADC soil, batas status serbuk gergaji, dan **reset data grafik**.
- **Indikator LED**: hijau = WiFi terhubung, merah berkedip = terputus/reconnecting.
- Pengaturan tersimpan permanen di **Preferences (NVS)**, tahan reboot.
- UI **Bootstrap 5 + Chart.js** (via CDN), 3 tab: Dashboard, Settings, Info.

## Perangkat Keras

| Komponen | Keterangan |
|---|---|
| ESP32 DevKit V1 | Mikrokontroler utama |
| GY-SHT31 | Sensor suhu & kelembapan udara (I2C) |
| Capacitive Soil Moisture | Sensor kelembapan media (ADC 0–4095) |
| Relay 3 channel | Fan, Lampu pemanas, Mist maker |
| LED Hijau & Merah | Indikator status WiFi |

### Peta Pin

| Fungsi | GPIO |
|---|---|
| SHT31 SDA / SCL | 21 / 22 |
| Soil Moisture (ADC) | 34 |
| Relay Fan | 25 |
| Relay Lampu | 26 |
| Relay Mist | 27 |
| LED Hijau | 16 |
| LED Merah | 17 |

### Logika Relay

| Relay | Logika | Keterangan |
|-------|--------|------------|
| Fan (GPIO 25) | **Active-LOW** (LOW = ON) | Relay aktif saat level LOW |
| Lampu (GPIO 26) | **Active-HIGH** (HIGH = ON) | Relay aktif saat level HIGH |
| Mist (GPIO 27) | **Active-LOW** (LOW = ON) | Relay aktif saat level LOW |

## Perangkat Lunak

- **Arduino IDE** dengan core **ESP32**.
- Library yang dibutuhkan:
  - [ESP Async WebServer](https://github.com/ESP32Async/ESPAsyncWebServer) + [Async TCP](https://github.com/ESP32Async/AsyncTCP)
  - [ArduinoJson](https://arduinojson.org/) (v6/v7)
  - Adafruit SHT31 (+ Adafruit Unified Sensor + Adafruit BusIO)
  - LittleFS & Preferences (bawaan core ESP32)

## Struktur Proyek

```
DzaqwanFiwmware.ino   Firmware ESP32
data/                 File web yang di-upload ke LittleFS
├── index.html
├── style.css
├── app.js
└── grafik.json       Data grafik (FIFO, awalnya dummy)
```

## Cara Pakai

1. Atur SSID & password WiFi pada bagian atas `DzaqwanFiwmware.ino`:
   ```cpp
   const char* WIFI_SSID = "ROSI1";
   const char* WIFI_PASS = "20517420";
   ```
2. Install library di atas, lalu **Upload** sketch ke ESP32.
3. Upload UI ke LittleFS: **Tools → ESP32 LittleFS Data Upload**
   (plugin [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload)).
4. Buka **Serial Monitor** (115200) untuk melihat alamat IP ESP32.
5. Akses lewat browser di **http://jamur.local** (mDNS) atau langsung via alamat IP-nya.
   Perangkat harus berada pada jaringan yang sama dan terhubung internet untuk memuat CDN.
6. Kalibrasi sensor soil melalui tab **Settings** (isi nilai ADC kering & basah sesuai pengukuran).

## REST API

| Method | Endpoint | Fungsi |
|---|---|---|
| GET | `/api/data` | Data sensor & status aktuator realtime |
| GET | `/api/settings` | Ambil pengaturan |
| POST | `/api/settings` | Simpan pengaturan |
| POST | `/api/mode` | Set mode aktuator (auto/manual) |
| POST | `/api/actuator` | Set ON/OFF aktuator (mode manual) |
| GET | `/api/graph` | Data grafik FIFO |
| POST | `/api/graph/reset` | Reset data grafik |

---

© 2026 — Politeknik Negeri Sriwijaya
