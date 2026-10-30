// 第7回 IoT特論：Arduino連携ピンポンゲーム（Arduino側）
// 入力：可変抵抗（A0）・ボタン（D2）／出力：LED×3（D3・D4・D5）・ブザー（D8）
// Arduino → Processing : "P,値"（可変抵抗 0〜1023） / "B,1"（ボタンが押された）
// Processing → Arduino : 1文字のコマンド
//   S=スタート  H=打ち返した  M=ミス  C=クリア  G=ゲームオーバー
//   P=一時停止／再開（押すたびに切り替え）  R=再開

const int POT_PIN    = A0;           // 可変抵抗
const int BTN_PIN    = 2;            // ボタン（もう片方はGND）
const int LED_PINS[3] = {3, 4, 5};   // ライフ表示のLED（左から①②③）
const int BUZZER_PIN = 8;            // ブザー

// ---- 可変抵抗 ----
int lastPot = -1;
unsigned long lastPotSend = 0;

// ---- ボタン（チャタリング対策） ----
int btnStable   = HIGH;              // 確定した状態（押していないとHIGH）
int btnLastRead = HIGH;
unsigned long btnChangedAt = 0;

// ---- ライフとLEDの点滅 ----
int lives = 3;
int blinkLed = -1;                   // 点滅させるLED（-1なら点滅なし）
int blinkToggles = 0;                // 残りの切り替え回数
bool blinkOn = false;
unsigned long blinkAt = 0;

// ---- 音の高さ（Hz）※A4・A5はアナログピン名とかぶるので NOTE_ を付けている ----
const int C4 = 262, D4 = 294, E4 = 330, F4 = 349, G4 = 392, NOTE_A4 = 440;
const int C5 = 523, E5 = 659, G5 = 784, NOTE_A5 = 880, C6 = 1047;

// ---- 効果音（BGMより優先して鳴らす） ----
const int MAX_NOTES = 8;
int melNotes[MAX_NOTES];
int melDurs[MAX_NOTES];
int melLen = 0;
int melIdx = 0;
unsigned long melNextAt = 0;

// ---- BGM（プレイ中に繰り返す。効果音が鳴っている間は休む） ----
const int BGM_LEN = 16;
const int bgmNotes[BGM_LEN] = {C4, E4, G4, E4,  F4, NOTE_A4, C5, NOTE_A4,
                               G4, E4, C4, E4,  D4, F4, G4, 0};   // 0は休符
const int BGM_STEP = 180;            // 1音の長さ（ミリ秒）
bool bgmOn = false;
int bgmIdx = 0;
unsigned long bgmNextAt = 0;

void setup() {
  Serial.begin(9600);
  pinMode(BTN_PIN, INPUT_PULLUP);    // 内部の抵抗を使うので外付けの抵抗は不要
  for (int i = 0; i < 3; i++) pinMode(LED_PINS[i], OUTPUT);
  showLives(3);
}

void loop() {
  sendPot();
  checkButton();
  readCommands();
  updateSound();
  updateBlink();
}

// ---- 可変抵抗の値を送る（変わったときだけ、約20msごと） ----
void sendPot() {
  if (millis() - lastPotSend < 20) return;
  lastPotSend = millis();
  int value = analogRead(POT_PIN);
  if (abs(value - lastPot) > 2) {
    Serial.print("P,");
    Serial.println(value);
    lastPot = value;
  }
}

// ---- ボタンが押された瞬間だけ "B,1" を送る ----
void checkButton() {
  int reading = digitalRead(BTN_PIN);
  if (reading != btnLastRead) {        // 変化したら時刻を記録
    btnChangedAt = millis();
    btnLastRead = reading;
  }
  // 30ms 同じ状態が続いたら確定（ボタンのバタつきを無視する）
  if (millis() - btnChangedAt > 30 && reading != btnStable) {
    btnStable = reading;
    if (btnStable == LOW) Serial.println("B,1");
  }
}

// ---- Processingからのコマンドを処理する ----
void readCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    switch (c) {
      case 'S': {                       // スタート：ライフ3、スタート音のあとBGM開始
        lives = 3; blinkLed = -1; showLives(lives);
        int n[] = {C5, E5, G5}; int d[] = {100, 100, 200};
        play(n, d, 3);
        bgmOn = true; bgmIdx = 0; bgmNextAt = 0;
        break;
      }
      case 'H': {                       // 打ち返した：短い高い音
        int n[] = {NOTE_A5}; int d[] = {50};
        play(n, d, 1);
        break;
      }
      case 'M': {                       // ミス：低い音＋消えるLEDを点滅
        if (lives > 0) { lives--; startBlink(lives); }
        int n[] = {150}; int d[] = {250};
        play(n, d, 1);
        break;
      }
      case 'C': {                       // クリア：BGMを止めてクリアのメロディ
        bgmOn = false;
        int n[] = {C5, E5, G5, C6, 0, G5, C6}; int d[] = {120, 120, 120, 250, 80, 120, 400};
        play(n, d, 7);
        break;
      }
      case 'G': {                       // ゲームオーバー：BGMを止めて下がる音、LED全部消灯
        bgmOn = false;
        lives = 0; blinkLed = -1; showLives(0);
        int n[] = {G4, E4, C4}; int d[] = {250, 250, 500};
        play(n, d, 3);
        break;
      }
      case 'P': {                       // 一時停止／再開：押すたびにBGMを止める・再開する
        bgmOn = !bgmOn;
        int n[] = {C6}; int d[] = {60};
        play(n, d, 1);
        break;
      }
      case 'R': {                       // 再開：BGMを続きから鳴らす
        bgmOn = true;
        int n[] = {C6}; int d[] = {60};
        play(n, d, 1);
        break;
      }
    }
  }
}

// ---- LED ----
void showLives(int n) {
  for (int i = 0; i < 3; i++) digitalWrite(LED_PINS[i], i < n ? HIGH : LOW);
}

void startBlink(int index) {           // index のLEDを3回点滅させてから消す
  blinkLed = index;
  blinkToggles = 6;
  blinkOn = true;
  blinkAt = millis();
}

void updateBlink() {
  if (blinkLed < 0) return;
  if (millis() - blinkAt < 150) return;
  blinkAt = millis();
  blinkOn = !blinkOn;
  digitalWrite(LED_PINS[blinkLed], blinkOn ? HIGH : LOW);
  if (--blinkToggles <= 0) {
    blinkLed = -1;
    showLives(lives);
  }
}

// ---- 音（効果音を優先し、鳴っていないときだけBGM） ----
void play(int notes[], int durs[], int n) {
  melLen = min(n, MAX_NOTES);
  for (int i = 0; i < melLen; i++) { melNotes[i] = notes[i]; melDurs[i] = durs[i]; }
  melIdx = 0;
  melNextAt = millis();
}

void updateSound() {
  unsigned long now = millis();

  // 効果音が残っていれば、それを鳴らす
  if (melIdx < melLen) {
    if ((long)(now - melNextAt) < 0) return;
    int f = melNotes[melIdx];
    int d = melDurs[melIdx];
    if (f > 0) tone(BUZZER_PIN, f, (unsigned long)d * 9 / 10);   // 音と音の間に少しすき間
    else       noTone(BUZZER_PIN);                               // 0 は休符
    melNextAt = now + d;
    melIdx++;
    return;
  }
  // 効果音の最後の音が鳴り終わるまで待つ
  if ((long)(now - melNextAt) < 0) return;

  // BGM
  if (!bgmOn) return;
  if ((long)(now - bgmNextAt) < 0) return;
  int f = bgmNotes[bgmIdx];
  if (f > 0) tone(BUZZER_PIN, f, BGM_STEP * 7 / 10);   // 少し短く切ってリズムを出す
  else       noTone(BUZZER_PIN);
  bgmNextAt = now + BGM_STEP;
  bgmIdx = (bgmIdx + 1) % BGM_LEN;
}

