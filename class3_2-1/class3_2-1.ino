// 問2：LCDに授業への意気込みを表示する
#include <LiquidCrystal.h>   // LCD制御用のライブラリ

// LCDとArduinoのピンの対応を指定する
// LiquidCrystal(RS, E, D4, D5, D6, D7)
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

void setup() {
  lcd.begin(16, 2);                // 16文字×2行のLCDとして初期化する
  lcd.print("Arduino Class");     // 1行目に表示する
  lcd.setCursor(0, 1);             // カーソルを2行目の先頭へ移動する（列, 行：0から数える）
  lcd.print("Ganbarimasu!");      // 2行目に意気込みを表示する
}

void loop() {
  // 表示は一度書き込めば保持されるため、繰り返し処理は不要
}
