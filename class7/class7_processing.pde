// 第7回 IoT特論：Arduino連携ピンポンゲーム（Processing側）
// ・ツマミ（可変抵抗）でラケットを操作し、5回連続で打ち返すとクリア。ライフは3。
// ・Arduinoとはシリアル通信（9600bps）でやり取りする。
//   受信：「P,値」=可変抵抗の値（0〜1023）、「B,1」=ボタンが押された
//   送信：S=スタート H=打ち返した M=ミス C=クリア G=ゲームオーバー P=一時停止 R=再開
import processing.serial.*;

// ---- Arduino ----
Serial port;
boolean arduinoConnected = false;
int potValue = -1;                   // 可変抵抗の値（0〜1023、未受信は-1）
volatile boolean buttonFlag = false; // Arduinoのボタンが押されたら true
final boolean POT_REVERSE = false;   // ラケットが逆に動くときは true にする

// ---- ボール ----
float ballX, ballY;
float ballVX, ballVY;
float ballSize = 20;
float ballSpeed = 3.5;              // ボールの速さ

// ---- ラケット ----
float paddleX;
float paddleY;
float paddleW = 120;
float paddleH = 15;
float paddleSpeed = 8;

// ---- 壁 ----
float wall = 10;

// ---- キーボードの状態 ----
boolean leftPressed  = false;
boolean rightPressed = false;

// ---- ルール ----
final int CLEAR_COMBO = 5;   // 連続でこの回数打ち返したらクリア
final int MAX_MISS    = 3;   // ライフの数

// ---- ゲームの状態 ----
final int READY    = 0;   // タイトル画面（ボタンでスタート）
final int PLAYING  = 1;
final int PAUSED   = 2;
final int GAMEOVER = 3;
final int CLEAR    = 4;
int state;

int combo;          // 今の連続で打ち返した回数（ミスで0に戻る）
int bestCombo;      // このゲームでの最高連続回数
int miss;
int comboTimer;     // 「○回連続！」を表示する残りフレーム数

PFont jpFont;       // 日本語を表示するためのフォント

void setup() {
  size(600, 400);
  setupFont();
  paddleY = height - 40;
  connectArduino();
  resetGame();
  state = READY;
}

// ---- 日本語フォントの準備 ----
// Processingの標準フォントは日本語を表示できないので、Macに入っている日本語フォントを使う
void setupFont() {
  String[] candidates = {"HiraginoSans-W6", "HiraKakuProN-W6", "Hiragino Sans", "Osaka", "Meiryo", "Yu Gothic"};
  String[] installed = PFont.list();
  String chosen = "SansSerif";
  for (String c : candidates) {
    for (String f : installed) {
      if (f.equals(c)) { chosen = c; break; }
    }
    if (!chosen.equals("SansSerif")) break;
  }
  println("Font: " + chosen);
  jpFont = createFont(chosen, 48);
  textFont(jpFont);
}

// ---- Arduinoとの接続 ----
void connectArduino() {
  printArray(Serial.list());
  for (String name : Serial.list()) {
    if (name.contains("cu.usbmodem") || name.contains("cu.usbserial")) {
      try {
        port = new Serial(this, name, 9600);
        port.bufferUntil('\n');
        arduinoConnected = true;
        println("Connected: " + name);
      } catch (Exception e) {
        println("Could not open " + name + " : " + e.getMessage());
      }
      break;
    }
  }
  if (!arduinoConnected) println("Arduino not found. Mouse / keyboard only.");
}

// Arduinoから1行届いたときに自動で呼ばれる
void serialEvent(Serial p) {
  try {
    String line = p.readStringUntil('\n');
    if (line == null) return;
    String[] parts = split(trim(line), ',');
    if (parts.length != 2) return;
    if (parts[0].equals("P")) potValue = int(parts[1]);   // 可変抵抗
    if (parts[0].equals("B")) buttonFlag = true;          // ボタン
  } catch (Exception e) {
    // 文字化けなどで読めない行は無視する
  }
}

// Arduinoへ1文字のコマンドを送る
void send(char c) {
  if (port != null) port.write(c);
}

// ---- ゲームの開始・リセット ----
void resetGame() {
  combo = 0;
  bestCombo = 0;
  miss = 0;
  comboTimer = 0;
  paddleX = width / 2;
  resetBall();
}

void startGame() {
  resetGame();
  state = PLAYING;
  send('S');          // LED全点灯＋スタート音
}

// ボタン（またはスペースキー・クリック）が押されたとき
void onButton() {
  if (state == READY)                             startGame();
  else if (state == PLAYING)                    { state = PAUSED;  send('P'); }
  else if (state == PAUSED)                     { state = PLAYING; send('R'); }
  else if (state == GAMEOVER || state == CLEAR)   state = READY;
}

void draw() {
  if (buttonFlag) {          // Arduinoのボタンはここでまとめて処理する
    buttonFlag = false;
    onButton();
  }

  background(220, 230, 245);
  drawWalls();

  if (state == PLAYING || state == READY) updatePaddle();
  if (state == PLAYING) updateBall();

  drawPaddle();
  if (state == PLAYING || state == PAUSED) drawBall();
  drawInfo();

  if (state == READY)    drawTitle();
  if (state == PAUSED)   drawMessage("一時停止", color(60, 100, 180), "ボタンで再開");
  if (state == GAMEOVER) drawMessage("ゲームオーバー", color(200, 50, 50), "最高連続：" + bestCombo + "回　　ボタンでタイトルへ");
  if (state == CLEAR)    drawMessage("クリア！", color(40, 150, 70), CLEAR_COMBO + "回連続で打ち返した！　　ボタンでタイトルへ");
}

// ---- 壁 ----
void drawWalls() {
  fill(60, 100, 180);
  noStroke();
  rect(0, 0, width, wall);
  rect(0, 0, wall, height);
  rect(width - wall, 0, wall, height);
}

// ---- ラケット ----
void updatePaddle() {
  if (potValue >= 0) {
    float v = POT_REVERSE ? 1023 - potValue : potValue;
    float target = map(v, 0, 1023, wall + paddleW/2, width - wall - paddleW/2);
    paddleX = lerp(paddleX, target, 0.5);
  }
  if (leftPressed)  paddleX -= paddleSpeed;
  if (rightPressed) paddleX += paddleSpeed;
  paddleX = constrain(paddleX, wall + paddleW/2, width - wall - paddleW/2);
}

void drawPaddle() {
  fill(60, 100, 180);
  noStroke();
  rect(paddleX - paddleW/2, paddleY, paddleW, paddleH);
}

// ---- ボール ----
void updateBall() {
  ballX += ballVX;
  ballY += ballVY;

  // 左右の壁で跳ね返る
  if (ballX - ballSize/2 < wall || ballX + ballSize/2 > width - wall) {
    ballVX *= -1;
    ballX = constrain(ballX, wall + ballSize/2, width - wall - ballSize/2);
  }
  // 上の壁で跳ね返る
  if (ballY - ballSize/2 < wall) {
    ballVY *= -1;
    ballY = wall + ballSize/2;
  }

  // ラケットで跳ね返る
  if (ballVY > 0 &&
      ballY + ballSize/2 >= paddleY &&
      ballY + ballSize/2 <= paddleY + paddleH + ballVY &&
      ballX >= paddleX - paddleW/2 &&
      ballX <= paddleX + paddleW/2) {
    ballY = paddleY - ballSize/2;
    combo++;
    bestCombo = max(bestCombo, combo);
    comboTimer = 40;

    // 5回連続で打ち返したらクリア
    if (combo >= CLEAR_COMBO) {
      state = CLEAR;
      send('C');                      // クリアのメロディ
      return;
    }
    send('H');                        // 打ち返した音

    // 当たった位置で角度を変えて跳ね返す
    float hit = (ballX - paddleX) / (paddleW/2);   // -1（左端）〜 1（右端）
    float angle = radians(hit * 50);               // 最大で左右50度
    ballVX = ballSpeed * sin(angle);
    ballVY = -ballSpeed * cos(angle);
  }

  // 画面の下に抜けた（ミス）→ 連続回数は0に戻る
  if (ballY - ballSize/2 > height) {
    miss++;
    combo = 0;
    if (miss >= MAX_MISS) {
      state = GAMEOVER;
      send('G');                      // ゲームオーバー音＋LED消灯
      return;
    }
    send('M');                        // ミス音＋LED点滅
    resetBall();
  }
}

void drawBall() {
  fill(230, 80, 60);
  noStroke();
  ellipse(ballX, ballY, ballSize, ballSize);
}

// ---- 表示 ----
void drawInfo() {
  // 連続回数（左上）：数字と、達成した分だけ塗りつぶす丸
  fill(30);
  textSize(16);
  textAlign(LEFT, BASELINE);
  String comboText = "連続：" + combo + " / " + CLEAR_COMBO;
  text(comboText, 20, 35);
  float dotX = 20 + textWidth(comboText) + 18;
  for (int i = 0; i < CLEAR_COMBO; i++) {
    if (i < combo) fill(40, 150, 70);
    else           fill(200);
    noStroke();
    ellipse(dotX + i * 20, 29, 12, 12);
  }

  // Arduinoの接続状態（左下）
  textSize(12);
  if (arduinoConnected) {
    fill(40, 150, 70);
    text("Arduino：接続中　　ツマミの値 = " + potValue, 20, height - 10);
  } else {
    fill(200, 50, 50);
    text("Arduino：未接続（マウス・キーボードで操作）", 20, height - 10);
  }

  // ライフ（ハート）。LEDと同じく右から消える
  for (int i = 0; i < MAX_MISS; i++) {
    float x = width - 40 - (MAX_MISS - 1 - i) * 32;
    boolean alive = i < (MAX_MISS - miss);
    drawHeart(x, 28, 24, alive);
  }

  // 打ち返したときに「○回連続！」を表示
  if (comboTimer > 0 && state == PLAYING) {
    fill(230, 80, 60, map(comboTimer, 0, 40, 0, 255));
    textSize(36);
    textAlign(CENTER, CENTER);
    text(combo + "回連続！", width/2, height/2);
    comboTimer--;
  }
}

void drawHeart(float x, float y, float s, boolean alive) {
  if (alive) fill(230, 50, 70);
  else       fill(200);
  noStroke();
  pushMatrix();
  translate(x, y);
  scale(s / 24.0);
  beginShape();
  vertex(0, 8);
  bezierVertex(-12, -2, -10, -14, 0, -6);
  bezierVertex(10, -14, 12, -2, 0, 8);
  endShape(CLOSE);
  popMatrix();
}

void drawTitle() {
  fill(255, 255, 255, 190);
  noStroke();
  rect(0, 0, width, height);
  fill(60, 100, 180);
  textAlign(CENTER, CENTER);
  textSize(56);
  text("ピンポン", width/2, height/2 - 50);
  fill(30);
  textSize(18);
  text(CLEAR_COMBO + "回連続で打ち返せばクリア！", width/2, height/2 + 5);
  text("ボタン（またはスペースキー）でスタート", width/2, height/2 + 35);
  textSize(14);
  text("ツマミ：ラケットを動かす　／　ボタン：一時停止", width/2, height/2 + 65);
}

void drawMessage(String msg, int c, String sub) {
  fill(255, 255, 255, 200);
  noStroke();
  rect(0, 0, width, height);
  fill(c);
  textAlign(CENTER, CENTER);
  textSize(48);
  text(msg, width/2, height/2 - 20);
  fill(30);
  textSize(18);
  text(sub, width/2, height/2 + 35);
}

// ボールを画面の上（ランダムな横位置）から出す
void resetBall() {
  ballX = random(wall + ballSize, width - wall - ballSize);
  ballY = wall + ballSize;
  float angle = radians(random(-30, 30));
  ballVX = ballSpeed * sin(angle);
  ballVY = ballSpeed * cos(angle);
}

// ---- マウス・キーボード（Arduinoがないときの予備） ----
void mouseMoved() {
  if (potValue < 0) paddleX = mouseX;
}

void mousePressed() {
  onButton();
}

void keyPressed() {
  if (keyCode == LEFT)  leftPressed  = true;
  if (keyCode == RIGHT) rightPressed = true;
  if (key == ' ') onButton();
}

void keyReleased() {
  if (keyCode == LEFT)  leftPressed  = false;
  if (keyCode == RIGHT) rightPressed = false;
}
