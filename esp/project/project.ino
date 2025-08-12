#include <Adafruit_Fingerprint.h>  //לטביעת אצבע
#include <HardwareSerial.h>        // ליצור עוד פורטים סיראלים
#include "TFT9341Touch.h"
#include <ESP32Servo.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "time.h"  // לסנכרון זמן עם NTP
#include "sntp.h"
#include "secrets.h"

// הגדרות מסך מגע TFT
//TFT9341Touch(touchCS, touchIRQ, tftCS, tftDC);

tft9341touch lcd(5, 4, 15, 35);

// הגדרות צבעים
#define BLACK 0x0000  // קוד צבע בפורמט RGB565
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define YELLOW 0xFFE0
#define WHITE 0xFFFF

// הגדרות חיישן טביעת אצבע
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger(&mySerial);

HardwareSerial arduinoSerial(1);  // פותח פורט סיראלי סשביל לתקשר עם ארדואינו  TX26, RX25

// מבנה שחקן
struct Player {
  String name;
  int wins;
  bool active;
};

// מערך דינמי לאחסון שחקנים
Player* players = nullptr;        // מצביע למערך
uint16_t maxPlayersCapacity = 0;  // קיבולת המערך הנוכחית
uint16_t currentPlayerCount = 0;  // מספר השחקנים בפועל

uint8_t nextID = 1;  // מזהה פנוי הבא לטביעת אצבע חדשה

// הגדרות מקלדת וקלט שם
String currentName = "";
const int maxNameLength = 12;  // אורך מקסימלי לשם שחקן
// מימדי פונט
const int FONT_CHAR_WIDTH = 6;
const int FONT_CHAR_HEIGHT = 8;
// פריסת מקלדת QWERTY
const char* engKeys[] = {
  "qwertyuiop",
  "asdfghjkl",
  "zxcvbnm"
};
const int numRows = 3;
const int keysPerRow[] = { 10, 9, 7 };  // מספר מקשים בכל שורה
// מיקום וגודל מקשים על המסך
int keyWidth = 22;
int keyHeight = 32;
int keyPaddingX = 1;
int keyPaddingY = 5;
int keyStartX = 5;
int keyStartY = 90;
int xTouch, yTouch;  // קואורדינטות מגע

// הגדרות חיישן מגע (לזיהוי ממתק)
const int touchPin = 12;
int lastTouchState = LOW;

// הגדרות מנוע סרוו סיבוב רציף (סרוו 1 - מגש ממתק)
Servo myServo;
const int servoPin = 14;

// הגדרות מנוע סרוו סיבוב רציף (סרוו 2 - פתיחה/סגירה זרוע)
// Servo mySecondServo;
// const int secondServoPin = 13;

// ערכי כיול לסרוו (במיקרושניות)
//קביעת רוחב פולס PWM לשליטה על כיוון ומהירות סיבוב סרוו רציף
const int SERVO_STOP_PULSE = 1500;           //עצירה
const int SERVO_DISPENSE_PULSE = 1000;       // סיבוב לכיוון אחד במהירות מלאה
const int SERVO_RETURN_PULSE = 2000;         // סיבוב לכיוון ההפוך
const int SERVO_DISPENSE_DURATION_MS = 500;  // כמה שניות להסתובב
const int SERVO_RETURN_DURATION_MS = 540;

// ערכי כיול לסרוו השני (פתיחה/סגירה)
// const int SECOND_SERVO_OPEN_PULSE = 1000;
// const int SECOND_SERVO_CLOSE_PULSE = 2000;
// const int SECOND_SERVO_OPEN_DURATION_MS = 500;
// const int SECOND_SERVO_CLOSE_DURATION_MS = 540;

const int buzzerPin = 27;  // פין הבאזר

// משתני מצב גלובליים
enum State {
  WAIT_START_PRESS,
  WAIT_FINGER,
  REGISTER_NAME,
  PLAY_GAME,       // כפתור התחלת משחק
  IN_GAME_ACTIVE,  //לפי הטיימר
  GAME_OVER_CHECK_TOUCH
};
State currentState = WAIT_START_PRESS;
uint16_t currentPlayerID = 0;

unsigned long gameStartTime = 0;  // ESP millis()

// הגדרות זמן המשחק
const unsigned long MAX_GAME_DURATION_SECONDS = 40;  // מקסימום שניות למשחק
const unsigned long MIN_GAME_DURATION_SECONDS = 10;  // מינימום שניות למשחק
const unsigned long REDUCTION_PER_WIN_SECOND = 1;    // כמה שניות משחק יורדות עבור כל ניצחון

unsigned long currentGameDuration = MAX_GAME_DURATION_SECONDS;
unsigned long lastTimerUpdateTime = 0;
unsigned long displayedTimeLeft = MAX_GAME_DURATION_SECONDS;
unsigned long touchCheckStartTime = 0;
const unsigned long TOUCH_SENSOR_WAIT_TIMEOUT_SECONDS = 10;

//אוביקטים של firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// משתנה גלובלי למספר התור הנוכחי
volatile int currentQueueNumber = 0;
// משתנה גלובלי לשמירת חותמת הזמן של התור הנוכחי מ-Firebase (זמן יוניקס במילישניות)
volatile unsigned long currentServingTimestamp = 0;
// משתנה גלובלי לשמירת חותמת הזמן של התור האחרון שהונפק (מ-Firebase)
volatile int lastIssuedNumber = 0;

// משתנה לשמירת חותמת הזמן האחרונה שבה בוצעה שליפה תקופתית ודיווח פעילות
unsigned long lastPeriodicFetchTime = 0;
const long periodicFetchInterval = 2 * 60 * 1000UL;  // 2 minutes in milliseconds

// הגדרות NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3 * 3600;  // GMT+3 (Israel Daylight Time)
const int daylightOffset_sec = 0;     // אין שינוי עבור שעון קיץ/חורף אוטומטי, NTP מטפל בזה

const int yellowLedPin = 32;  // לד צהוב - טיימר פעיל
const int redLedPin = 21;     // לד אדום - הפסד
const int greenLedPin = 33;   // לד ירוק - ניצחון

// הצהרות פונקציות
String fitTextToWidth(String text, int maxWidthPx, int fontSize);
void drawNameBox();
void drawKeyboard();
void handleKeyboardTouch(int x, int y);
void setupFingerprintSensor();
uint16_t getOrRegisterFingerprint();
bool enrollFingerprint(uint8_t id);
void drawStartScreen();
void updateQueueNumberDisplay();  // פונקציה חדשה לעדכון מספר התור בלבד
void drawGameScreen(bool isExistingUser);
void drawActiveGameScreen(unsigned long timeLeft);
void calibrateServo();  // פונקציית כיול סרוו (מוערת ב-setup)
// void calibrateSecondServo();
void playVictoryTune();              // מנגינת ניצחון
void playLossTune();                 // מנגינת הפסד
void advanceQueue();                 // פונקציה לקידום התור
unsigned long getCurrentUnixTime();  // פונקציה לקבלת זמן יוניקס נוכחי במילישניות
String getDistanceFromArduino();     // הצהרה לפונקציה החדשה לקבלת מרחק מהארדואינו

// פונקציות לניהול המערך הדינמי של שחקנים
void initializePlayersArray(uint16_t initialCapacity);
void resizePlayersArray(uint16_t newCapacity);
void addPlayer(uint16_t id, String name, int wins);
Player* getPlayer(uint16_t id);
bool playerExists(uint16_t id);

// פונקציות עזר

void sendStartCommand() {
  arduinoSerial.println("START");
  Serial.println("Sent START command to Arduino");
}
void sendStopCommand() {
  arduinoSerial.println("STOP");
  Serial.println("Sent STOP command to Arduino");
}

// פונקציה חדשה: שליחת בקשה למרחק לארדואינו וקבלת התשובה
String getDistanceFromArduino() {
  arduinoSerial.println("GET_DISTANCE");  // שלח פקודה לארדואינו
  Serial.println("Sent GET_DISTANCE command to Arduino.");
  unsigned long startTime = millis();
  while (!arduinoSerial.available() && (millis() - startTime < 1000)) {  // המתן לתשובה עד שנייה
    delay(10);
  }
  if (arduinoSerial.available()) {
    String response = arduinoSerial.readStringUntil('\n');
    response.trim();
    Serial.println("Received from Arduino: " + response);
    return response;
  }
  Serial.println("No response from Arduino for distance request.");
  return "ERROR";  // החזר שגיאה אם לא התקבלה תשובה
}


// מאתחל את מערך השחקנים
void initializePlayersArray(uint16_t initialCapacity) {
  if (players != nullptr) {
    delete[] players;  // שחרר זיכרון קודם אם קיים
  }
  maxPlayersCapacity = initialCapacity;
  players = new Player[maxPlayersCapacity];
  currentPlayerCount = 0;
  // אתחל את כל השחקנים במצב לא פעיל
  for (int i = 0; i < maxPlayersCapacity; i++) {
    players[i].active = false;
  }
}

// מגדיל את מערך השחקנים פי 2
void resizePlayersArray(uint16_t newCapacity) {
  if (newCapacity <= maxPlayersCapacity) {
    return;  // אין צורך להקטין או להישאר באותו גודל
  }
  Player* newPlayers = new Player[newCapacity];
  // העתק את הנתונים הקיימים
  for (int i = 0; i < currentPlayerCount; i++) {
    newPlayers[i] = players[i];
  }
  // אתחל את המקומות החדשים כלא פעילים
  for (int i = currentPlayerCount; i < newCapacity; i++) {
    newPlayers[i].active = false;
  }
  delete[] players;  // שחרר את הזיכרון הישן
  players = newPlayers;
  maxPlayersCapacity = newCapacity;
}

// מוסיף שחקן למערך.
// מזהה השחקן משמש כאינדקס (פחות 1) במערך.
void addPlayer(uint16_t id, String name, int wins) {
  // אם ה-ID גבוה מהקיבולת הנוכחית, הגדל את המערך
  if (id > maxPlayersCapacity) {
    uint16_t newCapacity = maxPlayersCapacity == 0 ? 4 : maxPlayersCapacity * 2;  // התחל מ-4 אם ריק, אחרת הכפל
    while (id > newCapacity) {                                                    // וודא שהקיבולת החדשה גדולה מספיק עבור ה-ID
      newCapacity *= 2;
    }
    resizePlayersArray(newCapacity);
  }

  // מכיוון שה-ID מתחיל מ-1, האינדקס הוא ID-1
  if (id - 1 < maxPlayersCapacity) {
    players[id - 1].name = name;
    players[id - 1].wins = wins;
    players[id - 1].active = true;  // סמן כשחקן פעיל
    // אם זה ID חדש לגמרי, הגדל את מונה השחקנים
    if (id > currentPlayerCount) {
      currentPlayerCount = id;
    }
  }
}

// מחזיר מצביע לשחקן לפי ID
Player* getPlayer(uint16_t id) {
  if (id > 0 && id <= currentPlayerCount && players[id - 1].active) {
    return &players[id - 1];
  }
  return nullptr;  // לא נמצא
}

// בודק אם שחקן קיים לפי ID
bool playerExists(uint16_t id) {
  return (id > 0 && id <= currentPlayerCount && players[id - 1].active);
}

// מקצר טקסט כדי שיתאים לרוחב נתון
String fitTextToWidth(String text, int maxWidthPx, int fontSize) {
  int currentTextWidth = text.length() * FONT_CHAR_WIDTH * fontSize;
  while (currentTextWidth > maxWidthPx && text.length() > 0) {
    text.remove(text.length() - 1);  // הסר תו מהסוף
    currentTextWidth = text.length() * FONT_CHAR_WIDTH * fontSize;
  }
  return text;
}

// מצייר תיבת קלט לשם השחקן
void drawNameBox() {
  int boxX = 10;
  int boxY = 40;
  int boxWidth = lcd.width() - 20;
  int boxHeight = 40;
  int fontSize = 2;
  int padding = 5;
  lcd.fillRoundRect(boxX, boxY, boxWidth, boxHeight, 8, 0x7BEF); 
  int availableTextWidth = boxWidth - (2 * padding);
  String displayableName = fitTextToWidth(currentName, availableTextWidth, fontSize);
  int textDisplayY = boxY + (boxHeight - (FONT_CHAR_HEIGHT * fontSize)) / 2;
  lcd.print(boxX + padding, textDisplayY, displayableName, fontSize, WHITE, 0x7BEF);
}

// מצייר את המקלדת הוירטואלית
void drawKeyboard() {
  lcd.fillScreen(0);  // Black background

  // מקשי אותיות
  for (int row = 0; row < numRows; row++) {
    int charsInRow = keysPerRow[row];
    int currentKeyStartX = keyStartX;
    if (row == 1) currentKeyStartX += (keyWidth + keyPaddingX) / 2;
    if (row == 2) currentKeyStartX += (keyWidth + keyPaddingX);
    for (int col = 0; col < charsInRow; col++) {
      int x = currentKeyStartX + col * (keyWidth + keyPaddingX);
      int y = keyStartY + row * (keyHeight + keyPaddingY);
      char key = engKeys[row][col];
      lcd.fillRoundRect(x, y, keyWidth, keyHeight, 5, BLUE);
      lcd.print(x + (keyWidth / 4), y + (keyHeight / 4), String(key), 2, WHITE, BLUE);
    }
  }

  // כפתור מחיקה (X)
  int screenWidth = lcd.width();
  int delBtnWidth = 60;
  int okBtnWidth = 50;
  int delX = screenWidth - delBtnWidth - okBtnWidth - 5;
  int delY = keyStartY + numRows * (keyHeight + keyPaddingY);
  lcd.fillRoundRect(delX, delY, delBtnWidth, keyHeight, 5, RED);
  lcd.print(delX + (delBtnWidth - (1 * FONT_CHAR_WIDTH * 2)) / 2, delY + (keyHeight / 4), "X", 2, WHITE, RED);

  // כפתור אישור (V)
  int okX = screenWidth - okBtnWidth - 5;
  int okY = delY;
  lcd.fillRoundRect(okX, okY, okBtnWidth, keyHeight, 5, GREEN);
  lcd.print(okX + (okBtnWidth - (1 * FONT_CHAR_WIDTH * 2)) / 2, okY + (keyHeight / 4), "V", 2, WHITE, GREEN); 

  // כפתור ביטול (CANCEL) - רק ציור, ללא לוגיקת מגע כאן
  int cancelBtnWidth = 70;
  int cancelX = delX - cancelBtnWidth - 5;
  int cancelY = delY;
  lcd.fillRoundRect(cancelX, cancelY, cancelBtnWidth, keyHeight, 5, RED);
  lcd.print(cancelX + 5, cancelY + 10, "CANCEL", 1, WHITE, RED);
}

// מטפל בלחיצות מקלדת
void handleKeyboardTouch(int x, int y) {
  // בדיקת מקשי אותיות
  for (int row = 0; row < numRows; row++) {
    int charsInRow = keysPerRow[row];
    int currentKeyStartX = keyStartX;
    if (row == 1) currentKeyStartX += (keyWidth + keyPaddingX) / 2;
    if (row == 2) currentKeyStartX += (keyWidth + keyPaddingX);
    for (int col = 0; col < charsInRow; col++) {
      int bx = currentKeyStartX + col * (keyWidth + keyPaddingX);
      int by = keyStartY + row * (keyHeight + keyPaddingY);
      if (x >= bx && x <= bx + keyWidth && y >= by && y <= by + keyHeight) {
        if (currentName.length() < maxNameLength) {
          currentName += engKeys[row][col];
        }
        drawNameBox();
        return;
      }
    }
  }

  // בדיקת כפתור מחיקה
  int screenWidth = lcd.width();
  int delBtnWidth = 60;
  int okBtnWidth = 50;
  int delX = screenWidth - delBtnWidth - okBtnWidth - 5;
  int delY = keyStartY + numRows * (keyHeight + keyPaddingY);
  if (x >= delX && x <= delX + delBtnWidth && y >= delY && y <= delY + keyHeight) {
    if (currentName.length() > 0) {
      currentName.remove(currentName.length() - 1);
      drawNameBox();
    }
    return;
  }

  // בדיקת כפתור אישור
  int okX = screenWidth - okBtnWidth - 5;
  int okY = delY;
  if (x >= okX && x <= okX + okBtnWidth && y >= okY && y <= okY + keyHeight) {
    if (currentName.length() > 0) {
      addPlayer(currentPlayerID, currentName, 0);  // הוספת שחקן חדש
    }
    drawGameScreen(false);  // משתמש חדש
    currentState = PLAY_GAME;
  }

  // בדיקת כפתור ביטול
  int cancelBtnWidth = 70;
  int cancelX = delX - cancelBtnWidth - 5;
  int cancelY = delY;
  if (x >= cancelX && x <= cancelX + cancelBtnWidth && y >= cancelY && y <= cancelY + keyHeight) {
    drawStartScreen();  // חזרה למסך ההתחלה
    currentState = WAIT_START_PRESS;
    currentName = "";  // ניקוי השם
    return;
  }
}

// מאתחל את חיישן טביעת האצבע
void setupFingerprintSensor() {
  mySerial.begin(57600, SERIAL_8N1, 16, 17);  // Baud rate, RX2, TX2 pins
  finger.begin(57600);                        // אתחול החיישן
  if (!finger.verifyPassword()) {
    lcd.fillScreen(0);
    lcd.print(20, 100, "Sensor Error!", 2, RED, 0);
    lcd.print(20, 130, "Check Wiring!", 2, RED, 0);
    Serial.println("Fingerprint sensor error! Check wiring.");
    while (true) delay(1);  // עצירה אם החיישן לא נמצא
  }
  // קבלת מספר התבניות הנוכחי וקביעת ה-ID הבא
  uint16_t templateCount;
  int p = finger.getTemplateCount();
  if (p == FINGERPRINT_OK) {
    templateCount = finger.templateCount;
    Serial.println("Current template count: " + String(templateCount));
  } else {
    templateCount = 0;
    Serial.println("Could not get template count. Assuming 0.");
  }
  nextID = templateCount + 1;
}

// מנסה לזהות טביעת אצבע או לרשום חדשה
uint16_t getOrRegisterFingerprint() {
  // המסך כבר נמשך מראש
  int cancelBtnX = 80;
  int cancelBtnY = 180;
  int cancelBtnW = 160;
  int cancelBtnH = 40;

  lcd.fillScreen(0);
  lcd.print(20, 40, "Place finger...", 2, WHITE, 0);
  lcd.fillRoundRect(cancelBtnX, cancelBtnY, cancelBtnW, cancelBtnH, 8, RED);
  lcd.print(cancelBtnX + (cancelBtnW - (6 * FONT_CHAR_WIDTH * 2)) / 2, cancelBtnY + (cancelBtnH - FONT_CHAR_HEIGHT * 2) / 2, "Cancel", 2, WHITE, RED);

  while (true) {  // לולאה לאצבע או ביטול
    // בדיקת מגע (לדוגמה, כפתור ביטול)
    if (lcd.touched()) {
      delay(30);  // Debounce
      lcd.readTouch();
      int touchX = lcd.xTouch;
      int touchY = lcd.yTouch;
      while (lcd.touched())
        ;  // המתן לשחרור האצבע

      // כפתור "Cancel" נלחץ
      if (touchX >= cancelBtnX && touchX <= (cancelBtnX + cancelBtnW) && touchY >= cancelBtnY && touchY <= (cancelBtnY + cancelBtnH)) {
        drawStartScreen();  // חזרה ישירה למסך ההתחלה
        Serial.println("Fingerprint operation cancelled by user. Returning to start screen.");
        delay(100);  // השהיה קצרה למניעת לחיצה כפולה
        return 0;    // ביטול
      }
    }

    // ניסיון לקבל תמונה מהחיישן
    int p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      lcd.fillScreen(0);
      lcd.print(20, 40, "Image Captured!", 2, GREEN, 0);
      Serial.println("Fingerprint image captured.");
      delay(500);
      break;
    } else if (p == FINGERPRINT_NOFINGER) {
    } else {
      lcd.fillScreen(0);
      lcd.print(20, 40, "Identification failed", 2, RED, 0);  // הודעת כשל זיהוי כללית
      Serial.println("Fingerprint sensor error code: " + String(p));
      delay(2000);
      return 0;
    }
    delay(50);
  }

  int tzResult = finger.image2Tz();
  if (tzResult != FINGERPRINT_OK) {
    lcd.fillScreen(0);
    lcd.print(20, 40, "Identification failed", 2, RED, 0);  // הודעת כשל זיהוי כללית
    Serial.println("Bad image quality. Error code: " + String(tzResult));
    delay(1500);
    return 0;
  }

  lcd.fillScreen(0);
  lcd.print(20, 40, "Processing...", 2, WHITE, 0);
  Serial.println("Processing fingerprint image.");
  delay(500);

  int searchResult = finger.fingerSearch();

  if (searchResult == FINGERPRINT_OK) {
    uint16_t id = finger.fingerID;
    lcd.fillScreen(0);
    lcd.print(20, 40, "Fingerprint Found!", 2, GREEN, 0);
    Serial.println("Fingerprint found. ID: " + String(id));
    delay(1000);
    return id;
  } else if (searchResult == FINGERPRINT_NOTFOUND) {
    lcd.fillScreen(0);
    lcd.print(20, 40, "New fingerprint!", 2, BLUE, 0);

    // כפתור ביטול
    int cancelBtnX = 20;
    int cancelBtnY = 100;
    int cancelBtnW = 120;
    int cancelBtnH = 40;
    lcd.fillRoundRect(cancelBtnX, cancelBtnY, cancelBtnW, cancelBtnH, 8, RED);
    lcd.print(cancelBtnX + (cancelBtnW - (6 * FONT_CHAR_WIDTH * 2)) / 2, cancelBtnY + (cancelBtnH - FONT_CHAR_HEIGHT * 2) / 2, "Cancel", 2, WHITE, RED);

    // כפתור Sign up
    int signUpBtnX = 180;
    int signUpBtnY = 100;
    int signUpBtnW = 120;
    int signUpBtnH = 40;
    lcd.fillRoundRect(signUpBtnX, signUpBtnY, signUpBtnW, signUpBtnH, 8, GREEN);
    lcd.print(signUpBtnX + (signUpBtnW - (7 * FONT_CHAR_WIDTH * 2)) / 2, signUpBtnY + (cancelBtnH - FONT_CHAR_HEIGHT * 2) / 2, "Sign up", 2, WHITE, GREEN);

    Serial.println("New fingerprint detected. Waiting for user action.");

    while (true) {
      if (lcd.touched()) {
        delay(30);
        lcd.readTouch();
        int touchX = lcd.xTouch;
        int touchY = lcd.yTouch;  // Revert to lcd.yTouch
        while (lcd.touched())
          ;

        if (touchX >= cancelBtnX && touchX <= (cancelBtnX + cancelBtnW) && touchY >= cancelBtnY && touchY <= (cancelBtnY + cancelBtnH)) {
          drawStartScreen();  // Return directly to home page
          Serial.println("New fingerprint operation cancelled. Returning to start screen.");
          delay(100);
          return 0;  // Cancel
        }
        if (touchX >= signUpBtnX && touchX <= (signUpBtnX + signUpBtnW) && touchY >= signUpBtnY && touchY <= (signUpBtnY + signUpBtnH)) {
          uint16_t idToEnroll = nextID;
          if (enrollFingerprint(idToEnroll)) {
            nextID++;  // Increment next ID only if registration succeeded
            Serial.println("Enrollment successful. New ID: " + String(idToEnroll));
            return idToEnroll;
          } else {
            lcd.fillScreen(0);
            lcd.print(20, 40, "Enrollment Failed!", 2, RED, 0);
            lcd.print(20, 70, "Try again.", 2, WHITE, 0);
            Serial.println("Fingerprint enrollment failed.");
            delay(2000);
            return 0;
          }
        }
      }
      delay(50);
    }
  } else {
    lcd.fillScreen(0);
    lcd.print(20, 40, "Identification failed", 2, RED, 0);  // הודעה כללית לכשל זיהוי
    Serial.println("Fingerprint search error code: " + String(searchResult));
    delay(2000);
    return 0;
  }
}

// מנחה את המשתמש בתהליך רישום טביעת אצבע
bool enrollFingerprint(uint8_t id) {
  lcd.fillScreen(0);
  lcd.print(20, 40, "Enroll Finger (1/2)", 2, WHITE, 0);
  lcd.print(20, 70, "Place finger...", 2, YELLOW, 0);
  Serial.println("Enrollment (1/2): Place finger on sensor.");

  int cancelBtnX = 80;
  int cancelBtnY = 180;
  int cancelBtnW = 160;
  int cancelBtnH = 40;
  lcd.fillRoundRect(cancelBtnX, cancelBtnY, cancelBtnW, cancelBtnH, 8, RED);
  lcd.print(cancelBtnX + (cancelBtnW - (6 * FONT_CHAR_WIDTH * 2)) / 2, cancelBtnY + (cancelBtnH - FONT_CHAR_HEIGHT * 2) / 2, "Cancel", 2, WHITE, RED);

  int p;

  // שלב 1: קליטת תמונה ראשונה
  while (true) {
    // בדוק ביטול
    if (lcd.touched()) {
      delay(30);
      lcd.readTouch();
      int touchX = lcd.xTouch;
      int touchY = lcd.yTouch;  // Revert to lcd.yTouch
      while (lcd.touched())
        ;
      if (touchX >= cancelBtnX && touchX <= (cancelBtnX + cancelBtnW) && touchY >= cancelBtnY && touchY <= (cancelBtnY + cancelBtnH)) {
        drawStartScreen();  // חזור ישר למסך ההתחלה
        Serial.println("Enrollment cancelled by user. Returning to start screen.");
        delay(100);
        return false;
      }
    }
    p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      lcd.fillScreen(0);
      lcd.print(20, 40, "Image Captured!", 2, GREEN, 0);
      Serial.println("Image 1 captured.");
      delay(500);
      break;
    } else if (p == FINGERPRINT_NOFINGER) {
      // אין צורך להדפיס הודעות חוזרות ונשנות "No finger detected"
    } else {
      lcd.fillScreen(0);
      lcd.print(20, 40, "Error during enroll!", 2, RED, 0);
      lcd.print(20, 70, "Code: " + String(p), 2, RED, 0);
      Serial.println("Error capturing image 1. Code: " + String(p));
      delay(1500);
      return false;
    }
    delay(50);
  }

  // עיבוד תמונה ראשונה
  int tz1Result = finger.image2Tz(1);
  if (tz1Result != FINGERPRINT_OK) {
    lcd.fillScreen(0);
    lcd.print(20, 40, "Image Error (1/2)", 2, RED, 0);
    lcd.print(20, 70, "Try again.", 2, WHITE, 0);
    Serial.println("Error processing image 1. Code: " + String(tz1Result));
    delay(1000);
    return false;
  }

  // הנחיה להרמת אצבע
  lcd.fillScreen(0);
  lcd.print(20, 40, "Enroll Finger (1/2)", 2, GREEN, 0);
  lcd.print(20, 70, "Lift finger...", 2, YELLOW, 0);
  lcd.fillRoundRect(cancelBtnX, cancelBtnY, cancelBtnW, cancelBtnH, 8, RED);
  lcd.print(cancelBtnX + (cancelBtnW - (6 * FONT_CHAR_WIDTH * 2)) / 2, cancelBtnY + (cancelBtnH - FONT_CHAR_HEIGHT * 2) / 2, "Cancel", 2, WHITE, RED);
  Serial.println("Enrollment (1/2): Lift finger from sensor.");

  while (finger.getImage() != FINGERPRINT_NOFINGER) {
    // בדוק ביטול בזמן המתנה
    if (lcd.touched()) {
      delay(30);
      lcd.readTouch();
      int touchX = lcd.xTouch;
      int touchY = lcd.yTouch;  // Revert to lcd.yTouch
      while (lcd.touched())
        ;
      if (touchX >= cancelBtnX && touchX <= (cancelBtnX + cancelBtnW) && touchY >= cancelBtnY && touchY <= (cancelBtnY + cancelBtnH)) {
        drawStartScreen();  // חזור ישר למסך ההתחלה
        Serial.println("Enrollment cancelled during lift finger. Returning to start screen.");
        delay(100);
        return false;
      }
    }
    delay(10);
  }

  // שלב 2: קליטת תמונה שנייה
  lcd.fillScreen(0);
  lcd.print(20, 40, "Enroll Finger (2/2)", 2, WHITE, 0);
  lcd.print(20, 70, "Place SAME finger!", 2, YELLOW, 0);
  lcd.fillRoundRect(cancelBtnX, cancelBtnY, cancelBtnW, cancelBtnH, 8, RED);
  lcd.print(cancelBtnX + (cancelBtnW - (6 * FONT_CHAR_WIDTH * 2)) / 2, cancelBtnY + (cancelBtnH - FONT_CHAR_HEIGHT * 2) / 2, "Cancel", 2, WHITE, RED);
  Serial.println("Enrollment (2/2): Place SAME finger on sensor.");

  while (true) {
    // בדוק ביטול
    if (lcd.touched()) {
      delay(30);
      lcd.readTouch();
      int touchX = lcd.xTouch;
      int touchY = lcd.yTouch;  // Corrected from lcd.yYouch
      while (lcd.touched())
        ;
      if (touchX >= cancelBtnX && touchX <= (cancelBtnX + cancelBtnW) && touchY >= cancelBtnY && touchY <= (cancelBtnY + cancelBtnH)) {
        drawStartScreen();  // חזור ישר למסך ההתחלה
        Serial.println("Enrollment cancelled during second image capture. Returning to start screen.");
        delay(100);
        return false;
      }
    }
    p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      lcd.fillScreen(0);
      lcd.print(20, 40, "Image Captured!", 2, GREEN, 0);
      Serial.println("Image 2 captured.");
      delay(500);
      break;
    } else if (p == FINGERPRINT_NOFINGER) {
      // אין צורך להדפיס הודעות חוזרות ונשנות "No finger detected"
    } else {
      lcd.fillScreen(0);
      lcd.print(20, 40, "Error during enroll!", 2, RED, 0);
      lcd.print(20, 70, "Code: " + String(p), 2, RED, 0);
      Serial.println("Error capturing image 2. Code: " + String(p));
      delay(1500);
      return false;
    }
    delay(50);
  }

  // עיבוד תמונה שנייה ויצירת מודל
  int tz2Result = finger.image2Tz(2);
  if (tz2Result != FINGERPRINT_OK) {
    lcd.fillScreen(0);
    lcd.print(20, 40, "Image Error (2/2)", 2, RED, 0);
    lcd.print(20, 70, "Try again.", 2, WHITE, 0);
    Serial.println("Error processing image 2. Code: " + String(tz2Result));
    delay(1000);
    return false;
  }

  lcd.fillScreen(0);
  lcd.print(20, 40, "Creating model...", 2, WHITE, 0);
  Serial.println("Creating fingerprint model.");
  int createModelResult = finger.createModel();
  if (createModelResult != FINGERPRINT_OK) {
    lcd.fillScreen(0);
    lcd.print(20, 40, "Mismatch Error!", 2, RED, 0);
    if (createModelResult == FINGERPRINT_ENROLLMISMATCH) {
      lcd.print(20, 70, "Fingers don't match!", 2, RED, 0);
      Serial.println("Enrollment mismatch: fingers don't match.");
    } else {
      lcd.print(20, 70, "Enrollment failed!", 2, RED, 0);
      Serial.println("Enrollment failed. Code: " + String(createModelResult));
    }
    delay(2000);
    return false;
  }

  lcd.fillScreen(0);
  lcd.print(20, 40, "Saving fingerprint...", 2, WHITE, 0);
  Serial.println("Saving fingerprint model with ID: " + String(id));
  int storeModelResult = finger.storeModel(id);
  bool store_ok = (storeModelResult == FINGERPRINT_OK);

  if (!store_ok) {
    lcd.fillScreen(0);
    lcd.print(20, 40, "Save Failed!", 2, RED, 0);
    if (storeModelResult == FINGERPRINT_BADLOCATION) {
      lcd.print(20, 70, "Invalid ID!", 2, RED, 0);
      Serial.println("Save failed: Invalid ID. Code: " + String(storeModelResult));
    } else {
      lcd.print(20, 70, "Memory full/error!", 2, RED, 0);
      Serial.println("Save failed: Memory full or other error. Code: " + String(storeModelResult));
    }
    delay(2000);
  } else {
    lcd.fillScreen(0);
    lcd.print(20, 40, "Fingerprint Saved!", 2, GREEN, 0);
    Serial.println("Fingerprint saved successfully.");
    delay(1000);
  }
  return store_ok;
}

// מצייר את מסך ההתחלה (אלמנטים סטטיים)
void drawStartScreen() {
  lcd.fillScreen(BLACK);  // רקע שחור

  digitalWrite(redLedPin, LOW);
  digitalWrite(yellowLedPin, LOW);
  digitalWrite(greenLedPin, LOW);

  // הגדרת מיקום המספר (currentQueueNumber)
  int numberFontSize = 7;
  int numberY = (lcd.height() / 2) - (FONT_CHAR_HEIGHT * numberFontSize) / 2 - 80;  // הזזה של 80 פיקסלים למעלה מהמרכז
  // רוחב מקסימלי למספר (לדוגמה, 3 ספרות) כדי לנקות מספיק שטח
  int maxNumberWidth = (String("999").length() * FONT_CHAR_WIDTH * numberFontSize) + (FONT_CHAR_WIDTH * numberFontSize);
  int numberX = (lcd.width() - maxNumberWidth) / 2;  // מרכוז לפי הרוחב המקסימלי

  // טקסט "This is your turn" (סטטי)
  String turnText = "This is your turn";
  int textFontSize = 2;
  int textWidth = turnText.length() * FONT_CHAR_WIDTH * textFontSize;
  // ממקם את הטקסט מתחת למספר, עם מרווח קבוע
  int textY = numberY + (FONT_CHAR_HEIGHT * numberFontSize) + 15;  // 15 פיקסלים מתחת למספר
  lcd.print((lcd.width() - textWidth) / 2, textY, turnText, textFontSize, YELLOW, BLACK);

  // כפתור "Click Here" (סטטי)
  lcd.fillRoundRect(50, lcd.height() - 70, 220, 60, 8, BLUE);  // מיקום בתחתית המסך
  String buttonText = "Click Here";
  int buttonTextFontSize = 2;
  int buttonTextX = 50 + (220 - (buttonText.length() * FONT_CHAR_WIDTH * buttonTextFontSize)) / 2;
  int buttonTextY = lcd.height() - 70 + (60 - FONT_CHAR_HEIGHT * buttonTextFontSize) / 2;
  lcd.print(buttonTextX, buttonTextY, buttonText, buttonTextFontSize, WHITE, BLUE);

  if (Firebase.ready()) {
    Serial.println("ESP: Immediate fetch on entering Start Screen.");
    if (Firebase.RTDB.getInt(&fbdo, "/queue/currentServingNumber")) {
      currentQueueNumber = fbdo.intData();
      Serial.println("Fetched currentServingNumber: " + String(currentQueueNumber));
    } else {
      Serial.print("Firebase Error fetching currentServingNumber: ");
      Serial.println(fbdo.errorReason());
    }
    if (Firebase.RTDB.getInt(&fbdo, "/queue/lastIssuedNumber")) {
      lastIssuedNumber = fbdo.intData();
      Serial.println("Fetched lastIssuedNumber: " + String(lastIssuedNumber));
    } else {
      Serial.print("Firebase Error fetching lastIssuedNumber on start screen: ");
      Serial.println(fbdo.errorReason());
    }
    if (Firebase.RTDB.getInt(&fbdo, "/queue/currentServingTimestamp")) {  
      currentServingTimestamp = fbdo.intData();                          
      Serial.println("Fetched currentServingTimestamp (Unix ms) on start screen: " + String(currentServingTimestamp));
    } else {
      Serial.print("Firebase Error fetching currentServingTimestamp on start screen: ");
      Serial.println(fbdo.errorReason());
    }
    // Immediate activity report on entering start screen
    unsigned long currentUnixMs = getCurrentUnixTime();
    if (currentUnixMs > 0) {
      if (Firebase.RTDB.setInt(&fbdo, "/queue/lastEspActivityTimestamp", currentUnixMs)) { 
        Serial.println("ESP activity timestamp updated successfully on start screen entry.");
      } else {
        Serial.print("Firebase Error updating lastEspActivityTimestamp on start screen entry: ");
        Serial.println(fbdo.errorReason());
      }
    } else {
      Serial.println("Warning: Could not get valid Unix time for lastEspActivityTimestamp update on start screen entry.");
    }
  }

  // Initial call to update turn number (will use fetched value)
  updateQueueNumberDisplay();
  lastPeriodicFetchTime = millis();  // Reset the periodic fetch timer
}

// פונקציה לעדכון תצוגת מספר התור בלבד
void updateQueueNumberDisplay() {
  static int displayedQueueNumber = -1;  // משתנה סטטי לשמירת המספר המוצג כרגע

  // רק אם המספר החדש שונה מהמספר המוצג, נעדכן את התצוגה
  if (currentQueueNumber != displayedQueueNumber) {
    // מיקום וגודל של אזור המספר
    int numberFontSize = 7;
    // ממקם את המספר מעט יותר למעלה, כמו ב-drawStartScreen
    int numberY = (lcd.height() / 2) - (FONT_CHAR_HEIGHT * numberFontSize) / 2 - 80;

    // רוחב מקסימלי למספר (לדוגמה, 3 ספרות) כדי לנקות מספיק שטח
    int maxNumberWidth = (String("999").length() * FONT_CHAR_WIDTH * numberFontSize) + (FONT_CHAR_WIDTH * numberFontSize);
    int numberX = (lcd.width() - maxNumberWidth) / 2;

    // מנקה רק את האזור שבו מוצג המספר
    lcd.fillRect(numberX, numberY, maxNumberWidth, FONT_CHAR_HEIGHT * numberFontSize, BLACK);

    // מצייר את המספר החדש
    String turnNumber = String(currentQueueNumber);
    int actualNumberWidth = turnNumber.length() * FONT_CHAR_WIDTH * numberFontSize;
    int actualNumberX = (lcd.width() - actualNumberWidth) / 2;
    lcd.print(actualNumberX, numberY, turnNumber, numberFontSize, WHITE, BLACK);

    displayedQueueNumber = currentQueueNumber;  // עדכן את המספר המוצג
  }
}


// מצייר את מסך המשחק (ברוכים הבאים לשחקן)
void drawGameScreen(bool isExistingUser) {
  lcd.fillScreen(0);
  Player* player = getPlayer(currentPlayerID);
  if (player != nullptr) {
    String welcomeText = "Hello " + player->name;
    String winsText = "Wins: " + String(player->wins);
    int textFontSize = 2;
    lcd.print(10, 40, welcomeText, textFontSize, WHITE, 0);
    lcd.print(10, 70, winsText, textFontSize, WHITE, 0);
    Serial.println("Displaying game screen for player: " + player->name + ", Wins: " + String(player->wins));
  } else {
    lcd.print(10, 40, "Error: Player not found!", 2, RED, 0);
    Serial.println("Error: Player not found for game screen.");
  }

  // כפתור "Start Game"
  String startGameButtonText = "Start Game";
  int startGameButtonColor = BLUE;
  lcd.fillRoundRect(50, 120, 220, 70, 8, BLUE);
  int startGameButtonTextFontSize = 2;
  int startGameButtonTextX = 50 + (220 - (startGameButtonText.length() * FONT_CHAR_WIDTH * startGameButtonTextFontSize)) / 2;
  int startGameButtonTextY = 150;
  lcd.print(startGameButtonTextX, startGameButtonTextY, startGameButtonText, startGameButtonTextFontSize, WHITE, startGameButtonColor);

  // כפתור "Cancel"
  String cancelButtonText = "Cancel";
  int cancelButtonColor = RED;
  int cancelButtonX = 50;
  int cancelButtonY = 200;
  int cancelButtonW = 220;
  int cancelButtonH = 40;
  lcd.fillRoundRect(cancelButtonX, cancelButtonY, cancelButtonW, cancelButtonH, 8, cancelButtonColor);
  int cancelButtonTextFontSize = 2;
  int cancelButtonTextX = cancelButtonX + (cancelButtonW - (cancelButtonText.length() * FONT_CHAR_WIDTH * cancelButtonTextFontSize)) / 2;
  int cancelButtonTextY = cancelButtonY + (cancelButtonH - FONT_CHAR_HEIGHT * cancelButtonTextFontSize) / 2;
  lcd.print(cancelButtonTextX, cancelButtonTextY, cancelButtonText, cancelButtonTextFontSize, WHITE, cancelButtonColor);
}

// מצייר את מסך המשחק הפעיל עם הטיימר
void drawActiveGameScreen(unsigned long timeLeft) {
  lcd.fillScreen(BLACK);  // נקה את כל המסך לפני ציור הטיימר

  // מגדיר גודל פונט גדול יותר עבור השניות
  int largeFontSize = 7;  // גודל פונט גדול מאוד
  String timeLeftStr = String(timeLeft) + "s";

  // מחשב מיקום לטקסט ממורכז על כל המסך
  int textWidth = timeLeftStr.length() * FONT_CHAR_WIDTH * largeFontSize;
  int textHeight = FONT_CHAR_HEIGHT * largeFontSize;
  int textX = (lcd.width() - textWidth) / 2;
  int textY = (lcd.height() - textHeight) / 2;

  // מצייר את השניות בצהוב ובגדול במרכז המסך
  lcd.print(textX, textY, timeLeftStr, largeFontSize, YELLOW, BLACK);
}

// פונקציה לכיול הסרוו הראשי (ממתק)
void calibrateServo() {
  Serial.println("\n--- Servo 1 (Candy Dispenser) Calibration Mode ---");
  Serial.println("Adjust SERVO_STOP_PULSE in code.");
  Serial.println("Current SERVO_STOP_PULSE: " + String(SERVO_STOP_PULSE) + "us");
  Serial.println("Check if servo is creeping. Press any key in Serial Monitor to change pulse.");
  Serial.println("Type '0' to stop, '1' for dispense pulse (1000us), '2' for reverse (2000us), or a number for specific pulse.");

  myServo.writeMicroseconds(SERVO_STOP_PULSE);  // התחל במצב עצירה
  long lastPulse = SERVO_STOP_PULSE;

  while (true) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      long newPulse = input.toInt();

      if (newPulse == 0) {
        newPulse = SERVO_STOP_PULSE;
      } else if (newPulse == 1) {
        newPulse = SERVO_DISPENSE_PULSE;
      } else if (newPulse == 2) {
        newPulse = 2000;
      }

      if (newPulse >= 500 && newPulse <= 2500) {
        myServo.writeMicroseconds(newPulse);
        lastPulse = newPulse;
        Serial.println("Servo 1 pulse set to: " + String(newPulse) + "us");
      } else {
        Serial.println("Invalid pulse value. Enter a number between 500 and 2500.");
      }
    }
    delay(100);
  }
}

// פונקציה לכיול הסרוו השני (פתיחה/סגירה)
// void calibrateSecondServo() {
//   Serial.println("\n--- Servo 2 (Open/Close) Calibration Mode ---");
//   Serial.println("Adjust SECOND_SERVO_OPEN_PULSE, SECOND_SERVO_CLOSE_PULSE in code.");
//   Serial.println("Current Open Pulse: " + String(SECOND_SERVO_OPEN_PULSE) + "us.");
//   Serial.println("Current Close Pulse: " + String(SECOND_SERVO_CLOSE_PULSE) + "us.");
//   Serial.println("Type 'open' to open, 'close' to close, or a number for specific pulse.");
//   Serial.println("Type 'stop' to stop (1500us).");

//   // mySecondServo.writeMicroseconds(SERVO_STOP_PULSE);
//   long currentTestPulse = SERVO_STOP_PULSE;

//   while (true) {
//     if (Serial.available()) {
//       String input = Serial.readStringUntil('\n');
//       input.trim();
//       input.toLowerCase();  // המר לאותיות קטנות

//       if (input == "open") {
//         // mySecondServo.writeMicroseconds(SECOND_SERVO_OPEN_PULSE);
//         currentTestPulse = SECOND_SERVO_OPEN_PULSE;
//         Serial.println("Servo 2: Opening (" + String(currentTestPulse) + "us)");
//       } else if (input == "close") {
//         // mySecondServo.writeMicroseconds(SECOND_SERVO_CLOSE_PULSE);
//         currentTestPulse = SECOND_SERVO_CLOSE_PULSE;
//         Serial.println("Servo 2: Closing (" + String(currentTestPulse) + "us)");
//       } else {
//         long newPulse = input.toInt();
//         if (newPulse >= 500 && newPulse <= 2500) {  // טווח פולסים תקין
//           // mySecondServo.writeMicroseconds(newPulse);
//           currentTestPulse = newPulse;
//           Serial.println("Servo 2: Custom pulse: " + String(currentTestPulse) + "us");
//         } else {
//           Serial.println("Invalid input. Enter 'open', 'close', 'stop' or a number between 500 and 2500.");
//         }
//       }
//     }
//     delay(100);
//   }
// }

// פונקציית מנגינת ניצחון
void playVictoryTune() {
  tone(buzzerPin, 523, 150);  // C5
  delay(150);
  tone(buzzerPin, 659, 150);  // E5
  delay(150);
  tone(buzzerPin, 784, 150);  // G5
  delay(150);
  tone(buzzerPin, 1047, 300);  // C6
  delay(300);
  noTone(buzzerPin);
  Serial.println("Playing victory tune.");
}

// פונקציית מנגינת הפסד
void playLossTune() {
  tone(buzzerPin, 392, 200);  // G4
  delay(200);
  tone(buzzerPin, 330, 200);  // E4
  delay(200);
  tone(buzzerPin, 262, 400);  // C4
  delay(400);
  noTone(buzzerPin);
  Serial.println("Playing loss tune.");
}

// פונקציה לקבלת זמן יוניקס נוכחי במילישניות
unsigned long getCurrentUnixTime() {
  time_t now;
  struct tm timeinfo;
  // נסה לקבל זמן, אם נכשל, נסה שוב עד 3 פעמים
  for (int i = 0; i < 3; i++) {
    if (getLocalTime(&timeinfo)) {
      time(&now);
      return (unsigned long)now * 1000UL;  // המר לשניות ואז למילישניות
    }
    delay(100);  // המתן קצת לפני ניסיון חוזר
  }
  Serial.println("Failed to obtain time from NTP after retries.");
  return 0;  // החזר 0 אם לא הצלחנו לקבל זמן תקין
}

// פונקציה לקידום התור ב-Firebase
void advanceQueue() {
  if (!Firebase.ready()) {
    Serial.println("Firebase not ready, cannot advance queue.");
    return;
  }

  int currentNum = 0;
  int lastIssuedNum = 0;

  // קריאת currentServingNumber
  if (Firebase.RTDB.getInt(&fbdo, "/queue/currentServingNumber")) {
    currentNum = fbdo.intData();
  } else {
    Serial.println("Failed to read currentServingNumber for advancement.");
    return;
  }

  // קריאת lastIssuedNumber
  if (Firebase.RTDB.getInt(&fbdo, "/queue/lastIssuedNumber")) {
    lastIssuedNum = fbdo.intData();
  } else {
    Serial.println("Failed to read lastIssuedNumber for advancement.");
    return;
  }

  // קדם תור רק אם התור הנוכחי קטן מהתור האחרון שהונפק
  if (currentNum < lastIssuedNum) {
    // עדכון currentServingNumber
    if (Firebase.RTDB.setInt(&fbdo, "/queue/currentServingNumber", currentNum + 1)) {
      Serial.println("ESP: Advancing queue. New serving number: " + String(currentNum + 1));
      // עדכן משתנה לוקאלי מיד לאחר כתיבה מוצלחת ל-Firebase
      currentQueueNumber = currentNum + 1;
    } else {
      Serial.printf("Firebase Error updating currentServingNumber: %s\n", fbdo.errorReason().c_str());
    }

    // עדכון currentServingTimestamp לזמן יוניקס נוכחי
    unsigned long currentUnixMs = getCurrentUnixTime();
    if (currentUnixMs > 0) {                                                               // וודא שקיבלנו זמן תקין
      if (Firebase.RTDB.setInt(&fbdo, "/queue/currentServingTimestamp", currentUnixMs)) {  
        Serial.println("ESP: Updated currentServingTimestamp with current Unix time.");
        // עדכן משתנה לוקאלי מיד לאחר כתיבה מוצלחת ל-Firebase
        currentServingTimestamp = currentUnixMs;
      } else {
        Serial.printf("Firebase Error updating currentServingTimestamp: %s\n", fbdo.errorReason().c_str());
      }
    } else {
      Serial.println("Warning: Could not get valid Unix time for currentServingTimestamp update.");
    }
  } else {
    Serial.println("Queue is already at the last issued number. No advancement needed.");
  }
}


void setup() {
  Serial.begin(115200);
  lcd.begin();
  lcd.setRotation(2);

  // lcd.fillScreen(BLUE);
  // lcd.print(20, 100, "Initializing...", 2, WHITE, BLUE);
  // delay(3000);

  lcd.setTouch(3780, 372, 489, 3811);
  setupFingerprintSensor();
  // אתחול תקשורת עם ארדואינו
  // ESP32 TX26 Arduino RX D7
  // ESP32 RX25 Arduino TX D8
  arduinoSerial.begin(115200, SERIAL_8N1, 26, 25);

  pinMode(touchPin, INPUT);
  lastTouchState = digitalRead(touchPin);

  // pinMode(TRIG_PIN, OUTPUT);
  // pinMode(ECHO_PIN, INPUT);

  myServo.attach(servoPin);
  myServo.writeMicroseconds(SERVO_STOP_PULSE);  // לעצור את הסרוו בהתחלה

  // אתחול את הסרוו השני (פתיחה/סגירה)
  // mySecondServo.attach(secondServoPin);
  // mySecondServo.writeMicroseconds(SERVO_STOP_PULSE);

  pinMode(buzzerPin, OUTPUT);
  noTone(buzzerPin);

  // אתחול את מערך השחקנים בקיבולת התחלתית
  initializePlayersArray(5);  // התחל עם קיבולת של 5 שחקנים

  // Firebase הגדרות
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long wifiConnectStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiConnectStart <= 30000) {
    Serial.print(".");
    delay(500);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed.");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("Connected to Wi-Fi");
    // סנכרן זמן עם NTP
    Serial.println("Configuring NTP time...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    // המתן עד לקבלת זמן תקין
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retryCount = 0;
    const int maxRetries = 10;
    while (!getLocalTime(&timeinfo) && retryCount < maxRetries) {
      Serial.print(".");
      delay(1000);
      retryCount++;
    }
    if (retryCount == maxRetries) {
      Serial.println("\nFailed to obtain time from NTP after multiple retries.");
    } else {
      Serial.println("\nTime synchronized with NTP.");
      Serial.printf("Current Unix time: %lu\n", getCurrentUnixTime());
    }
  } else {
    Serial.println("Not connected to WiFi, Firebase will not work.");
  }

  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_DATABASE_URL;

  // אימות ESP32 עם משתמש Firebase ייעודי
  auth.user.email = FIREBASE_ESP32_EMAIL;
  auth.user.password = FIREBASE_ESP32_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // לוודא שהאימות הושלם
  Serial.println("Signing in with dedicated ESP32 user...");
  //מונע קידום מיידי בתור בגלל חותמת זמן ישנה לאחר חיבור מחדש/הפעלה מחדש
  if (Firebase.ready()) {
    unsigned long currentUnixMs = getCurrentUnixTime();
    if (currentUnixMs > 0) {
      if (Firebase.RTDB.setInt(&fbdo, "/queue/currentServingTimestamp", currentUnixMs)) { 
        Serial.println("ESP: Updated currentServingTimestamp to current Unix time on connection.");
      } else {
        Serial.printf("Firebase Error updating currentServingTimestamp on connect: %s\n", fbdo.errorReason().c_str());
      }
    } else {
      Serial.println("Warning: Could not get valid Unix time for currentServingTimestamp update on connect.");
    }
  }

  pinMode(redLedPin, OUTPUT);
  pinMode(yellowLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);
  digitalWrite(redLedPin, LOW);
  digitalWrite(yellowLedPin, LOW);
  digitalWrite(greenLedPin, LOW);

  delay(2000);  // לוודא שהFIREBASE מוכן

  // ציור ראשוני של מסך ההתחלה. המספר יעודכן לאחר שליפת הנתונים הראשונה.
  drawStartScreen();
}

void loop() {
  static State lastState = currentState;

  // טיפול במגע במסך
  if (lcd.touched()) {
    delay(30);  // ניתוק
    lcd.readTouch();
    xTouch = lcd.xTouch;
    yTouch = lcd.yTouch;
    // המתן לשחרור האצבע
    while (lcd.touched()) {
      delay(5);
    }

    if (currentState == WAIT_START_PRESS) {
      // כפתור "Click Here" נלחץ (מיקום מעודכן)
      if (xTouch >= 50 && xTouch <= 270 && yTouch >= (lcd.height() - 70) && yTouch <= (lcd.height() - 70 + 60)) {
        currentState = WAIT_FINGER;
        Serial.println("Start button pressed. Waiting for fingerprint scan.");
      }
    } else if (currentState == REGISTER_NAME) {
      handleKeyboardTouch(xTouch, yTouch);  // קלט מקלדת
    } else if (currentState == PLAY_GAME) {
      // כפתור "Start Game" נלחץ
      if (xTouch >= 50 && xTouch <= 270 && yTouch >= 120 && yTouch <= 190) {
        if (playerExists(currentPlayerID)) {
          Player* player = getPlayer(currentPlayerID);
          // חישוב זמן המשחק לפי מספר הניצחונות
          currentGameDuration = MAX_GAME_DURATION_SECONDS - (player->wins * REDUCTION_PER_WIN_SECOND);
          if (currentGameDuration < MIN_GAME_DURATION_SECONDS) {
            currentGameDuration = MIN_GAME_DURATION_SECONDS;  // לוודא שהזמן לא יורד מתחת למינימום
          }

          // אם התור הנוכחי הוא 0 (טרם התחיל אף משחק) וגם (lastIssuedNumber הוא 0 או 1)
          // זה מכסה את המקרה של המשחק הראשון אי פעם.
          if (currentQueueNumber == 0 && (lastIssuedNumber == 0 || lastIssuedNumber == 1)) {
            Serial.println("ESP: First game starting, initializing currentServingNumber to 1.");
            if (Firebase.RTDB.setInt(&fbdo, "/queue/currentServingNumber", 1)) {
              currentQueueNumber = 1;  // עדכן משתנה לוקאלי מיד
              Serial.println("ESP: currentServingNumber set to 1 in Firebase.");
            } else {
              Serial.printf("Firebase Error setting currentServingNumber to 1: %s\n", fbdo.errorReason().c_str());
            }
          }

          unsigned long currentUnixMs = getCurrentUnixTime();
          if (currentUnixMs > 0) {
            if (Firebase.RTDB.setInt(&fbdo, "/queue/currentServingTimestamp", currentUnixMs)) { 
              Serial.println("ESP: Game started, updated currentServingTimestamp in Firebase.");
              currentServingTimestamp = currentUnixMs; 
            } else {
              Serial.printf("Firebase Error updating currentServingTimestamp on game start: %s\n", fbdo.errorReason().c_str());
            }
          } else {
            Serial.println("Warning: Could not get valid Unix time for currentServingTimestamp on game start.");
          }
          sendStartCommand();

          //הפעלת סרוו שני לפתיחה בדיוק כשהטיימר מתחיל
          // mySecondServo.writeMicroseconds(SECOND_SERVO_OPEN_PULSE);
          // delay(SECOND_SERVO_OPEN_DURATION_MS);
          // mySecondServo.writeMicroseconds(SERVO_STOP_PULSE);
          // Serial.println("Second servo opened.");

          gameStartTime = millis();  // התחל טיימר פנימי של ESP למשך המשחק
          lastTimerUpdateTime = millis();
          displayedTimeLeft = currentGameDuration;    // עדכן את הזמן המוצג
          drawActiveGameScreen(currentGameDuration); 

          currentState = IN_GAME_ACTIVE;

        } else {  // משתמש חדש
          Serial.println("Error: Player not found when trying to start game.");
          drawStartScreen();  // חזור למסך ההתחלה
          currentState = WAIT_START_PRESS;
        }
      } else if (xTouch >= 50 && xTouch <= 270 && yTouch >= 200 && yTouch <= 240) {  // כפתור "Cancel" נלחץ
        drawStartScreen();                                                           // חזור למסך ההתחלה
        currentState = WAIT_START_PRESS;
        Serial.println("Game start cancelled. Returning to start screen.");
      }
    }
    // לחיצה על המסך בזמן סיום משחק תחזיר להתחלה
    else if (currentState == GAME_OVER_CHECK_TOUCH) {
      Serial.println("Returning to start screen after touch during game over.");
      drawStartScreen();
      currentState = WAIT_START_PRESS;
    }
  }

  // טיפול במעבר מצבים וציור מסכים
  if (currentState != lastState) {
    if (currentState == WAIT_START_PRESS) {
      drawStartScreen();

    } else if (currentState == REGISTER_NAME) {
      drawKeyboard();
      drawNameBox();

    } else if (currentState == PLAY_GAME) {
      drawGameScreen(true);

    } else if (currentState == GAME_OVER_CHECK_TOUCH) {
      lcd.fillScreen(0);
      lcd.print(20, 80, "GAME OVER!", 2, RED, 0);
      lcd.print(20, 110, "Checking if you", 2, WHITE, 0);
      lcd.print(20, 140, "caught a candy!", 2, WHITE, 0);
      touchCheckStartTime = millis();
    }
  }

  // if (currentState != lastState) {
  //   switch (currentState) {
  //     case WAIT_START_PRESS:
  //       drawStartScreen();
  //       break;
  //     case WAIT_FINGER:
  //       // getOrRegisterFingerprint() מטפלת בציור המסך שלה
  //       break;
  //     case REGISTER_NAME:
  //       drawKeyboard();
  //       drawNameBox();
  //       break;
  //     case PLAY_GAME:
  //       drawGameScreen(true);  // מניח נכון עבור משתמש קיים, יטופל על ידי getOrRegisterFingerprint
  //       break;
  //     case IN_GAME_ACTIVE:
  //       // drawActiveGameScreen() נקראת בלולאה באופן רציף
  //       break;
  //     case GAME_OVER_CHECK_TOUCH:
  //       lcd.fillScreen(0);
  //       lcd.print(20, 80, "GAME OVER!", 2, RED, 0);
  //       lcd.print(20, 110, "Checking if you", 2, WHITE, 0);
  //       lcd.print(20, 140, "caught a candy!", 2, WHITE, 0);
  //       touchCheckStartTime = millis();
  //       break;
  //   }
  // }
  lastState = currentState;  // עדכן את המצב הקודם בסוף טיפול במגע ומעברי מצב

  // טיפול במצב WAIT_FINGER (רצף)
  if (currentState == WAIT_FINGER) {
    uint16_t id = getOrRegisterFingerprint();  // מטפל גם בביטול
    if (id > 0) {                              // זוהה או נרשם חדש
      currentPlayerID = id;
      if (playerExists(id)) {
        currentState = PLAY_GAME;
        Serial.println("Existing player identified. ID: " + String(id));
      } else {
        currentName = "";
        currentState = REGISTER_NAME;
        Serial.println("New player identified. Prompting for name registration.");
      }
    } else {  // כשל או בוטל
      currentState = WAIT_START_PRESS;
      Serial.println("Fingerprint scan failed or cancelled. Returning to start screen.");
    }
  }

  // לוגיקת טיימר בזמן משחק פעיל
  if (currentState == IN_GAME_ACTIVE) {
    unsigned long elapsedTime = (millis() - gameStartTime) / 1000;
    unsigned long timeLeft = currentGameDuration - elapsedTime;  // השתמש בזמן המשחק הנוכחי

    if (timeLeft > 0) {
      digitalWrite(yellowLedPin, HIGH);
      digitalWrite(redLedPin, LOW);
      digitalWrite(greenLedPin, LOW);
      // עדכן טיימר על המסך כל שנייה, או אם הערך השתנה
      if (millis() - lastTimerUpdateTime >= 1000 || timeLeft != displayedTimeLeft) {
        drawActiveGameScreen(timeLeft);  // קריאה לפונקציה שתצייר את המסך עם הטיימר בלבד
        displayedTimeLeft = timeLeft;
        lastTimerUpdateTime = millis();
      }
    } else {
      // סיום זמן המשחק
      sendStopCommand();

      // mySecondServo.writeMicroseconds(SECOND_SERVO_CLOSE_PULSE);
      // delay(SECOND_SERVO_CLOSE_DURATION_MS);
      // mySecondServo.writeMicroseconds(SERVO_STOP_PULSE);
      // Serial.println("Game timer ended. Second servo closed.");

      touchCheckStartTime = millis();
      currentState = GAME_OVER_CHECK_TOUCH;
    }
  }

  // לוגיקת בדיקת חיישן מגע בסיום המשחק
  if (currentState == GAME_OVER_CHECK_TOUCH) {
    int currentTouchState = digitalRead(touchPin);
    if (currentTouchState == HIGH) {              // יש מגע (ממתק זוהה)
      if (currentTouchState != lastTouchState) {  // רק אם מצב השתנה ל-HIGH
        // להוסיף בדיקת חיישן מרחק כאן
        String distanceStatus = getDistanceFromArduino();  // קבל את מצב המרחק מהארדואינו
        if (distanceStatus == "DISTANCE_CLOSE") {
          Serial.println("Less than 5 cm (from Arduino)");
          // ניתן להוסיף כאן גם תצוגה על המסך אם רוצים
        } else if (distanceStatus == "DISTANCE_FAR") {
          Serial.println("More than 5 cm (from Arduino)");
          // אפשרות לטפל במצב זה, למשל להמתין עוד או להחשיב כהפסד
        } else {
          Serial.println("error getting data from arduino");
        }

        lcd.fillScreen(0);
        lcd.print(20, 80, "GAME OVER!", 2, RED, 0);
        lcd.print(20, 110, "Well done!", 2, GREEN, 0);
        lcd.print(20, 140, "Collect your candy!", 2, GREEN, 0);
        Serial.println("Candy detected! Victory!");
        digitalWrite(greenLedPin, HIGH);
        digitalWrite(yellowLedPin, LOW);
        digitalWrite(redLedPin, LOW);
        playVictoryTune(); 

        Player* player = getPlayer(currentPlayerID);
        if (player != nullptr) {
          player->wins++;  // הגדל ניצחונות
          Serial.println("Player " + player->name + " now has " + String(player->wins) + " wins.");
        }

        // הפעלת סרוו לשחרור ממתק
        myServo.writeMicroseconds(SERVO_DISPENSE_PULSE);  // התחל סיבוב
        Serial.println("Dispensing candy...");
        delay(SERVO_DISPENSE_DURATION_MS);           
        myServo.writeMicroseconds(SERVO_STOP_PULSE); 

        delay(10000);  // לחכות 10 שניות (הסרוו לא זז במהלך זמן זה)
        Serial.println("Waiting 10 seconds after dispense.");

        // לסובב את הסרוו הראשי חזרה למיקום ההתחלתי
        myServo.writeMicroseconds(SERVO_RETURN_PULSE);  // סובב לאחור
        delay(SERVO_RETURN_DURATION_MS);                // משך הסיבוב לאחור
        myServo.writeMicroseconds(SERVO_STOP_PULSE);    // עצור בחזרה
        Serial.println("Servo 1 returned to initial position.");

        advanceQueue();  // קדם תור לאחר איסוף ממתק וחזרת סרוו
        currentState = WAIT_START_PRESS;
      }
    } else {                                      // אין מגע
      if (currentTouchState != lastTouchState) {  // רק אם מצב השתנה ל-LOW (לא היה מגע)
        Serial.println("No candy detected, waiting for timeout.");
      }
    }
    lastTouchState = currentTouchState;  // עדכן מצב קודם

    // בדיקת Timeout אם לא היה מגע או לחיצה
    if (millis() - touchCheckStartTime >= TOUCH_SENSOR_WAIT_TIMEOUT_SECONDS * 1000) {
      if (currentTouchState == LOW) {  // וודא שאין מגע גם בסיום ה-timeout
        lcd.fillScreen(0);
        lcd.print(20, 100, "Timeout!", 2, YELLOW, 0);
        lcd.print(20, 130, "No candy detected.", 2, YELLOW, 0);
        Serial.println("Timeout: No candy detected. Loss.");
        digitalWrite(redLedPin, HIGH);
        digitalWrite(yellowLedPin, LOW);
        digitalWrite(greenLedPin, LOW);
        playLossTune();
        delay(1500);     // השהיה קצרה להצגת הודעת ה-timeout
        advanceQueue();  // קדם תור לאחר timeout ומנגינת הפסד
        currentState = WAIT_START_PRESS;
      }
    }
  }

  delay(10);  // השהיה כללית בלולאה

  // לוגיקה לשליפת נתונים מ-Firebase באופן תקופתי
  // וגם דיווח על פעילות ה-ESP
  if (Firebase.ready()) {
    if (currentState == WAIT_START_PRESS) {
      if (millis() - lastPeriodicFetchTime > periodicFetchInterval) {
        lastPeriodicFetchTime = millis();
        Serial.println("ESP: Periodic fetch and activity report initiated.");

        if (Firebase.RTDB.getInt(&fbdo, "/queue/currentServingNumber")) {
          currentQueueNumber = fbdo.intData();
          Serial.println("Fetched currentServingNumber: " + String(currentQueueNumber));
          updateQueueNumberDisplay();  // עדכן תצוגה אם במסך הבית
        } else {
          Serial.print("Firebase Error fetching currentServingNumber: ");
          Serial.println(fbdo.errorReason());
        }

        if (Firebase.RTDB.getInt(&fbdo, "/queue/lastIssuedNumber")) {
          lastIssuedNumber = fbdo.intData();
          Serial.println("Fetched lastIssuedNumber: " + String(lastIssuedNumber));
        } else {
          Serial.print("Firebase Error fetching lastIssuedNumber: ");
          Serial.println(fbdo.errorReason());
        }

        if (Firebase.RTDB.getInt(&fbdo, "/queue/currentServingTimestamp")) {  
          currentServingTimestamp = fbdo.intData();                          
          Serial.println("Fetched currentServingTimestamp (Unix ms): " + String(currentServingTimestamp));
        } else {
          Serial.print("Firebase Error fetching currentServingTimestamp: ");
          Serial.println(fbdo.errorReason());
        }

        unsigned long currentUnixMs = getCurrentUnixTime();
        if (currentUnixMs > 0) {
          if (Firebase.RTDB.setInt(&fbdo, "/queue/lastEspActivityTimestamp", currentUnixMs)) {  
            Serial.println("ESP activity timestamp updated successfully.");
          } else {
            Serial.print("Firebase Error updating lastEspActivityTimestamp: ");
            Serial.println(fbdo.errorReason());
          }
        } else {
          Serial.println("Warning: Could not get valid Unix time for lastEspActivityTimestamp update.");
        }
      }
    }

    if (currentState == WAIT_START_PRESS && currentQueueNumber > 0 && currentQueueNumber < lastIssuedNumber) {

      if (currentServingTimestamp > 0) {
        unsigned long currentUnixMs = getCurrentUnixTime();
        if (currentUnixMs > 0) {
          // חישוב הזמן שחלף מאז תחילת התור הנוכחי (במילישניות של יוניקס)
          unsigned long timeSinceCurrentTurnStarted = currentUnixMs - currentServingTimestamp;

          // Serial.printf("DEBUG: Timeout check: currentQueueNumber=%d, lastIssuedNumber=%d, currentServingTimestamp=%lu, currentUnixMs=%lu, timeSinceCurrentTurnStarted=%lu\n",
          //               currentQueueNumber, lastIssuedNumber, currentServingTimestamp, currentUnixMs, timeSinceCurrentTurnStarted);

          // אם עברו 2 דקות והתור הנוכחי עדיין לא התחיל משחק
          if (timeSinceCurrentTurnStarted >= (2 * 60 * 1000UL)) {  // 2 דקות במילישניות
            Serial.println("ESP: 2 minutes timeout for current turn (not started game). Advancing queue.");
            advanceQueue();
          }
        } else {
          Serial.println("Warning: Could not get valid Unix time for timeout check.");
        }
      }
    }
  }
}
