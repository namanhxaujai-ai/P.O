#include <WiFi.h>
#include <WiFiManager.h>   
#include <WebSocketMCP.h>
#include <ArduinoJson.h>
#include <WiFiMulti.h>       // thử kết nối lần lượt nhiều WiFi đã lưu
#include <Preferences.h>     // lưu danh sách WiFi vào bộ nhớ flash (NVS)
#define CHAN_LED_TRANG 25
#define CHAN_LED_VANG  26
#define CHAN_CAM_BIEN  33     // T8 - chân touch

// ================== CẤU HÌNH MCP ==================
const char* diaChiMCP = "wss://api.xiaozhi.me/mcp/?token=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VySWQiOjgwMzE0MywiYWdlbnRJZCI6MjM1NTQwNywiZW5kcG9pbnRJZCI6ImFnZW50XzIzNTU0MDciLCJwdXJwb3NlIjoibWNwLWVuZHBvaW50IiwiaWF0IjoxNzg5ODg1NzAwLCJleHAiOjE4MjE0NDMzMDB9.cZIiKHG18TDYSRJxGKHDJkTNu9dS-knOmpNshtrcL5XY8bW5HDK3_IAX5cR4ZyBpa-6J6Ejl571FYA1wpR7PIA";

WebSocketMCP doiTuongMCP;
WiFiManager quanLyWifi;

// ================== CẤU HÌNH PWM / ĐỘ SÁNG ==================
const int KENH_PWM_TRANG   = 0;       // chỉ dùng cho core ESP32 2.x
const int KENH_PWM_VANG    = 1;       // chỉ dùng cho core ESP32 2.x
const int TAN_SO_PWM       = 5000; 
const int DO_PHAN_GIAI_PWM = 12;    
const int PWM_TOI_DA       = (1 << DO_PHAN_GIAI_PWM) - 1;

const int   DO_SANG_TOI_THIEU = 20;   // giảm tối đa xuống 20%
const int   DO_SANG_TOI_DA    = 100;  // tăng tối đa lên 100%
const float HE_SO_GAMMA       = 1.0;

int doSang = DO_SANG_TOI_DA;          // độ sáng hiện tại (%), dùng chung cho cả 2 màu

// ================== TRẠNG THÁI ĐÈN ==================
enum TrangThaiDen { DEN_TAT, DEN_TRANG, DEN_VANG };
TrangThaiDen trangThaiHienTai = DEN_TAT;

void khoiTaoPWM() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(CHAN_LED_TRANG, TAN_SO_PWM, DO_PHAN_GIAI_PWM);
  ledcAttach(CHAN_LED_VANG,  TAN_SO_PWM, DO_PHAN_GIAI_PWM);
#else
  ledcSetup(KENH_PWM_TRANG, TAN_SO_PWM, DO_PHAN_GIAI_PWM);
  ledcAttachPin(CHAN_LED_TRANG, KENH_PWM_TRANG);
  ledcSetup(KENH_PWM_VANG, TAN_SO_PWM, DO_PHAN_GIAI_PWM);
  ledcAttachPin(CHAN_LED_VANG, KENH_PWM_VANG);
#endif
}

void ghiPWM(int chan, int kenh, int giaTri) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(chan, giaTri);
#else
  ledcWrite(kenh, giaTri);
#endif
}
int tinhDutyPWM(int phanTram) {
  float tyLe = powf(phanTram / 100.0f, HE_SO_GAMMA);
  return (int)(tyLe * PWM_TOI_DA + 0.5f);
}

// LED là anode chung -> mức thấp = sáng, nên duty phải ĐẢO
// Tắt hẳn = giữ ở mức cao liên tục (PWM_TOI_DA)
void capNhatDen() {
  int duty = tinhDutyPWM(doSang);
  int giaTriTrang = (trangThaiHienTai == DEN_TRANG) ? (PWM_TOI_DA - duty) : PWM_TOI_DA;
  int giaTriVang  = (trangThaiHienTai == DEN_VANG)  ? (PWM_TOI_DA - duty) : PWM_TOI_DA;
  ghiPWM(CHAN_LED_TRANG, KENH_PWM_TRANG, giaTriTrang);
  ghiPWM(CHAN_LED_VANG,  KENH_PWM_VANG,  giaTriVang);
}

void datTrangThaiDen(TrangThaiDen trangThai) {
  trangThaiHienTai = trangThai;
  capNhatDen();

  const char* ten = (trangThai == DEN_TRANG) ? "TRẮNG" : (trangThai == DEN_VANG) ? "VÀNG" : "TẮT";
  Serial.printf("[LED] Trạng thái: %s | độ sáng: %d%%\n", ten, doSang);
}

// ================== LƯU NHIỀU WIFI ==================
const int      SO_WIFI_TOI_DA         = 5;
const uint32_t THOI_GIAN_CHO_PORTAL_S = 60;   // portal cấu hình tự đóng sau 1 phút nếu không ai thao tác

Preferences boNho;
WiFiMulti wifiMulti;

// Đọc danh sách (thứ tự: cũ nhất -> mới nhất), trả về số mạng đang lưu
int docDanhSachWifi(String ssid[], String matKhau[]) {
  boNho.begin("wifi", true);
  int n = boNho.getUChar("n", 0);
  if (n > SO_WIFI_TOI_DA) n = SO_WIFI_TOI_DA;
  for (int i = 0; i < n; i++) {
    ssid[i]    = boNho.getString(("s" + String(i)).c_str(), "");
    matKhau[i] = boNho.getString(("p" + String(i)).c_str(), "");
  }
  boNho.end();
  return n;
}

int soWifiDaLuu() {
  String ssid[SO_WIFI_TOI_DA], matKhau[SO_WIFI_TOI_DA];
  return docDanhSachWifi(ssid, matKhau);
}

void luuWifi(const String& ssidMoi, const String& matKhauMoi) {
  if (ssidMoi.length() == 0) return;

  String ssid[SO_WIFI_TOI_DA], matKhau[SO_WIFI_TOI_DA];
  int n = docDanhSachWifi(ssid, matKhau);

  // Bỏ mạng trùng tên (sẽ được ghi lại ở cuối danh sách với mật khẩu mới)
  int m = 0;
  for (int i = 0; i < n; i++) {
    if (ssid[i] != ssidMoi) {
      ssid[m] = ssid[i];
      matKhau[m] = matKhau[i];
      m++;
    }
  }
  // Danh sách đầy -> bỏ mạng cũ nhất
  if (m >= SO_WIFI_TOI_DA) {
    for (int i = 1; i < m; i++) {
      ssid[i - 1] = ssid[i];
      matKhau[i - 1] = matKhau[i];
    }
    m--;
  }
  ssid[m] = ssidMoi;
  matKhau[m] = matKhauMoi;
  m++;

  boNho.begin("wifi", false);
  boNho.clear();
  boNho.putUChar("n", m);
  for (int i = 0; i < m; i++) {
    boNho.putString(("s" + String(i)).c_str(), ssid[i]);
    boNho.putString(("p" + String(i)).c_str(), matKhau[i]);
  }
  boNho.end();

  Serial.printf("[WiFi] Đã lưu mạng \"%s\" (hiện có %d mạng trong danh sách)\n", ssidMoi.c_str(), m);
}

// Thử kết nối lần lượt các mạng đã lưu, mạng nào đang phát sóng thì vào mạng đó
bool ketNoiWifiDaLuu(unsigned long thoiGianChoMs) {
  String ssid[SO_WIFI_TOI_DA], matKhau[SO_WIFI_TOI_DA];
  int n = docDanhSachWifi(ssid, matKhau);
  if (n == 0) return false;

  WiFi.mode(WIFI_STA);
  wifiMulti.APlistClean();
  for (int i = 0; i < n; i++) {
    wifiMulti.addAP(ssid[i].c_str(), matKhau[i].length() ? matKhau[i].c_str() : NULL);
  }

  Serial.printf("[WiFi] Thử kết nối trong %d mạng đã lưu...\n", n);
  unsigned long batDau = millis();
  while (millis() - batDau < thoiGianChoMs) {
    if (wifiMulti.run(5000) == WL_CONNECTED) {
      Serial.println("[WiFi] Đã kết nối: " + WiFi.SSID());
      return true;
    }
    delay(200);
  }
  return false;
}

// Lần đầu chạy: nếu thiết bị đang nhớ 1 WiFi từ phiên bản cũ thì chuyển vào danh sách,
// để khi cấu hình thêm mạng mới thì mạng cũ đó không bị mất.
void dongBoWifiCu() {
  if (soWifiDaLuu() > 0) return;

  WiFi.mode(WIFI_STA);
  WiFi.begin();   // dùng thông tin WiFi mà thiết bị đã lưu từ trước (nếu có)
  unsigned long batDau = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - batDau < 8000) {
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Tìm thấy WiFi cũ đã lưu trên thiết bị -> đưa vào danh sách");
    luuWifi(WiFi.SSID(), WiFi.psk());
  }
}

// ================== XỬ LÝ CẢM BIẾN CHẠM ==================
const float TY_LE_KICH_HOAT = 0.75;   // giá trị < 75% nền  -> coi là "chạm"
const float TY_LE_NHA       = 0.85;   // giá trị > 85% nền  -> coi là "nhả" (hysteresis chống nhiễu)
const bool  IN_DEBUG_CAM_UNG = false; // đặt true để xem giá trị touch trên Serial Monitor cảu Ardino

float giaTriNen = 0;                  // giá trị nền khi không chạm

// Chống nhiễu (debounce)
const unsigned long DEBOUNCE_MS_CAM_UNG = 30;
bool dangKichHoatTho = false;         // đầu ra bộ so sánh hysteresis (chưa lọc nhiễu)
bool trangThaiThoTruoc = false;
unsigned long thoiDiemDoiTrangThaiTho = 0;

bool dangKichHoat = false;            // trạng thái CUỐI đã qua debounce
unsigned long thoiDiemBatDauKichHoat = 0;
unsigned long thoiDiemKichHoatCuoi = 0;
bool dangChoKichHoatLan2 = false;

// Chạm ngắn hơn mốc này = "chạm" (đổi màu / chạm đúp). Giữ từ mốc này trở lên = "giữ" (chỉnh độ sáng).
const unsigned long thoiGianKichHoatToiDa     = 500;
const unsigned long khoangThoiGianKichHoatDup = 700; // khoảng cách tối đa giữa 2 lần để tính "chạm đúp"

// --- Giữ để tăng/giảm độ sáng ---
const int           BUOC_DO_SANG           = 5;   // mỗi bước tăng/giảm 5%
const unsigned long CHU_KY_BUOC_DO_SANG_MS = 100; // giữ liên tục thì cứ 100ms đổi 1 bước -> 20%->100% mất khoảng 1.6 giây
const unsigned long CHAN_CHAM_SAU_GIU_MS   = 300; // sau khi thả tay khỏi thao tác giữ, bỏ qua các "chạm" giả trong 300ms
bool dangGiu = false;                 // đang trong thao tác giữ
bool giuBoQua = false;                // giữ nhưng đèn đang tắt -> không làm gì
bool huongTang = false;               // hướng đổi độ sáng của lần giữ hiện tại (true = tăng)
unsigned long thoiDiemBuocCuoi = 0;
unsigned long thoiDiemKetThucGiu = 0;

// Đọc touch, lấy trung bình 4 lần để giảm nhiễu
int docCamUng() {
  uint32_t tong = 0;
  for (int i = 0; i < 4; i++) tong += touchRead(CHAN_CAM_BIEN);
  return tong / 4;
}

// Đo giá trị nền (lúc không chạm)
void hieuChinhCamUng() {
  uint32_t tong = 0;
  const int soMau = 30;
  for (int i = 0; i < soMau; i++) {
    tong += docCamUng();
    delay(10);
  }
  giaTriNen = (float)tong / soMau;
  Serial.printf("[TOUCH] Giá trị nền: %.1f | ngưỡng chạm < %.1f | ngưỡng nhả > %.1f\n",
                giaTriNen, giaTriNen * TY_LE_KICH_HOAT, giaTriNen * TY_LE_NHA);
}

void khiKichHoatMotLan() {
  Serial.println("[CHẠM] Phát hiện 1 lần -> đổi màu đèn");
  // 1 lần chạm: chuyển vòng OFF -> TRẮNG -> VÀNG -> OFF ...
  switch (trangThaiHienTai) {
    case DEN_TAT:   datTrangThaiDen(DEN_TRANG); break;
    case DEN_TRANG: datTrangThaiDen(DEN_VANG);  break;
    case DEN_VANG:  datTrangThaiDen(DEN_TAT);   break;
  }
}

void vaoCheDoCauHinhWifi() {
  Serial.println("[WiFi] Chạm 2 lần liên tiếp -> Vào chế độ cấu hình WiFi...");

  // KHÔNG xoá WiFi cũ: mạng mới sẽ được THÊM vào danh sách đã lưu.
  // Có thời gian chờ để nếu không cấu hình thì tự quay về các WiFi cũ.
  quanLyWifi.setConfigPortalTimeout(THOI_GIAN_CHO_PORTAL_S);
  bool thanhCong = quanLyWifi.startConfigPortal("ESP32-LED-Config"); // tạo Access Point để cấu hình

  if (thanhCong) {
    Serial.println("[WiFi] Đã kết nối mạng mới thành công.");
    luuWifi(WiFi.SSID(), WiFi.psk());
    datTrangThaiDen(DEN_TRANG);
    delay(500);
  } else {
    Serial.println("[WiFi] Không thêm được mạng mới (hết thời gian chờ) -> kết nối lại WiFi đã lưu.");
    ketNoiWifiDaLuu(15000);
  }
  datTrangThaiDen(DEN_TAT);

  // Sau khi quay lại: đo lại nền và xoá trạng thái chạm cũ
  hieuChinhCamUng();
  dangKichHoatTho = false;
  trangThaiThoTruoc = false;
  dangKichHoat = false;
  dangChoKichHoatLan2 = false;
  dangGiu = false;
}

void khiKichHoatHaiLan() {
  Serial.println("[CHẠM] Phát hiện 2 lần liên tiếp -> gọi vaoCheDoCauHinhWifi()");
  vaoCheDoCauHinhWifi();
}

// Gọi 1 lần khi vừa nhận ra thao tác "giữ"
void khiBatDauGiu() {
  giuBoQua = false;

  // Nếu trước đó có 1 lần chạm đang chờ (chưa kịp xác nhận là đơn/đúp) thì chốt nó là chạm đơn
  if (dangChoKichHoatLan2) {
    dangChoKichHoatLan2 = false;
    khiKichHoatMotLan();
  }

  if (trangThaiHienTai == DEN_TAT) {
    giuBoQua = true;
    Serial.println("[GIỮ] Đèn đang tắt -> bỏ qua (chạm 1 lần để bật đèn trước)");
    return;
  }

  // Chọn hướng: chạm mốc thì tự đảo; ở giữa thì lần giữ này ngược với lần trước
  if (doSang >= DO_SANG_TOI_DA)        huongTang = false;
  else if (doSang <= DO_SANG_TOI_THIEU) huongTang = true;
  else                                  huongTang = !huongTang;

  thoiDiemBuocCuoi = millis() - CHU_KY_BUOC_DO_SANG_MS; // bước đầu tiên xảy ra ngay khi bắt đầu giữ
  Serial.printf("[GIỮ] Bắt đầu %s độ sáng (hiện tại %d%%)\n", huongTang ? "TĂNG" : "GIẢM", doSang);
}

// Gọi liên tục khi đang giữ
void khiDangGiu() {
  if (giuBoQua) return;
  if (millis() - thoiDiemBuocCuoi < CHU_KY_BUOC_DO_SANG_MS) return;
  thoiDiemBuocCuoi = millis();

  // Căn về bội số của BUOC_DO_SANG (vd đang 33% do MCP đặt: tăng -> 35%, giảm -> 30%)
  int doSangMoi = huongTang
                    ? ((doSang / BUOC_DO_SANG) + 1) * BUOC_DO_SANG
                    : (((doSang + BUOC_DO_SANG - 1) / BUOC_DO_SANG) - 1) * BUOC_DO_SANG;
  doSangMoi = constrain(doSangMoi, DO_SANG_TOI_THIEU, DO_SANG_TOI_DA);
  if (doSangMoi != doSang) {
    doSang = doSangMoi;
    capNhatDen();
  }
}

void xuLyCamUng() {
  int giaTri = docCamUng();

  if (IN_DEBUG_CAM_UNG) {
    Serial.printf("touch=%d  nen=%.1f\n", giaTri, giaTriNen);
  }

  float nguongKichHoat = giaTriNen * TY_LE_KICH_HOAT;
  float nguongNha      = giaTriNen * TY_LE_NHA;
  if (!dangKichHoatTho && giaTri < nguongKichHoat) {
    dangKichHoatTho = true;
  } else if (dangKichHoatTho && giaTri > nguongNha) {
    dangKichHoatTho = false;
  }
  if (!dangKichHoatTho && !dangKichHoat) {
    giaTriNen = giaTriNen * 0.995f + giaTri * 0.005f;
  }
  if (dangKichHoatTho != trangThaiThoTruoc) {
    thoiDiemDoiTrangThaiTho = millis();
  }
  trangThaiThoTruoc = dangKichHoatTho;

  if ((millis() - thoiDiemDoiTrangThaiTho) > DEBOUNCE_MS_CAM_UNG) {
    if (dangKichHoatTho != dangKichHoat) {
      dangKichHoat = dangKichHoatTho;

      if (dangKichHoat) {
        thoiDiemBatDauKichHoat = millis();
      } else {
        unsigned long thoiGianKichHoat = millis() - thoiDiemBatDauKichHoat;
        bool vuaKetThucGiu = (millis() - thoiDiemKetThucGiu) < CHAN_CHAM_SAU_GIU_MS;
        // Chỉ tính là "chạm" nếu đủ ngắn và không phải nhiễu ngay sau một lần giữ
        if (thoiGianKichHoat < thoiGianKichHoatToiDa && !vuaKetThucGiu) {
          if (dangChoKichHoatLan2 && (millis() - thoiDiemKichHoatCuoi) < khoangThoiGianKichHoatDup) {
            dangChoKichHoatLan2 = false;
            khiKichHoatHaiLan();
          } else {
            dangChoKichHoatLan2 = true;
            thoiDiemKichHoatCuoi = millis();
          }
        }
      }
    }
  }

  //Giữ -> tăng/giảm độ sáng ---
  if (dangKichHoat) {
    if ((millis() - thoiDiemBatDauKichHoat) >= thoiGianKichHoatToiDa) {
      if (!dangGiu) {
        dangGiu = true;
        khiBatDauGiu();
      }
      khiDangGiu();
    }
  } else if (dangGiu) {
    dangGiu = false;
    thoiDiemKetThucGiu = millis();
    if (!giuBoQua) Serial.printf("[GIỮ] Kết thúc, độ sáng: %d%%\n", doSang);
  }

  // Nếu đã chạm 1 lần mà chờ quá lâu không có lần 2 -> coi là 1 lần đơn
  if (dangChoKichHoatLan2 && (millis() - thoiDiemKichHoatCuoi) > khoangThoiGianKichHoatDup) {
    dangChoKichHoatLan2 = false;
    khiKichHoatMotLan();
  }
}

// ================== MCP ==================
void khiTrangThaiKetNoi(bool daKetNoi) {
  if (daKetNoi) {
    Serial.println("[MCP] ✅ Đã kết nối tới máy chủ");
    dangKyCongCuMCP();
  } else {
    Serial.println("[MCP] ⚠️ Mất kết nối với máy chủ MCP");
  }
}

void dangKyCongCuMCP() {
  // Tool điều khiển đèn: white / yellow / off, kèm độ sáng (tuỳ chọn) 20-100
  doiTuongMCP.registerTool(
    "led_control",
    "Điều khiển đèn LED 2 màu trên ESP32: bật trắng, bật vàng, hoặc tắt. Có thể kèm độ sáng 20-100 (%)",
    "{\"type\":\"object\",\"properties\":{\"mode\":{\"type\":\"string\",\"enum\":[\"white\",\"yellow\",\"off\"],\"description\":\"Trạng thái đèn cần đặt\"},\"brightness\":{\"type\":\"integer\",\"minimum\":20,\"maximum\":100,\"description\":\"Độ sáng (%), từ 20 đến 100\"}},\"required\":[\"mode\"]}",
    [](const String& thamSo) {
      DynamicJsonDocument taiLieu(256);
      deserializeJson(taiLieu, thamSo);
      String cheDo = taiLieu["mode"].as<String>();

      if (taiLieu["brightness"].is<int>()) {
        doSang = constrain(taiLieu["brightness"].as<int>(), DO_SANG_TOI_THIEU, DO_SANG_TOI_DA);
      }

      if (cheDo == "white") datTrangThaiDen(DEN_TRANG);
      else if (cheDo == "yellow") datTrangThaiDen(DEN_VANG);
      else datTrangThaiDen(DEN_TAT);

      String phanHoi = "{\"success\":true,\"state\":\"" + cheDo + "\",\"brightness\":" + String(doSang) + "}";
      return WebSocketMCP::ToolResponse(phanHoi);
    }
  );

  // Tool đọc trạng thái hiện tại
  doiTuongMCP.registerTool(
    "led_get_state",
    "Lấy trạng thái hiện tại của đèn LED (white/yellow/off) và độ sáng (20-100)",
    "{\"type\":\"object\",\"properties\":{}}",
    [](const String& thamSo) {
      String chuoiTrangThai = (trangThaiHienTai == DEN_TRANG) ? "white"
                             : (trangThaiHienTai == DEN_VANG) ? "yellow" : "off";
      String phanHoi = "{\"state\":\"" + chuoiTrangThai + "\",\"brightness\":" + String(doSang) + "}";
      return WebSocketMCP::ToolResponse(phanHoi);
    }
  );

  Serial.println("[MCP] 🛠️ Đã đăng ký tool điều khiển đèn LED 2 màu");
}

// ================== SETUP / LOOP ==================
void setup() {
  Serial.begin(115200);

  khoiTaoPWM();                 // thay cho pinMode(OUTPUT) vì giờ điều khiển độ sáng bằng PWM
  datTrangThaiDen(DEN_TAT);

  // Chân touch KHÔNG cần pinMode / pull-up. Đo giá trị nền ngay (đừng chạm lúc này).
  delay(300);
  hieuChinhCamUng();

  // Kết nối WiFi:
  Serial.println("Đang kết nối WiFi...");
  dongBoWifiCu();
  bool daKetNoi = (WiFi.status() == WL_CONNECTED) || ketNoiWifiDaLuu(15000);
  if (!daKetNoi && soWifiDaLuu() == 0) {
    quanLyWifi.setConfigPortalTimeout(THOI_GIAN_CHO_PORTAL_S);
    daKetNoi = quanLyWifi.autoConnect("ESP32-LED-Config");
    if (daKetNoi) luuWifi(WiFi.SSID(), WiFi.psk());
  }

  if (daKetNoi) {
    Serial.println("\n✅ WiFi đã kết nối");
    Serial.println("IP thiết bị: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n⚠️ Không thể kết nối WiFi, tiếp tục chạy ở chế độ ngoại tuyến.");
  }

  // Bắt đầu MCP
  doiTuongMCP.begin(diaChiMCP, khiTrangThaiKetNoi);
}

void loop() {
  doiTuongMCP.loop();
  xuLyCamUng();
  delay(10);
}
