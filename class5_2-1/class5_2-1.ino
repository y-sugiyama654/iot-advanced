// 問2：ジャイロセンサーの傾きに合わせてサーボモーターを動かす（ロール・ピッチ両対応・止まり対策版）
#include <Wire.h>    // I2C通信用
#include <Servo.h>   // サーボモーター用

const int MPU_ADDR  = 0x68;  // センサーのI2Cアドレス
const int SERVO_PIN = 9;     // サーボの信号線（橙）

// ---- どの傾きでサーボを動かすか ----
// 0：X軸まわりだけ　1：Y軸まわりだけ　2：両方（大きく傾いている方を使う）
const int MODE = 2;

// サーボに送るパルス幅（マイクロ秒）。SG90は約500〜2400で0〜180度
const int PULSE_MIN = 500;
const int PULSE_MAX = 2400;

const float ALPHA       = 0.96;  // ジャイロをどれだけ信じるか（大きいほど速くなめらか）
const float GYRO_SIGN_X = 1.0;   // X軸まわり：傾けた瞬間に一度逆に動く場合は -1.0
const float GYRO_SIGN_Y = 1.0;   // Y軸まわり：傾けた瞬間に一度逆に動く場合は -1.0
const bool  REVERSE_X   = false; // X軸まわりでサーボの向きを逆にしたいときは true
const bool  REVERSE_Y   = false; // Y軸まわりでサーボの向きを逆にしたいときは true
const int   DEADBAND    = 2;     // パルスの変化がこれ未満なら動かさない（震え防止）

Servo servo;
float angleX = 0, angleY = 0;    // 推定した傾きの角度
float offsetX = 0, offsetY = 0;  // ジャイロの誤差（起動時に測る）
int   lastPulse = 1450;
unsigned long lastMicros = 0;
unsigned long lastPrint  = 0;
int errorCount = 0;              // 通信エラーが続いた回数

// 2バイト（上位→下位の順）を読み取って、1つの値にする
int16_t read16() {
  int high = Wire.read();
  int low  = Wire.read();
  return (int16_t)((high << 8) | low);
}

// センサーのレジスタに1バイト書き込む
void writeReg(byte reg, byte value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission(true);
}

// センサーを起動・設定する（止まったときにも呼び直す）
void initSensor() {
  writeReg(0x6B, 0x00);   // スリープ解除
  writeReg(0x1A, 0x03);   // センサー内蔵のノイズ除去フィルタ（約44Hz）
  writeReg(0x1B, 0x00);   // ジャイロの範囲：±250度/秒
}

// 通信が固まったときに、I2Cとセンサーを起動し直す
void recover() {
  Serial.println("I2C error -> restart sensor");
  Wire.end();
  Wire.begin();
  Wire.setClock(100000);
  Wire.setWireTimeout(3000, true);   // 3ミリ秒返事がなければ待つのをやめる
  Wire.clearWireTimeoutFlag();
  initSensor();
  errorCount = 0;
  lastMicros = micros();
}

// 加速度から求めた2方向の角度と、ジャイロ2軸の生の値を読む（成功したらtrue）
bool readSensor(float &accX, float &accY, int16_t &gx, int16_t &gy) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);   // 加速度X → Y → Z → 温度 → ジャイロX → Y → Z の順
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, 14, true) < 14) return false;

  int16_t ax = read16();
  int16_t ay = read16();
  int16_t az = read16();
  read16();             // 温度（使わない）
  gx = read16();
  gy = read16();
  read16();             // ジャイロZ（使わない）

  // 全部0なら、センサーがリセットされてスリープに戻っている
  if (ax == 0 && ay == 0 && az == 0) {
    initSensor();
    return false;
  }

  accX = atan2(ay, az) * 180.0 / PI;                                       // X軸まわり
  accY = atan2(-ax, sqrt((float)ay * ay + (float)az * az)) * 180.0 / PI;   // Y軸まわり
  return true;
}

// 傾き（度）をサーボのパルス幅に変換する（map関数を使う）
int toPulse(float angle, bool reverse) {
  int tilt10 = constrain((int)(angle * 10), -900, 900);   // 0.1度単位
  return reverse ? map(tilt10, -900, 900, PULSE_MAX, PULSE_MIN)
                 : map(tilt10, -900, 900, PULSE_MIN, PULSE_MAX);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(100000);             // 標準速度（ブレッドボードの配線で安定しやすい）
  Wire.setWireTimeout(3000, true);   // 通信が固まらないようにタイムアウトを設定
  initSensor();

  servo.attach(SERVO_PIN, PULSE_MIN, PULSE_MAX);
  servo.writeMicroseconds(lastPulse);

  // ---- 起動時にジャイロの誤差を測る（この間はセンサーを動かさない）----
  Serial.println("Calibrating... keep still");
  delay(500);
  long sumX = 0, sumY = 0;
  int count = 0;
  float accX, accY;
  int16_t gx, gy;
  for (int i = 0; i < 300; i++) {
    if (readSensor(accX, accY, gx, gy)) { sumX += gx; sumY += gy; count++; }
    delay(3);
  }
  if (count > 0) { offsetX = (float)sumX / count; offsetY = (float)sumY / count; }

  if (readSensor(accX, accY, gx, gy)) { angleX = accX; angleY = accY; }
  lastMicros = micros();
  Serial.println("Start");
}

void loop() {
  float accX, accY;
  int16_t gx, gy;

  // ---- 読み取りに失敗したら、続いたときだけ起動し直す ----
  if (Wire.getWireTimeoutFlag() || !readSensor(accX, accY, gx, gy)) {
    Wire.clearWireTimeoutFlag();
    errorCount++;
    if (errorCount >= 5) recover();
    delay(5);
    return;
  }
  errorCount = 0;

  // 前回からの経過時間（秒）。復帰直後などで大きすぎるときは切り詰める
  unsigned long now = micros();
  float dt = (now - lastMicros) / 1000000.0;
  lastMicros = now;
  if (dt > 0.1) dt = 0.1;

  // 相補フィルタ：ジャイロで素早く追いかけ、加速度で少しずつズレを直す
  float rateX = GYRO_SIGN_X * (gx - offsetX) / 131.0;
  float rateY = GYRO_SIGN_Y * (gy - offsetY) / 131.0;
  angleX = ALPHA * (angleX + rateX * dt) + (1.0 - ALPHA) * accX;
  angleY = ALPHA * (angleY + rateY * dt) + (1.0 - ALPHA) * accY;

  // ---- どちらの傾きでサーボを動かすか決める ----
  int pulse;
  if (MODE == 0) {
    pulse = toPulse(angleX, REVERSE_X);
  } else if (MODE == 1) {
    pulse = toPulse(angleY, REVERSE_Y);
  } else {
    if (abs(angleX) >= abs(angleY)) pulse = toPulse(angleX, REVERSE_X);
    else                            pulse = toPulse(angleY, REVERSE_Y);
  }

  if (abs(pulse - lastPulse) >= DEADBAND) {
    servo.writeMicroseconds(pulse);
    lastPulse = pulse;
  }

  // ---- 確認用の表示（0.1秒に1回）----
  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    Serial.print("X: ");
    Serial.print(angleX, 1);
    Serial.print("  Y: ");
    Serial.print(angleY, 1);
    Serial.print("  pulse: ");
    Serial.println(pulse);
  }
}
