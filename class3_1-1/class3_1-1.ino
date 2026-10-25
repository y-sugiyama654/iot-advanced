// 問1：0.5秒ごとに点灯するLEDを左から右へ移動させる

const int LED_PINS[] = {8, 9, 10, 11, 12};  // LEDをつないだピン（左から右の順）
const int LED_COUNT  = 5;                     // LEDの個数
const int INTERVAL   = 500;                   // 点灯時間（ミリ秒）= 0.5秒


void setup() {
  // すべてのLEDピンを出力に設定し、最初は全部消灯しておく
  for (int i = 0; i < LED_COUNT; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }
}


void loop() {
  // 左（配列の先頭）から右へ1個ずつ順番に光らせる
  for (int i = 0; i < LED_COUNT; i++) {
    digitalWrite(LED_PINS[i], HIGH);  // 対象のLEDだけ点灯
    delay(INTERVAL);                  // 0.5秒待つ
    digitalWrite(LED_PINS[i], LOW);   // 次へ進む前に消灯（常に1個だけ点灯）
  }
  // forループが終わるとloop()が最初から実行され、一番左のLEDに戻る
}
