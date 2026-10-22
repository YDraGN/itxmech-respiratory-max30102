# RespiCare wristband firmware (ESP32-C3 SuperMini + MAX30102)

Firmware cho vòng đeo đo SpO2 và nhịp tim. Vòng tự chạy luật cảnh báo và model `hybrid_tc` trên chip,
rồi gửi kết quả qua **BLE** cho app RespiCare trên điện thoại. App lưu lại và đẩy lên server.

```text
MAX30102 ──I2C──► ESP32-C3 ──BLE──► app Android ──HTTPS──► server ──► web bác sĩ / điện thoại khác
                     │
                     └─ không có app: vẫn đo, lưu vào flash (~6 ngày), app kết nối lại thì gửi bù
```

> **Gửi partner phần cứng:** đọc mục [Nạp code](#2-nạp-code) và [Kiểm tra sau khi nạp](#3-kiểm-tra-sau-khi-nạp)
> là đủ để chạy. Code này **chưa được build lần nào** ở phía nhóm phần mềm (máy chưa có ESP-IDF).
> Nếu build hoặc nạp lỗi, chụp/copy toàn bộ log lỗi gửi lại, đừng sửa code.

## Mục lục

1. [Chuẩn bị](#1-chuẩn-bị)
2. [Nạp code](#2-nạp-code)
3. [Kiểm tra sau khi nạp](#3-kiểm-tra-sau-khi-nạp)
4. [Firmware hoạt động thế nào](#4-firmware-hoạt-động-thế-nào)
5. [Cấu hình](#5-cấu-hình)
6. [Sự cố thường gặp](#6-sự-cố-thường-gặp)
7. [Cấu trúc thư mục](#7-cấu-trúc-thư-mục)
8. [Trạng thái và việc còn lại](#8-trạng-thái-và-việc-còn-lại)

## 1. Chuẩn bị

### Phần cứng (giữ nguyên mạch đang chạy thử)

| Tín hiệu | Chân ESP32-C3 SuperMini |
|---|---|
| MAX30102 SDA | GPIO8 |
| MAX30102 SCL | GPIO9 |
| MAX30102 INT | GPIO10 (chưa dùng ngắt, firmware đọc FIFO theo chu kỳ) |
| LED đỏ | GPIO4 (mức 1 = sáng) |
| LED xanh | GPIO5 (mức 1 = sáng) |
| Nguồn MAX30102 | 3V3 và GND |

Chân khác thì đổi trong menuconfig (mục [5](#5-cấu-hình)), không cần sửa code.

### Phần mềm: chọn một trong hai cách

| Cách | Cài gì | Khi nào dùng |
|---|---|---|
| **A. PlatformIO** (khuyến nghị) | VS Code + extension **PlatformIO IDE** | Dễ nhất, nhóm đã dùng cho firmware ống nghe |
| B. ESP-IDF | ESP-IDF **v5.1 trở lên** (Windows: *ESP-IDF Tools Installer* của Espressif) | Đã quen `idf.py` |

Lần build đầu sẽ tự tải bộ công cụ cho ESP32-C3, ESP-IDF và thư viện TFLite Micro (khoảng 1 GB, mất 10–20 phút
tùy mạng). Các lần sau build nhanh.

Cáp USB phải là **cáp có truyền dữ liệu** (không phải cáp chỉ sạc). SuperMini dùng USB-Serial-JTAG có sẵn
trong chip, Windows 10/11 tự nhận, không cần cài driver.

## 2. Nạp code

### Cách A: PlatformIO

1. Mở VS Code → *File → Open Folder…* → chọn thư mục `itxmech-max30102` (thư mục có file `platformio.ini`).
2. Chờ PlatformIO nhận project (thanh trạng thái phía dưới hiện biểu tượng PlatformIO).
3. Cắm SuperMini vào máy.
4. **Lần đầu nạp lên mạch đã có firmware cũ: xóa flash một lần.** Firmware mới dùng bảng phân vùng khác.
   Bấm biểu tượng PlatformIO bên trái → *esp32c3 → Platform → Erase Flash*, hoặc trong Terminal:

   ```bash
   pio run -t erase
   ```

5. Build và nạp: *esp32c3 → General → Upload*, hoặc:

   ```bash
   pio run -t upload
   ```

6. Xem log: *esp32c3 → General → Monitor*, hoặc:

   ```bash
   pio device monitor
   ```

   Thoát monitor bằng `Ctrl+C`.

Nếu `pio` không có trong Terminal, mở *PlatformIO: New Terminal* từ bảng lệnh (`Ctrl+Shift+P`).

**Bản demo nhanh (dùng khi trình diễn):** model cần đủ 60 phút dữ liệu mới chạy lần đầu. Env `esp32c3_demo`
rút 1 "phút" còn 10 giây nên khoảng 10 phút là thấy kết quả model trên app. Kết quả không có ý nghĩa y khoa.
Trong cây PlatformIO chọn *esp32c3_demo → Upload*, hoặc:

```bash
pio run -e esp32c3_demo -t upload
```

### Cách B: ESP-IDF (`idf.py`)

Mở *ESP-IDF Command Prompt* (có sẵn sau khi cài ESP-IDF), `cd` vào thư mục `itxmech-max30102`, rồi:

```bash
idf.py set-target esp32c3
```

```bash
idf.py build
```

Lần đầu nạp lên mạch đã có firmware cũ, xóa flash một lần:

```bash
idf.py -p COM5 erase-flash
```

```bash
idf.py -p COM5 flash monitor
```

Thay `COM5` bằng cổng của mạch (xem trong *Device Manager → Ports (COM & LPT)*, tên kiểu *USB JTAG/serial debug unit*).

## 3. Kiểm tra sau khi nạp

### 3.1 Log khi khởi động

```text
I (...) state: device_id=WB-C3-XXXX boot_id=1
I (...) model: hybrid_tc-20261016-s3 | model 20216 byte | arena dùng ... byte
I (...) spo2tc: self-test: 24/24 khớp | trung bình ... us/lần
I (...) store: 0/10880 entry, seq tiếp 1, acked 0
I (...) config: dùng config xuất xưởng (v0, T=88)
I (...) wifi_store: 0 mạng WiFi đã lưu
I (...) link: STANDALONE -> WAITING_APP
I (...) ble: quảng bá RespiCare-XXXX (200 ms)
I (...) max30102: MAX30102 detected (PART_ID=0x15)
I (...) spo2tc: MAX30102 OK
I (...) spo2tc: sec | SpO2=97.3  HR=76  valid=1  sqi=100 | WAITING_APP
```

Những dòng cần có: `self-test: 24/24`, `MAX30102 OK`, `quảng bá RespiCare-XXXX`. Mỗi giây có một dòng `sec |`,
mỗi phút có một dòng `MINUTE seq ...`.

### 3.2 LED

| LED | Ý nghĩa |
|---|---|
| Đỏ và xanh **nháy nhanh** | Vừa bật nguồn hoặc vừa mất kết nối: đang chờ app qua BLE (tối đa 60 giây) |
| Xanh **nháy chậm 3 giây** | App vừa kết nối và bắt tay thành công |
| Xanh sáng | Có ngón tay / đang đeo (như bản cũ) |
| Đỏ sáng | Không có ngón tay (như bản cũ) |
| Đỏ nháy liên tục | Không tìm thấy MAX30102: kiểm tra dây SDA/SCL, nguồn 3V3 |

Hết 60 giây không có app thì LED quay về báo đỏ/xanh như bản cũ để vẫn thử cảm biến được.
Vòng vẫn quảng bá BLE để app kết nối bất cứ lúc nào.

### 3.3 Thử BLE bằng nRF Connect (không cần app RespiCare)

Cài **nRF Connect for Mobile** (Android/iOS).

| Bước | Thao tác trong nRF Connect | Kết quả mong đợi |
|---|---|---|
| 1 | *Scanner* → thấy `RespiCare-XXXX` → *Connect* | Log vòng: `điện thoại đã kết nối` |
| 2 | Menu ⋮ → *Request MTU* → 247 | Log: `MTU = 247` |
| 3 | Mở service `a51c0001-…`, đọc characteristic `a51c0002` (DEVICE_INFO) | JSON `{"id":"WB-C3-XXXX","fw":"0.2.0",...}` |
| 4 | Bật notify (mũi tên 3 vạch) cho `a51c0003`, `a51c0004`, `a51c0005`, `a51c0006` | |
| 5 | Ghi `a51c0007` kiểu *Byte array*: `02` + 4 byte giờ hiện tại (epoch, little-endian) | Log: `đã đồng bộ giờ`. Từ đây `a51c0003` (LIVE) nhảy 8 byte mỗi giây |
| 6 | Ghi `a51c0007` = `0401000000` | **LED xanh nháy chậm 3 giây**, log `WAITING_APP -> GATEWAY` |
| 7 | Ghi `a51c0007` = `0300000000` | Nhận lại các bản ghi phút đã lưu trên `a51c0004` |
| 8 | Ngắt kết nối | LED nháy nhanh lại; sau 60 giây log `-> STANDALONE` |

Lấy epoch little-endian nhanh: vào https://www.epochconverter.com lấy số giây, đổi sang hex, đảo thứ tự 4 byte.
Ví dụ `1792137600` = `0x6AD1D980` → ghi `02 80 D9 D1 6A`.

### 3.4 Thử với app RespiCare

1. Cài app bản debug lên điện thoại Android thật (máy ảo không có Bluetooth thật).
2. Đăng nhập, bật **"Máy này kết nối vòng đeo tay"**, vào màn **Vòng đeo tay** → quét → chọn `RespiCare-XXXX`.
3. Vòng nháy xanh chậm 3 giây; app hiện SpO2/nhịp tim mỗi giây, bản ghi phút, pin "Không rõ".
4. Trong màn Vòng đeo tay có mục **WiFi cho vòng**: nhập tên WiFi + mật khẩu → *Gửi xuống vòng*.
   Log vòng: `đã lưu WiFi "TênWiFi" (1 mạng)`.

Thử nhanh không phải chờ từng phút: đặt `RESPI_MINUTE_PERIOD_S = 10` (mục 5). Khi đó cảnh báo của model không còn
ý nghĩa, chỉ dùng để thử kết nối.

## 4. Firmware hoạt động thế nào

Chi tiết đầy đủ: [docs/architecture.md](docs/architecture.md). Tóm tắt:

### 4.1 Đo và xử lý

1. **Task cảm biến** đọc FIFO MAX30102 khoảng 25 lần/giây. Thuật toán SpO2/HR **giữ nguyên bản của nhóm phần cứng**
   (`main/sensor/ppg.c`: ngưỡng IR > 40000, SpO2 = 104 − 17R, bắt đỉnh IR).
2. Mỗi giây: cộng vào bộ gộp phút, gửi **LIVE** qua BLE nếu có app, cập nhật LED.
3. Mỗi phút: tính trung bình/nhỏ nhất, chạy **engine luật + model** (`spo2_tc`), ra mức NORMAL / WATCH / ALERT / CRITICAL.
4. Bản ghi phút được **ghi vào flash trước**, rồi mới gửi **RECORDS** qua BLE. Mỗi 5 phút hoặc khi mức thay đổi thì gửi thêm **ASSESSMENT**.

### 4.2 Kết nối với app

```text
  bật nguồn / mất BLE ─► WAITING_APP (LED nháy nhanh, quảng bá BLE)
                              │ app bắt tay xong (SET_GATEWAY 1)
                              ▼
                          GATEWAY (LED xanh 3 s) ── mất BLE ──► WAITING_APP
                              │
  hết 60 s không có app ─► STANDALONE: vẫn đo, lưu flash, chờ app (sau này: tự gửi WiFi)
```

Khi app kết nối, trình tự là: xin MTU 247 → đọc DEVICE_INFO → bật notify → đồng bộ giờ (TIME_SYNC) →
`SET_GATEWAY(1)` → app xin gửi bù (BACKFILL) → mỗi bản ghi nhận được app lưu rồi trả ACK.

- **Không mất dữ liệu khi mất kết nối:** bản ghi nằm trong flash (vòng đệm khoảng 6 ngày). ACK chỉ đánh dấu, không xóa,
  nên điện thoại khác kết nối sau vẫn xin lại được dữ liệu 24 giờ gần nhất.
- **Mỗi lúc một điện thoại.** Khi đang có kết nối, vòng không quảng bá nên máy khác không thấy.
- **WiFi:** app gửi tên + mật khẩu qua characteristic WIFI_PROV (`a51c0009`), vòng lưu tối đa 3 mạng vào NVS.
  Phần tự kết nối WiFi gửi lên server khi không có app **chưa làm** (chờ server).

### 4.3 Các characteristic BLE

Service `a51c0001-7b2e-4f6a-9c3d-5e8f1b2a6c40`. Đặc tả đầy đủ:
`mobile-app/ITxMech_RespiratoryCare/docs/ble-gatt.md`.

| UUID | Tên | Kiểu | Nội dung |
|---|---|---|---|
| `a51c0002` | DEVICE_INFO | read | JSON id, fw, model, cfg, boot |
| `a51c0003` | LIVE | notify | 8 B mỗi giây: giờ, SpO2, nhịp tim, chất lượng tín hiệu |
| `a51c0004` | RECORDS | notify | bản ghi phút, 16 B/bản ghi |
| `a51c0005` | ASSESSMENT | notify | kết quả luật + model, 24 B |
| `a51c0006` | STATUS | read, notify | pin (0xFF = chưa đo), đang đeo, backlog, seq |
| `a51c0007` | CONTROL | write | ACK, TIME_SYNC, BACKFILL, SET_GATEWAY |
| `a51c0008` | CONFIG | read, write | ngưỡng bác sĩ đặt, 16 B |
| `a51c0009` | WIFI_PROV | read, write | WiFi app gửi xuống (đọc lại chỉ trả tên mạng) |

## 5. Cấu hình

Mở menuconfig: PlatformIO `pio run -t menuconfig`, ESP-IDF `idf.py menuconfig`. Vào mục **RespiCare wristband**
và **MAX30102**. Lưu (`S`) rồi build/nạp lại.

| Mục | Mặc định | Ý nghĩa |
|---|---|---|
| `RESPI_DEVICE_ID` | trống → `WB-C3-XXXX` theo MAC | Phải trùng id server cấp cho vòng |
| Chân GPIO (LED, INT) | 4 / 5 / 10 | |
| MAX30102: SDA / SCL / tốc độ / dòng LED | 8 / 9 / 400 kHz / `0x36` | Tăng dòng LED nếu tín hiệu cổ tay yếu |
| `RESPI_APP_WAIT_S` | 60 | Chờ app bao lâu trước khi tự chạy |
| `RESPI_LED_SEARCH_S` | 60 | Thời gian LED nháy nhanh; 0 = nháy đến khi có app |
| `RESPI_MINUTE_PERIOD_S` | 60 | Đặt 10 khi chỉ thử BLE |
| `RESPI_LOG_EVERY_SECOND` | bật | Log SpO2/HR mỗi giây |

## 6. Sự cố thường gặp

| Hiện tượng | Cách xử lý |
|---|---|
| Không thấy cổng COM | Đổi cáp (cáp có dữ liệu), cắm cổng USB khác |
| Nạp báo lỗi kết nối / timeout | Giữ nút **BOOT**, nhấn-thả **RESET**, thả BOOT, rồi nạp lại |
| `PART_ID = 0x00` hoặc `0xFF`, LED đỏ nháy | Kiểm tra dây SDA (GPIO8) / SCL (GPIO9), nguồn 3V3; module tím GY-MAX30102 có thể phải chuyển điện trở kéo lên sang 3,3 V |
| Boot lặp lại liên tục | GPIO8/GPIO9 là chân strapping: rút MAX30102 ra rồi cắm lại thử; nếu hết thì cân nhắc chuyển I2C sang GPIO6/7 |
| `không có partition 'reclog'` | Chưa xóa flash sau khi đổi bảng phân vùng: chạy lại bước xóa flash |
| Build lỗi tải `esp-tflite-micro` | Kiểm tra mạng, build lại; lỗi vẫn còn thì gửi log |
| Không thấy `RespiCare-XXXX` khi quét | Vòng đang có kết nối với máy khác (không quảng bá); tắt Bluetooth máy kia |
| SpO2 nhảy lung tung | Thuật toán và công thức `104 − 17R` chưa hiệu chuẩn, giữ nguyên như bản thử nghiệm |

## 7. Cấu trúc thư mục

```text
itxmech-max30102/
├── platformio.ini            build bằng PlatformIO
├── CMakeLists.txt            project ESP-IDF, PROJECT_VER = phiên bản firmware
├── sdkconfig.defaults        ESP32-C3, BLE NimBLE (1 kết nối, MTU 247), bảng phân vùng riêng
├── partitions.csv            nvs, app 2 MB, reclog 512 KB (lưu bản ghi)
├── main/
│   ├── app_main.cc           khởi tạo, task cảm biến, vòng lặp phút
│   ├── Kconfig.projbuild     các mục trong menuconfig
│   ├── ble/                  ble_gatt.c (BLE server), ble_proto.c (đóng gói byte)
│   ├── link/                 link_manager.c (chờ app / gateway / tự chạy), uplink.c (khung gửi WiFi)
│   ├── sensor/               ppg.c (thuật toán SpO2/HR), vitals.c (gộp phút)
│   ├── storage/              record_log/record_store (flash), device_state (NVS), wifi_store (WiFi)
│   ├── system/               led_status.c, device_clock.c, wristband_config.c
│   └── model/                model_runner.cc (TFLite Micro)
├── components/
│   ├── max30102/             driver I2C
│   └── spo2_tc/              luật + model + vector kiểm tra, chép từ SPO2-RESEARCH
├── tests/host/               test chạy trên máy tính (bash tests/host/run.sh, cần gcc)
├── scripts/sync_spo2_tc.sh   đồng bộ lại model khi có bản mới
├── docs/architecture.md      chi tiết kiến trúc
└── legacy/hardware-snapshot/ code gốc nhóm phần cứng (chỉ để đối chiếu, không build)
```

## 8. Trạng thái và việc còn lại

| Phần | Trạng thái |
|---|---|
| Đo SpO2/HR, gộp phút, luật + model, self-test | Có (từ bản phần cứng) |
| BLE GATT (8 characteristic), lưu flash, ACK/BACKFILL, đổi điện thoại | Có, test logic trên máy tính, **chưa build/chạy trên mạch** |
| LED báo trạng thái, máy trạng thái chờ app / gateway / tự chạy | Có, chưa chạy trên mạch |
| Nhận WiFi từ app qua BLE | Có (chỉ lưu) |
| Tự kết nối WiFi gửi lên server khi không có app | Chưa (chờ server có endpoint và khóa thiết bị) |
| Mã hóa BLE (bonding), đo pin, motor rung, SQI thật | Chưa |

Khi gửi lại kết quả thử cho nhóm phần mềm, kèm: log từ lúc bật nguồn đến khi app kết nối, ảnh/clip LED,
và ghi chú bước nào trong mục 3 không đúng.
