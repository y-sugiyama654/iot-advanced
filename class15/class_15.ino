// コーヒー自動ドリップ（1人前ボタンのみ）
// 配線: D2=ボタン(GNDへ) / D3=緑LED / D4=黄LED / D5=ブザー / D8〜D11=ULN2003 IN1〜IN4
#include <Stepper.h>

const int PIN_BTN    = 2;
const int PIN_GREEN  = 3;
const int PIN_YELLOW = 4;
const int PIN_BUZZER = 5;

const int STEPS_PER_REV = 2048;        // 28BYJ-48 の1回転のステップ数
const int MOTOR_RPM     = 12;          // モーター毎分12回転 → 容器は毎分約2.4回転（5:1）
const unsigned long BREW_MS = 120000UL;    // 1人前の抽出時間 2分
// const unsigned long BREW_MS = 10000UL;  // デバッグ用：10秒で完成

// ULN2003 は IN1, IN3, IN2, IN4 の順で渡す
Stepper stepper(STEPS_PER_REV, 8, 10, 9, 11);

enum State { IDLE, BREWING, DONE };
State state = IDLE;
unsigned long startMs = 0;

// ---- ボタン（押した瞬間だけ true、チャタリング対策つき）----
bool lastStable = HIGH, lastRead = HIGH;
unsigned long lastChange = 0;
bool buttonPressed() {
  bool r = digitalRead(PIN_BTN);
  if (r != lastRead) { lastRead = r; lastChange = millis(); }
  if (millis() - lastChange > 30 && r != lastStable) {
    lastStable = r;
    if (r == LOW) return true;   // INPUT_PULLUP なので押すと LOW
  }
  return false;
}

// モーターのコイルの電流を切る（止まっている間の発熱を防ぐ）
void motorOff() {
  for (int p = 8; p <= 11; p++) digitalWrite(p, LOW);
}

// ---- 完成アラーム（止まらずに鳴らすノンブロッキング再生）----
// 音の高さ(Hz)と長さ(ms)。0 は休み
const int NOTE_G5 = 784, NOTE_C6 = 1047, NOTE_D6 = 1175, NOTE_E6 = 1319, NOTE_G6 = 1568;
const int JINGLE[][2] = {
  {NOTE_G5, 110}, {NOTE_C6, 110}, {NOTE_E6, 110}, {NOTE_G6, 260},
  {0, 90},
  {NOTE_E6, 110}, {NOTE_G6, 520},
};
const int JINGLE_LEN = sizeof(JINGLE) / sizeof(JINGLE[0]);
const int ALARM_REPEAT = 3;                 // 何回くり返すか
const unsigned long ALARM_INTERVAL = 4000;  // くり返しの間隔(ms)

int noteIdx = -1;          // 再生中の音の番号（-1 = 鳴っていない）
int repeatCount = 0;
unsigned long noteStart = 0, lastJingle = 0;

void startAlarm() {
  repeatCount = 0;
  noteIdx = 0;
  noteStart = millis();
  lastJingle = noteStart;
  if (JINGLE[0][0]) tone(PIN_BUZZER, JINGLE[0][0]);
}

void stopAlarm() {
  noteIdx = -1;
  repeatCount = ALARM_REPEAT;
  noTone(PIN_BUZZER);
}

// loop の中で毎回呼ぶ
void updateAlarm() {
  unsigned long now = millis();
  if (noteIdx >= 0) {                                   // ジングル再生中
    if (now - noteStart >= (unsigned long)JINGLE[noteIdx][1] + 20) {
      noteIdx++;
      if (noteIdx >= JINGLE_LEN) {                      // 1回分おわり
        noTone(PIN_BUZZER);
        noteIdx = -1;
        repeatCount++;
      } else {
        noteStart = now;
        if (JINGLE[noteIdx][0]) tone(PIN_BUZZER, JINGLE[noteIdx][0]);
        else noTone(PIN_BUZZER);
      }
    }
  } else if (repeatCount < ALARM_REPEAT && now - lastJingle >= ALARM_INTERVAL) {
    noteIdx = 0;                                        // 次のくり返し
    noteStart = now;
    lastJingle = now;
    tone(PIN_BUZZER, JINGLE[0][0]);
  }
  // 鳴っている間は緑LEDを点滅、鳴り終わったら点灯のまま
  bool ringing = repeatCount < ALARM_REPEAT;
  digitalWrite(PIN_GREEN, ringing ? ((now / 250) % 2) : HIGH);
}

void startBrew() {
  state = BREWING;
  startMs = millis();
  digitalWrite(PIN_YELLOW, HIGH);
  digitalWrite(PIN_GREEN, LOW);
  tone(PIN_BUZZER, 880, 100);            // スタートの合図
  Serial.println("抽出開始");
}

void finish() {
  state = DONE;
  motorOff();
  digitalWrite(PIN_YELLOW, LOW);
  Serial.println("完成");
  startAlarm();
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_YELLOW, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  for (int p = 8; p <= 11; p++) pinMode(p, OUTPUT);
  stepper.setSpeed(MOTOR_RPM);
  motorOff();
  Serial.println("待機中：ボタンで開始");
}

void loop() {
  bool pressed = buttonPressed();

  switch (state) {
    case IDLE:                      // ボタンを待つ
      if (pressed) startBrew();
      break;

    case BREWING:                   // 回しながら時間を測る
      stepper.step(1);
      if (millis() - startMs >= BREW_MS) finish();
      else if (pressed) {           // 途中でボタン → 中止して待機へ
        motorOff();
        digitalWrite(PIN_YELLOW, LOW);
        state = IDLE;
        Serial.println("中止");
      }
      break;

    case DONE:                      // アラームを鳴らし、ボタンで止めて待機へ
      updateAlarm();
      if (pressed) {
        stopAlarm();
        digitalWrite(PIN_GREEN, LOW);
        state = IDLE;
        Serial.println("待機中");
      }
      break;
  }
}

