# RespiCare wristband firmware (ESP32-C3 SuperMini + MAX30102)

Firmware cho vòng đeo đo SpO2 và nhịp tim. Vòng tự chạy luật cảnh báo và model `hybrid_tc` trên chip,
rồi gửi kết quả qua **BLE** cho app RespiCare trên điện thoại. App lưu lại và đẩy lên server.

```text
MAX30102 ──I2C──► ESP32-C3 ──BLE──► app Android ──HTTPS──► server ──► web bác sĩ / điện thoại khác
                     │
                     ├─ không có app, đã cài WiFi + khóa: tự bật WiFi gửi thẳng lên server (chế độ WiFi)
                     └─ không có app, chưa cài WiFi: vẫn đo, lưu vào flash (~6 ngày), app kết nối lại thì gửi bù
```

> **Bản này đổi cách chạy so với bản trước** (xem [mục 9](#9-thay-đổi-ở-bản-này)): bảng phân vùng mới
> (**phải xóa flash trước khi nạp**), bảng LED mới, và chế độ WiFi tự gửi lên server.

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
9. [Thay đổi ở bản này](#9-thay-đổi-ở-bản-này)

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
4. **Lần đầu nạp lên mạch đã có firmware cũ (kể cả bản RespiCare trước): xóa flash một lần.** Bản này dùng
   bảng phân vùng khác (app 3 MB). Xóa flash cũng xóa WiFi, khóa thiết bị và bản ghi chưa gửi trên vòng:
   cài lại WiFi từ app sau khi nạp.
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
I (...) device_cred: chưa có khóa thiết bị (app cấp sau khi cài WiFi)
I (...) link: STANDALONE -> WAITING_APP
I (...) ble: quảng bá RespiCare-XXXX (200 ms)
I (...) max30102: MAX30102 detected (PART_ID=0x15)
I (...) spo2tc: MAX30102 OK
I (...) spo2tc: live | SpO2=97.3 (min 95.8)  HR=76  valid=100%  sqi=100 | WAITING_APP
```

Những dòng cần có: `self-test: 24/24`, `MAX30102 OK`, `quảng bá RespiCare-XXXX`, `diag: lý do khởi động lần này: …`.
Mỗi 15 giây có một dòng `live |` (số gửi cho app), mỗi phút một dòng `MINUTE seq ...`, mỗi 5 phút một dòng `DIAG |`
(mục 3.6). Dòng `sec |` mỗi giây đã tắt mặc định; bật lại bằng `RESPI_LOG_EVERY_SECOND`.

### 3.2 LED

LED **chỉ chớp ngắn (50 ms)** rồi tắt, để tiết kiệm pin; không còn LED sáng liên tục như bản cũ.
"Vàng" = đỏ và xanh sáng cùng lúc. Nhiều trạng thái cùng lúc thì dòng trên cùng thắng.

| Ưu tiên | Trạng thái | LED |
|---|---|---|
| 1 | Không tìm thấy MAX30102 | Đỏ nháy đều liên tục (200 ms): kiểm tra dây SDA/SCL, nguồn 3V3 |
| 2 | Mức **CRITICAL** | Đỏ chớp **3 lần**, mỗi 2 giây |
| 3 | Mức **ALERT** | Đỏ chớp 1 lần, mỗi 2 giây |
| 4 | Đang chờ app (vừa bật nguồn / vừa mất BLE, tối đa 60 giây) | Vàng chớp **2 lần**, mỗi 2 giây |
| 5 | Không đeo / không có ngón tay | Tắt |
| 6 | Chế độ WiFi: không có app, đã có WiFi + khóa | Vàng chớp 1 lần, mỗi 5 giây |
| 7 | Không có app, chưa cài WiFi hoặc chưa có khóa (chỉ lưu flash) | Xanh chớp 1 lần, mỗi 10 giây |
| 8 | Đã nối app (gateway), bình thường | Xanh chớp 1 lần, mỗi 5 giây |

Lúc vừa cấp nguồn LED đỏ sáng một nhịp ngắn rồi chuyển sang bảng trên. Bảng nằm ở `main/system/led_pattern.c`
(có test `tests/host/test_led_pattern.c`). Vòng vẫn quảng bá BLE để app kết nối bất cứ lúc nào.

### 3.3 Thử BLE bằng nRF Connect (không cần app RespiCare)

Cài **nRF Connect for Mobile** (Android/iOS).

| Bước | Thao tác trong nRF Connect | Kết quả mong đợi |
|---|---|---|
| 1 | *Scanner* → thấy `RespiCare-XXXX` → *Connect* | Log vòng: `điện thoại đã kết nối` |
| 2 | Menu ⋮ → *Request MTU* → 247 | Log: `MTU = 247` |
| 3 | Mở service `a51c0001-…`, đọc characteristic `a51c0002` (DEVICE_INFO) | JSON `{"id":"WB-C3-XXXX","fw":"0.3.0",...}` |
| 4 | Bật notify (mũi tên 3 vạch) cho `a51c0003`, `a51c0004`, `a51c0005`, `a51c0006` | |
| 5 | Ghi `a51c0007` kiểu *Byte array*: `02` + 4 byte giờ hiện tại (epoch, little-endian) | Log: `đã đồng bộ giờ`. Từ đây `a51c0003` (LIVE) nhảy 12 byte mỗi 15 giây |
| 6 | Ghi `a51c0007` = `0401000000` | Log `WAITING_APP -> GATEWAY`; đang đeo thì LED xanh chớp mỗi 5 giây |
| 7 | Ghi `a51c0007` = `0300000000` | Nhận lại các bản ghi phút đã lưu trên `a51c0004` |
| 8 | Ngắt kết nối | LED vàng chớp đôi lại; sau 60 giây log `-> STANDALONE` |

Lấy epoch little-endian nhanh: vào https://www.epochconverter.com lấy số giây, đổi sang hex, đảo thứ tự 4 byte.
Ví dụ `1792137600` = `0x6AD1D980` → ghi `02 80 D9 D1 6A`.

### 3.4 Thử với app RespiCare

1. Cài app bản debug lên điện thoại Android thật (máy ảo không có Bluetooth thật).
2. Đăng nhập, bật **"Máy này kết nối vòng đeo tay"**, vào màn **Vòng đeo tay** → quét → chọn `RespiCare-XXXX`.
3. Đang đeo thì vòng chớp xanh mỗi 5 giây; app hiện SpO2/nhịp tim mỗi 15 giây kèm "Đo lúc hh:mm:ss", bản ghi phút,
   pin "Không rõ". Máy người nhà khác và web bác sĩ hiện cùng số, cùng giờ đo (chậm hơn vài giây).
4. Trong màn Vòng đeo tay có mục **WiFi cho vòng**: nhập tên WiFi + mật khẩu → *Gửi xuống vòng*.
   Log vòng: `đã lưu WiFi "TênWiFi" (1 mạng)`, rồi ngay sau đó app tự cấp khóa:
   `đã lưu khóa thiết bị mới, server https://.../api/app/v1`. App báo "Vòng đã lưu WiFi … và nhận khóa gửi dữ liệu".
   Nếu app báo "chưa nhận khóa", bấm **Cấp lại khóa gửi dữ liệu** (cần mạng trên điện thoại và vòng đã được bác sĩ
   giao cho đúng bệnh nhân trên web).

### 3.5 Thử chế độ WiFi (vòng tự gửi lên server)

Điều kiện: đã làm bước 3.4 (vòng có WiFi + khóa), server đã cập nhật bản có `device-key` và `ble_config_hex`,
WiFi là **2,4 GHz** (ESP32-C3 không bắt được 5 GHz).

1. Trong app: màn Vòng đeo tay → *Ngắt kết nối* (hoặc tắt Bluetooth điện thoại).
2. Chờ `RESPI_APP_WAIT_S` (60 giây): log `-> STANDALONE`, LED đổi sang vàng chớp mỗi 5 giây.
3. Vòng thử gửi ngay một lần. Log mong đợi:

   ```text
   I (...) uplink: định kỳ: bật WiFi, 12 bản ghi chờ gửi
   I (...) uplink: đã vào WiFi "TênWiFi"
   I (...) clock: đã đồng bộ giờ: 1792137800
   I (...) uplink: gửi 12 bản ghi (seq 1..12), server acked 12
   I (...) uplink: xong: đã gửi 12 bản ghi, còn 0 chờ (acked 12 / last 12)
   ```

4. Trên web bác sĩ (hồ sơ bệnh nhân), biểu đồ SpO₂ có thêm các phút vừa gửi; thiết bị hiện kết nối `wifi`.
5. Sau đó vòng gửi mỗi `upload_interval_min` phút (mặc định 10, bác sĩ đổi trên web), và gửi **ngay** khi có
   ALERT/CRITICAL. Ngoài giờ gửi, WiFi tắt.
6. Đổi ngưỡng T trên web → lần gửi kế tiếp log `nhận config v… từ server`, web hiện "đã áp dụng trên vòng".

Lỗi hay gặp: `không vào được WiFi` (sai mật khẩu / WiFi 5 GHz), `HTTP 401 Khóa thiết bị không đúng`
(cấp lại khóa từ app), `HTTP 404 NO_ASSIGNMENT` (bác sĩ chưa giao vòng cho bệnh nhân), `HTTP -1`
(không ra được Internet hoặc sai giờ hệ thống làm HTTPS lỗi).

Thử nhanh không phải chờ từng phút: đặt `RESPI_MINUTE_PERIOD_S = 10` (mục 5). Khi đó cảnh báo của model không còn
ý nghĩa, chỉ dùng để thử kết nối.

### 3.6 Đo hiệu năng (CPU, thời gian model) khi chạy không dây

Mỗi 5 phút vòng đóng một cửa sổ đo và in:

```text
I (...) diag: DIAG | CPU tb 2.31% đỉnh 9.8% | model 18.2 ms | phút 21.5 ms | flash 0.9 ms | heap min 61 KB | WiFi 0 s | BLE bỏ 0 | reset POWERON
I (...) diag: task tốn CPU nhất: sensor 1.20%, ble_tx 0.40%, main 0.05%, nimble_host 0.30%
```

- **CPU tb / đỉnh**: 100% trừ phần thời gian task IDLE; đỉnh là giây bận nhất trong 5 phút (giây có chạy model).
  Tiêu chí nhóm đặt: đỉnh dưới ~70% là tốt, trên 85% là quá tải.
- **model**: cả lần chạy model (baseline + đặc trưng + lượng tử + TFLite). Self-test lúc boot đo được khoảng 18 ms.
- **phút**: toàn bộ xử lý một phút (gộp, luật, model, ghi flash, xếp hàng BLE). **flash**: lần ghi flash lâu nhất.
- **WiFi**: số giây WiFi bật trong cửa sổ (chế độ không có app). **reset**: lý do lần khởi động này (`BROWNOUT` = sụt áp,
  `PANIC` / `TASK_WDT` = treo).

Không cắm USB vẫn xem được 3 cách:

1. **Web bác sĩ** → hồ sơ bệnh nhân → tab SpO₂ → mục **Hiệu năng vòng (kỹ thuật)**: bảng 24 giờ, mỗi dòng 5 phút.
   Có app thì app chuyển lên; không có app thì vòng tự gửi qua WiFi (`ingest/diagnostics`) ở mỗi lượt WiFi.
2. **App** → màn Vòng đeo tay → thẻ **Nhật ký vòng** → *Bắt đầu nhận*: xem log như cổng USB (chỉ bản `esp32c3_demo`).
3. **nRF Connect**: bật notify `a51c000b` (log) hoặc đọc `a51c000a` (DIAG gần nhất, 24 byte, mục 3.9 của `ble-gatt.md`).

Build lần đầu sau bản này phải **xóa file `sdkconfig` / `sdkconfig.esp32c3`** để nhận 2 dòng FreeRTOS mới trong
`sdkconfig.defaults` (bộ đếm thời gian chạy task). Không xóa thì vòng vẫn chạy nhưng log báo `chưa bật run-time stats`
và cột CPU để trống.

**Hai kịch bản thử:**

| Kịch bản | Cách làm | Xem gì |
|---|---|---|
| A. Có app (gateway) | App kết nối vòng, đeo vòng ≥ 70 phút (bản demo: ≥ 12 phút) | App, máy người nhà khác và web cùng số, cùng "đo lúc"; DIAG mỗi 5 phút trên web; CPU đỉnh lúc model chạy |
| B. Không có app (WiFi) | Cài WiFi + khóa (mục 3.4), rồi ngắt app; chờ `-> STANDALONE` | `WiFi … s` trong DIAG, web hiện "Đang dùng WiFi dự phòng", số cập nhật mỗi `upload_interval_min` phút |

Đo dòng điện thật (mA) vẫn cần đồng hồ nối tiếp với pin (USB power meter, INA219…); DIAG chỉ cho biết CPU bận bao nhiêu.

## 4. Firmware hoạt động thế nào

Chi tiết đầy đủ: [docs/architecture.md](docs/architecture.md). Tóm tắt:

### 4.1 Đo và xử lý

1. **Task cảm biến** đọc FIFO MAX30102 khoảng 25 lần/giây. Thuật toán SpO2/HR **giữ nguyên bản của nhóm phần cứng**
   (`main/sensor/ppg.c`: ngưỡng IR > 40000, SpO2 = 104 − 17R, bắt đỉnh IR).
2. Mỗi giây: cộng vào bộ gộp phút và cửa sổ 15 giây, cập nhật LED. Mỗi 15 giây gửi một **LIVE** (trung bình cửa sổ)
   qua BLE nếu có app: đây là số hiển thị chung, app chuyển lên server để mọi máy thấy cùng số.
3. Mỗi phút: tính trung bình/nhỏ nhất, chạy **engine luật + model** (`spo2_tc`), ra mức NORMAL / WATCH / ALERT / CRITICAL.
4. Bản ghi phút được **ghi vào flash trước**, rồi mới gửi **RECORDS** qua BLE. Mỗi 5 phút hoặc khi mức thay đổi thì gửi thêm **ASSESSMENT**.

### 4.2 Kết nối với app

```text
  bật nguồn / mất BLE ─► WAITING_APP (LED nháy nhanh, quảng bá BLE)
                              │ app bắt tay xong (SET_GATEWAY 1)
                              ▼
                          GATEWAY (app chuyển tiếp lên server) ── mất BLE ──► WAITING_APP
                              │
  hết 60 s không có app ─► STANDALONE: vẫn đo, lưu flash; có WiFi + khóa thì tự gửi lên server
```

Khi app kết nối, trình tự là: xin MTU 247 → đọc DEVICE_INFO → bật notify → đồng bộ giờ (TIME_SYNC) →
`SET_GATEWAY(1)` → app xin gửi bù (BACKFILL) → mỗi bản ghi nhận được app lưu rồi trả ACK.

- **Không mất dữ liệu khi mất kết nối:** bản ghi nằm trong flash (vòng đệm khoảng 6 ngày). ACK chỉ đánh dấu, không xóa,
  nên điện thoại khác kết nối sau vẫn xin lại được dữ liệu 24 giờ gần nhất.
- **Mỗi lúc một điện thoại.** Khi đang có kết nối, vòng không quảng bá nên máy khác không thấy.
- **WiFi:** app gửi tên + mật khẩu qua characteristic WIFI_PROV (`a51c0009`), vòng lưu tối đa 3 mạng vào NVS.
  Sau đó app xin **khóa thiết bị** từ server (`POST /api/app/v1/device-key`) và ghi khóa + địa chỉ server xuống vòng
  (WIFI_PROV op `0x03`). Mỗi lần cấp khóa mới thì khóa cũ hết hiệu lực.

### 4.4 Chế độ WiFi (`main/link/uplink.c`)

Chỉ chạy ở STANDALONE. Mỗi `upload_interval_min` phút, hoặc ngay khi có ALERT/CRITICAL:

1. Bật WiFi, thử lần lượt các mạng đã lưu (mới nhất trước, mỗi mạng tối đa 15 giây).
2. Chưa có giờ thì gửi một lô rỗng để lấy `server_time`.
3. Lặp tối đa 20 lô: đọc tối đa 60 bản ghi phút từ `acked_seq + 1` trong flash, gửi assessment cùng khoảng seq
   (`POST ingest/assessments`, 20 cái/lô) rồi bản ghi phút (`POST ingest/telemetry`, `transport: "wifi"`).
   Server trả `acked_seq` thì dời con trỏ (dữ liệu vẫn trong flash tới khi bị ghi đè, app vẫn xin lại được).
4. Server báo có config mới thì `GET device/config` (etag = version đang dùng), lấy `ble_config_hex` (đúng 16 byte
   CONFIG như qua BLE), áp dụng, rồi `POST ingest/config-ack`.
5. Tắt WiFi.

Mọi request có header `x-device-id` / `x-device-key`, chạy HTTPS và kiểm tra chứng chỉ bằng bộ CA gốc của ESP-IDF.
JSON giống hệt app gửi khi chuyển tiếp BLE (`main/link/uplink_json.c`, test `tests/host/test_uplink_json.c`).

### 4.3 Các characteristic BLE

Service `a51c0001-7b2e-4f6a-9c3d-5e8f1b2a6c40`. Đặc tả đầy đủ:
`mobile-app/ITxMech_RespiratoryCare/docs/ble-gatt.md`.

| UUID | Tên | Kiểu | Nội dung |
|---|---|---|---|
| `a51c0002` | DEVICE_INFO | read | JSON id, fw, model, cfg, boot |
| `a51c0003` | LIVE | notify | 12 B mỗi 15 giây: giờ đo, SpO2 trung bình / nhỏ nhất, nhịp tim, tỷ lệ giây hợp lệ, đang đeo |
| `a51c0004` | RECORDS | notify | bản ghi phút, 16 B/bản ghi |
| `a51c0005` | ASSESSMENT | notify | kết quả luật + model, 24 B |
| `a51c0006` | STATUS | read, notify | pin (0xFF = chưa đo), đang đeo, backlog, seq |
| `a51c0007` | CONTROL | write | ACK, TIME_SYNC, BACKFILL, SET_GATEWAY |
| `a51c0008` | CONFIG | read, write | ngưỡng bác sĩ đặt, 16 B |
| `a51c0009` | WIFI_PROV | read, write | `01` thêm WiFi, `02` xóa WiFi, `03` khóa thiết bị + URL server; đọc lại chỉ trả tên mạng |
| `a51c000a` | DIAG | read, notify | hiệu năng mỗi 5 phút, 24 B: % CPU tb / đỉnh, thời gian model / phút / flash, RAM, WiFi, lý do reset |
| `a51c000b` | DEBUG_LOG | notify | nhật ký dạng chữ, **chỉ bản demo** (`RESPI_BLE_DEBUG_LOG`) |

## 5. Cấu hình

Mở menuconfig: PlatformIO `pio run -t menuconfig`, ESP-IDF `idf.py menuconfig`. Vào mục **RespiCare wristband**
và **MAX30102**. Lưu (`S`) rồi build/nạp lại.

| Mục | Mặc định | Ý nghĩa |
|---|---|---|
| `RESPI_DEVICE_ID` | trống → `WB-C3-XXXX` theo MAC | Phải trùng id server cấp cho vòng |
| Chân GPIO (LED, INT) | 4 / 5 / 10 | |
| MAX30102: SDA / SCL / tốc độ / dòng LED | 8 / 9 / 400 kHz / `0x36` | Tăng dòng LED nếu tín hiệu cổ tay yếu |
| `RESPI_APP_WAIT_S` | 60 | Chờ app bao lâu trước khi tự chạy |
| `RESPI_LED_SEARCH_S` | 60 | Thời gian LED chớp "chờ app"; 0 = chớp đến khi có app |
| `RESPI_MINUTE_PERIOD_S` | 60 | Đặt 10 khi chỉ thử BLE |
| `RESPI_LOG_EVERY_SECOND` | **tắt** | Log SpO2/HR mỗi giây (đã có dòng `live |` mỗi 15 giây) |
| `RESPI_LIVE_PERIOD_S` | 15 | Chu kỳ gói LIVE (số hiển thị chung cho app, người nhà, web) |
| `RESPI_DIAG_PERIOD_MIN` | 5 | Chu kỳ DIAG; nên bằng nhịp model để mỗi cửa sổ có đúng một lần chạy model |
| `RESPI_BLE_DEBUG_LOG` | tắt (bản demo: bật) | Gửi log qua BLE để xem trên app khi không cắm USB |

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
| Build báo app quá lớn cho partition | Đã nới app lên 3 MB; vẫn lỗi thì gửi log (dòng `app partition is too small`) |
| Chế độ WiFi không gửi | Xem mục 3.5 phần "Lỗi hay gặp" |

## 7. Cấu trúc thư mục

```text
itxmech-max30102/
├── platformio.ini            build bằng PlatformIO
├── CMakeLists.txt            project ESP-IDF, PROJECT_VER = phiên bản firmware
├── sdkconfig.defaults        ESP32-C3, BLE NimBLE (1 kết nối, MTU 247), bảng phân vùng riêng
├── partitions.csv            nvs, app 3 MB, reclog 512 KB (lưu bản ghi)
├── main/
│   ├── app_main.cc           khởi tạo, task cảm biến, vòng lặp phút
│   ├── Kconfig.projbuild     các mục trong menuconfig
│   ├── ble/                  ble_gatt.c (BLE server), ble_proto.c (đóng gói byte)
│   ├── link/                 link_manager.c (chờ app / gateway / tự chạy), uplink.c (chế độ WiFi), uplink_json.c
│   ├── sensor/               ppg.c (thuật toán SpO2/HR), vitals.c (gộp phút)
│   ├── storage/              record_log/record_store (flash), device_state (NVS), wifi_store (WiFi), device_cred (khóa)
│   ├── system/               led_pattern.c (bảng LED), led_status.c, device_clock.c, wristband_config.c
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
| LED báo trạng thái (bảng mới, chớp ngắn), máy trạng thái chờ app / gateway / tự chạy | Có, test logic trên máy tính, chưa chạy trên mạch |
| Nhận WiFi + khóa thiết bị từ app qua BLE | Có, test logic trên máy tính |
| Tự kết nối WiFi gửi lên server khi không có app | Có, **chưa build và chưa chạy trên mạch** (máy nhóm phần mềm chưa có ESP-IDF) |
| Mã hóa BLE (bonding), đo pin, motor rung, SQI thật | Chưa |

Khi gửi lại kết quả thử cho nhóm phần mềm, kèm: log từ lúc bật nguồn đến khi app kết nối, ảnh/clip LED,
và ghi chú bước nào trong mục 3 không đúng.

## 9. Thay đổi ở bản này

| Thay đổi | Ảnh hưởng khi chạy |
|---|---|
| `partitions.csv`: app 2 MB → **3 MB**, `reclog` dời sang `0x310000` | **Bắt buộc xóa flash** trước khi nạp (mục 2). Mất WiFi, khóa và bản ghi cũ trên vòng |
| Thêm WiFi + HTTPS (`esp_wifi`, `esp_http_client`, `mbedtls`) | Lần build đầu tải thêm thành phần ESP-IDF; firmware lớn hơn khoảng 0,6 MB; RAM lúc gửi tăng khoảng 60 KB |
| `sdkconfig.defaults`: bật chia sóng WiFi/BLE, bộ CA gốc | Đã có `sdkconfig` cũ thì xóa file `sdkconfig` (hoặc `sdkconfig.esp32c3`) để nhận mặc định mới |
| Bảng LED mới (mục 3.2) | Không còn đỏ/xanh sáng liên tục theo ngón tay; đang đeo + có app: xanh chớp mỗi 5 giây |
| WIFI_PROV op `0x03` | App bản mới tự ghi khóa sau khi gửi WiFi. App cũ vẫn dùng được (chỉ không có chế độ WiFi) |
| Chế độ WiFi (mục 3.5, 4.4) | Cần server đã cập nhật và đã chạy migration; xem `docs/` của repo web |
| LIVE 15 giây thay cho LIVE mỗi giây (đặc tả BLE v2) | App bản mới hiện "Đo lúc hh:mm:ss" và chuyển số lên server; app cũ vẫn đọc được số |
| DIAG + nhật ký BLE (mục 3.6) | **Xóa file `sdkconfig`** trước khi build để có % CPU; server cần migration `0014` |
| Nhịp giây cố định (không trôi 40 ms mỗi giây) | Phút đủ 60 giây, `valid` lên được 1,00 (log cũ tối đa 0,98) |

Khi gửi lại kết quả thử chế độ WiFi, kèm log từ lúc `-> STANDALONE` đến dòng `uplink: xong`.
