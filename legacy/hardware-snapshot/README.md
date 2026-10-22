# Bản gốc của nhóm phần cứng

Các file nhóm phần cứng đã nạp để chạy thử model và thu thập dữ liệu, giữ nguyên để đối chiếu.
Không nằm trong build. `CMakeLists.txt` ở đây trỏ tới `../../..` vì ban đầu nằm trong
`SPO2-RESEARCH/respiratory-risk-classifier/firmware/examples/esp32c3/main/`.

| File gốc | Đã chuyển tới |
|---|---|
| `main.cc` (xử lý PPG) | `main/sensor/ppg.c` (giữ nguyên thuật toán) |
| `main.cc` (bộ gộp phút) | `main/sensor/vitals.c` |
| `main.cc` (self-test, vòng lặp phút) | `main/app_main.cc` |
| `main.cc` (LED đỏ/xanh) | `main/system/led_status.c` (chế độ "bình thường") |
| `max30102.c`, `max30102.h` | `components/max30102/` (chân lấy từ Kconfig) |

Xóa thư mục này khi không cần đối chiếu nữa.
