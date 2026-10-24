// 問1-2（拡張版）：リモコンでLEDマトリクスの静止画・アニメーション・文字・明るさを切り替える
#include <LedControl.h>
#include <IRremote.h>

// ---- ピン配置 ----
const int DIN_PIN = 12, CLK_PIN = 11, CS_PIN = 10;
const int IR_PIN  = 2;   // 11番はCLKと重なるので2番を使う
LedControl lc = LedControl(DIN_PIN, CLK_PIN, CS_PIN, 1);

// ---- リモコンのボタンのコード（ELEGOOリモコン）----
const uint8_t BTN_0 = 0x16, BTN_1 = 0x0C, BTN_2 = 0x18, BTN_3 = 0x5E, BTN_4 = 0x08;
const uint8_t BTN_5 = 0x1C, BTN_6 = 0x5A, BTN_7 = 0x42, BTN_8 = 0x52, BTN_9 = 0x4A;
const uint8_t BTN_VOL_UP = 0x46, BTN_VOL_DOWN = 0x15;

// ---- 静止画パターン（1行=8ビット、1が点灯）----
const byte HEART[8]   = {B00000000,B01100110,B11111111,B11111111,B01111110,B00111100,B00011000,B00000000};
const byte HEART_S[8] = {B00000000,B00000000,B00100100,B01111110,B00111100,B00011000,B00000000,B00000000};
const byte SMILE[8]   = {B00111100,B01000010,B10100101,B10000001,B10100101,B10011001,B01000010,B00111100};
const byte WINK[8]    = {B00111100,B01000010,B10000101,B10000001,B10100101,B10011001,B01000010,B00111100};
const byte ARROW[8]   = {B00011000,B00111100,B01111110,B11011011,B00011000,B00011000,B00011000,B00011000};
const byte CHECK[8]   = {B00000000,B00000001,B00000011,B10000110,B11001100,B01111000,B00110000,B00000000};

// ---- 文字フォント（1文字=横5列、1列=縦8ビット、上の行が最上位ビット）----
const byte FONT_A[5] = {B00111110,B01001000,B01001000,B01001000,B00111110};
const byte FONT_C[5] = {B00111100,B01000010,B01000010,B01000010,B00100100};
const byte FONT_D[5] = {B01111110,B01000010,B01000010,B01000010,B00111100};
const byte FONT_E[5] = {B01111110,B01010010,B01010010,B01010010,B01000010};
const byte FONT_H[5] = {B01111110,B00010000,B00010000,B00010000,B01111110};
const byte FONT_I[5] = {B01000010,B01000010,B01111110,B01000010,B01000010};
const byte FONT_K[5] = {B01111110,B00011000,B00100100,B01000010,B10000001};
const byte FONT_L[5] = {B01111110,B00000010,B00000010,B00000010,B00000010};
const byte FONT_N[5] = {B01111110,B00100000,B00011000,B00000100,B01111110};
const byte FONT_O[5] = {B00111100,B01000010,B01000010,B01000010,B00111100};
const byte FONT_R[5] = {B01111110,B01001000,B01001000,B01001100,B00110010};
const byte FONT_S[5] = {B00100010,B01010010,B01010010,B01010010,B01001100};
const byte FONT_U[5] = {B01111100,B00000010,B00000010,B00000010,B01111100};
const byte FONT_V[5] = {B01110000,B00001100,B00000010,B00001100,B01110000};
const byte FONT_W[5] = {B01111110,B00000010,B00001100,B00000010,B01111110};

// 文字に対応するフォントを返す（小文字は大文字として扱う。未対応の文字は空白扱い）
const byte* glyph(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
  switch (ch) {
    case 'A': return FONT_A; case 'C': return FONT_C; case 'D': return FONT_D;
    case 'E': return FONT_E; case 'H': return FONT_H; case 'I': return FONT_I;
    case 'K': return FONT_K; case 'L': return FONT_L; case 'N': return FONT_N;
    case 'O': return FONT_O; case 'R': return FONT_R; case 'S': return FONT_S;
    case 'U': return FONT_U; case 'V': return FONT_V; case 'W': return FONT_W;
    default:  return nullptr;
  }
}

// ---- 状態 ----
enum Mode { MODE_OFF, MODE_STATIC, MODE_BEAT, MODE_WINK, MODE_RAIN, MODE_TEXT };
Mode mode = MODE_OFF;
int brightness = 7;              // 明るさ（1〜15、2刻みで上下するので奇数から始める）
unsigned long lastFrame = 0;     // 前回コマ送りした時刻
int frame = 0;                   // アニメーションのコマ番号
byte rain[8];                    // 雨の画面
const unsigned long BEAT_MS = 400, WINK_MS = 600, RAIN_MS = 120, TEXT_MS = 90;  // コマ送りの間隔
const int MAX_COLS = 120;        // スクロール用の列データの最大数
byte textCols[MAX_COLS];         // スクロール用の列データ
int textLen = 0;

// パターンを1行ずつマトリクスに書き込む
void showPattern(const byte p[8]) {
  for (int r = 0; r < 8; r++) lc.setRow(0, r, p[r]);
}

// 列データを1列追加する（配列からはみ出さないように確認する）
void addCol(byte col) {
  if (textLen < MAX_COLS) textCols[textLen++] = col;
}

// 文字列からスクロール用の列データを作る（前後に空白8列、文字の間に空白1列）
void buildText(const char* text) {
  textLen = 0;
  for (int i = 0; i < 8; i++) addCol(0);
  for (int c = 0; text[c] != '\0'; c++) {
    const byte* g = glyph(text[c]);
    if (g == nullptr) {                       // 空白は3列ぶん空ける
      for (int i = 0; i < 3; i++) addCol(0);
    } else {
      for (int i = 0; i < 5; i++) addCol(g[i]);
      addCol(0);
    }
  }
  for (int i = 0; i < 8; i++) addCol(0);
}

// モードを切り替えて、最初のコマから始める
// ※Arduino IDEの仕様で、引数をMode型にするとエラーになるためint型で受け取る
void startMode(int m) {
  mode = (Mode)m;
  frame = 0;
  lastFrame = 0;
  lc.clearDisplay(0);
  for (int r = 0; r < 8; r++) rain[r] = 0;
}

// 文字スクロールを開始する
void startText(const char* text) {
  buildText(text);
  startMode(MODE_TEXT);
}

// アニメーションを1コマ進める（時間が来たときだけ）
void updateAnimation() {
  unsigned long interval;
  switch (mode) {
    case MODE_BEAT: interval = BEAT_MS; break;
    case MODE_WINK: interval = WINK_MS; break;
    case MODE_RAIN: interval = RAIN_MS; break;
    case MODE_TEXT: interval = TEXT_MS; break;
    default: return;   // 静止画・消灯は何もしない
  }
  if (millis() - lastFrame < interval) return;
  lastFrame = millis();

  switch (mode) {
    case MODE_BEAT:   // 大きいハートと小さいハートを交互に
      showPattern(frame % 2 == 0 ? HEART : HEART_S);
      break;
    case MODE_WINK:   // スマイル3コマ、ウインク1コマ
      showPattern(frame % 4 == 3 ? WINK : SMILE);
      break;
    case MODE_RAIN:   // 1行ずつ下にずらし、いちばん上にランダムな雨粒を足す
      for (int r = 7; r > 0; r--) rain[r] = rain[r - 1];
      rain[0] = (random(3) == 0) ? (1 << random(8)) : 0;
      showPattern(rain);
      break;
    case MODE_TEXT: { // 8列分の窓を1列ずつ左へずらす（最後まで行ったら繰り返す）
      int start = frame % (textLen - 7);
      for (int c = 0; c < 8; c++) lc.setColumn(0, c, textCols[start + c]);
      break;
    }
  }
  frame++;
}

// リモコンのボタンに応じて処理を変える
void handleButton(uint8_t cmd) {
  switch (cmd) {
    case BTN_1: startMode(MODE_STATIC); showPattern(HEART); break;
    case BTN_2: startMode(MODE_STATIC); showPattern(SMILE); break;
    case BTN_3: startMode(MODE_STATIC); showPattern(ARROW); break;
    case BTN_4: startMode(MODE_STATIC); showPattern(CHECK); break;
    case BTN_5: startMode(MODE_BEAT); break;
    case BTN_6: startMode(MODE_WINK); break;
    case BTN_7: startMode(MODE_RAIN); break;
    case BTN_8: startText("HELLO WORLD"); break;
    case BTN_9: startText("ARDUINO CLASS"); break;
    case BTN_0: startMode(MODE_OFF); break;
    case BTN_VOL_UP:   brightness = min(15, brightness + 2); lc.setIntensity(0, brightness); break;
    case BTN_VOL_DOWN: brightness = max(1,  brightness - 2); lc.setIntensity(0, brightness); break;
  }
}

void setup() {
  Serial.begin(9600);
  lc.shutdown(0, false);
  lc.setIntensity(0, brightness);
  lc.clearDisplay(0);
  IrReceiver.begin(IR_PIN, ENABLE_LED_FEEDBACK);
  randomSeed(analogRead(A0));   // 雨の位置を毎回変える
}

void loop() {
  if (IrReceiver.decode()) {
    // NEC方式で、押し続けのリピートでない信号だけ扱う（ノイズ対策）
    if (IrReceiver.decodedIRData.protocol == NEC &&
        !(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) {
      uint8_t cmd = IrReceiver.decodedIRData.command;
      Serial.print("address: 0x");
      Serial.print(IrReceiver.decodedIRData.address, HEX);
      Serial.print(" command: 0x");
      Serial.println(cmd, HEX);
      handleButton(cmd);
    }
    IrReceiver.resume();
  }
  updateAnimation();
}
