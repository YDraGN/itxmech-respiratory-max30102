# Kiến trúc firmware vòng đeo RespiCare

Tài liệu này mô tả cách firmware chạy và cách nó nói chuyện với app. Định dạng từng byte BLE nằm ở
đặc tả chung `itxmech-respiratory-mobile-app/ITxMech_RespiratoryCare/docs/ble-gatt.md` (phiên bản 1).
Config và API server nằm ở `SPO2-RESEARCH/respiratory-risk-classifier/firmware/docs/CONFIG_VA_API.md`.

## 1. Các task

| Task | Ưu tiên | Việc |
|---|---|---|
| `sensor` | 5 | Đọc FIFO MAX30102 ~25 Hz, tính SpO2/HR (`sensor/ppg.c`). Mỗi giây: cộng vào bộ gộp phút, gửi LIVE, cập nhật LED đỏ/xanh |
| `main` (vòng lặp phút) | 1 | Áp config mới, chạy engine luật + model, ghi flash, gửi RECORDS / ASSESSMENT |
| NimBLE host | (stack) | Quảng bá, kết nối, đọc/ghi characteristic. Callback không chặn |
| `ble_tx` | 5 | Hàng đợi notify duy nhất: LIVE, RECORDS, ASSESSMENT, STATUS, BACKFILL |
| `uplink` | 4 | Chế độ tự chạy: thức dậy mỗi `upload_interval_min` hoặc khi có cảnh báo. Hiện chỉ ghi log, chưa gửi mạng |
| `led` | 3 | Vẽ LED theo chế độ (mục 4) |

Engine (`spo2tc_engine_t`) chỉ được sửa trong task phút. Config đến từ BLE được kiểm tra, lưu NVS rồi
đưa qua hàng đợi 1 phần tử (`system/wristband_config.c`), đúng mục 4 của CONFIG_VA_API.md.

## 2. Luồng dữ liệu

```text
MAX30102 ──I2C──► ppg ──mỗi giây──► vitals (gộp phút) ──mỗi phút──► engine luật + model
                    │                                                  │
                    └──► LIVE (BLE notify, chỉ hiển thị)               ▼
                                                     record_store (flash, seq không reset)
                                                        │                      │
                                         app gateway ◄──┘ RECORDS/ASSESSMENT   └──► uplink (STANDALONE, sẽ làm)
```

Bản ghi luôn được ghi flash **trước** khi gửi. Mất BLE giữa chừng thì dữ liệu vẫn còn để BACKFILL.

## 3. Kết nối và dự phòng

### 3.1 Máy trạng thái (`link/link_manager.c`)

```text
            khởi động / mất BLE
                    │
                    ▼
            ┌───────────────┐  SET_GATEWAY(1)   ┌─────────┐
            │  WAITING_APP  │ ────────────────► │ GATEWAY │
            │ LED nháy nhanh│                   │ LED 3 s │
            └───────────────┘ ◄──── mất BLE ─── └─────────┘
                    │                                │
     hết RESPI_APP_WAIT_S (60 s)               SET_GATEWAY(0)
                    ▼                                │
            ┌───────────────┐ ◄──────────────────────┘
            │  STANDALONE   │ ── SET_GATEWAY(1) ──► GATEWAY
            │ lưu flash, chờ│
            │ uplink        │
            └───────────────┘
```

- "Có app" nghĩa là app **đã bắt tay xong** (ghi `SET_GATEWAY(1)` ở bước 5 của đặc tả), không phải
  chỉ mới có kết nối BLE. Một điện thoại lạ kết nối mà không bắt tay thì vòng vẫn tính giờ chờ.
- Khi không có kết nối, vòng luôn quảng bá: 200 ms trong `RESPI_ADV_FAST_S` (120 s) đầu sau khi
  khởi động hoặc mất kết nối, sau đó 1 000 ms để tiết kiệm pin.
- App chủ động ngắt (đăng xuất, tắt vai trò gateway) gửi `SET_GATEWAY(0)` trước: vòng vào STANDALONE
  ngay, không chờ 60 s.

### 3.2 Trình tự khi app kết nối

Theo mục 4 của đặc tả: MTU 247 → đọc DEVICE_INFO → bật notify → `TIME_SYNC` → `SET_GATEWAY(1)` →
đọc STATUS → `BACKFILL(from)` → nhận RECORDS, lưu Room, `ACK(seq)`.

### 3.3 ACK và BACKFILL (đổi điện thoại)

- **ACK chỉ dời con trỏ `acked_seq`** (lưu NVS), không xóa flash. Bản ghi cũ chỉ mất khi vòng đệm
  ghi đè (khoảng 6 ngày dữ liệu). Vẫn đúng ràng buộc của đặc tả: không bao giờ xóa trước khi có ACK.
- **BACKFILL(from)** gửi lại mọi RECORDS (và ASSESSMENT) có `seq ≥ max(from, acked_seq + 1 − RESPI_BACKFILL_KEEP_ACKED)`.
  Mặc định giữ 1 440 bản ghi (24 giờ) đã ACK.
- Lý do: điện thoại A đã ACK nhưng chưa kịp gửi lên server rồi hết pin/mất mạng. Điện thoại B kết nối
  sau, xin BACKFILL từ seq cũ, vẫn lấy lại được dữ liệu đó. Server bỏ trùng theo `(device_id, seq)`.
- Gửi lại cùng seq là an toàn ở cả app (Room `IGNORE`) và server.

### 3.4 Thời gian

- Vòng không có RTC. Trước `TIME_SYNC`, `ts` là số giây từ lúc khởi động (< 1 600 000 000).
- Khi đã có giờ, các bản ghi **cùng lần khởi động** được đổi `ts` sang epoch lúc gửi đi.
- Bản ghi của lần khởi động trước mà chưa từng có giờ thì giữ nguyên `ts` nhỏ: app/server nên coi
  `ts < 1 600 000 000` là "không rõ giờ".
- LIVE chỉ gửi sau khi có giờ.

## 4. LED

| Chế độ | LED | Khi nào |
|---|---|---|
| Đang tìm BLE | Cả đỏ và xanh nháy nhanh (100 ms) | Khởi động và sau khi mất kết nối, tối đa `RESPI_LED_SEARCH_S` (60 s; 0 = nháy đến khi có app) |
| App kết nối thành công | Xanh nháy chậm (500 ms) trong 3 s | Ngay khi app ghi `SET_GATEWAY(1)` |
| Bình thường (như bản cũ) | Xanh = có tay / đang đeo, đỏ = không có tay | Sau hai chế độ trên |
| Lỗi cảm biến | Đỏ nháy 200 ms | Không đọc được `PART_ID` của MAX30102 |

Hết thời gian tìm BLE thì LED quay về chế độ bình thường để nhóm phần cứng vẫn thử cảm biến được khi
không có điện thoại, và để tiết kiệm pin. Vòng vẫn quảng bá BLE.

## 5. Lưu trữ

| Nơi | Nội dung |
|---|---|
| NVS `respi` | `boot_id`, `seq_floor`, `acked_seq`, blob `cfg` |
| Partition `reclog` (512 KB) | Vòng đệm entry 48 B: telemetry 16 B hoặc assessment 24 B, kèm `seq`, `boot_id`, `wcount`, CRC32 |

- `seq` = seq lớn nhất trong flash + 1, không nhỏ hơn `seq_floor` (ghi trước mỗi 64 bản ghi) để không
  dùng lại seq kể cả khi vùng log bị xóa.
- Entry ghi dở do mất điện có CRC sai và bị bỏ qua; đầu ghi tự tìm lại khi khởi động.
- Mỗi sector bị xóa khoảng 6 ngày một lần, độ bền flash không đáng lo.

## 6. Khác biệt so với đặc tả v1 (tương thích ngược)

| Điểm | Firmware làm | Ảnh hưởng tới app |
|---|---|---|
| BACKFILL | Gửi cả ASSESSMENT có `seq ≥ from` | App nhận thêm assessment cũ, Room bỏ trùng |
| ACK | Không xóa flash ngay | Không |
| STATUS pin | `0xFF` (mạch chưa có chân đo pin) | App cần hiển thị "không rõ" thay vì "255%" |
| CONTROL / CONFIG không hợp lệ | Lỗi ATT `0x80` | Như đặc tả |
| WIFI_PROV `a51c0009` (mới) | Nhận tối đa 3 mạng WiFi, lưu NVS, đọc lại chỉ trả SSID | App gửi WiFi xuống vòng (mục 7) |

## 7. Cài WiFi qua BLE (WIFI_PROV)

| Thao tác | Byte |
|---|---|
| Ghi: thêm / đổi mật khẩu | `01` · `ssid_len` · SSID · `pass_len` · mật khẩu (`pass_len = 0`: mạng mở) |
| Ghi: xóa hết | `02` |
| Đọc | `count` · (`ssid_len` · SSID) × count, **không** trả mật khẩu |

- SSID 1–32 byte, mật khẩu 0 hoặc 8–63 byte. Sai định dạng: lỗi ATT `0x80`.
- Tối đa 3 mạng; mạng thứ 4 đẩy mạng cũ nhất ra. Gửi lại SSID đã có thì thay mật khẩu.
- Gói dài tối đa 98 byte nên app phải xin MTU 247 trước (app đã làm ở bước 1).
- Hiện vòng chỉ **lưu** WiFi; việc kết nối và gửi lên server làm cùng `uplink_flush()`.
- Liên kết BLE chưa mã hóa (chưa bonding): mật khẩu WiFi đi dạng rõ qua sóng BLE. Chấp nhận khi demo, phải
  bật bonding trước khi dùng thật.
