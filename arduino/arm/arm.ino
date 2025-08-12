#include <SoftwareSerial.h>
#include <Servo.h>
 
// הגדרות פינים לחיישן מרחק אולטרה-סוניק
#define TRIG_PIN 4 
#define ECHO_PIN 2 

SoftwareSerial espSerial(7, 8);  // RX=D7, TX=D8 
Servo servo1, servo2, servo3, servo4, servo5;
bool potentiometerControlEnabled = false;

void setup() {
  Serial.begin(115200);   
  espSerial.begin(115200); 

  // הגדרת פינים לחיישן מרחק
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  servo1.attach(9);
  servo2.attach(6);
  servo3.attach(5);
  servo4.attach(3);
  servo5.attach(11);
  
  resetServos();
  Serial.println("Arduino Ready. Waiting for ESP32 commands...");
}

void loop() {
  if (espSerial.available()) {
    String command = espSerial.readStringUntil('\n');
    command.trim();
    
    if (command == "START") {
      potentiometerControlEnabled = true;
      Serial.println("Command: START - Servos ACTIVE");
    } 
    else if (command == "STOP") {
      potentiometerControlEnabled = false;
      resetServos();
      Serial.println("Command: STOP - Servos RESET");
    }
    else if (command == "GET_DISTANCE") { // פקודה מה-ESP32 לבקשת מרחק
      float distance = measureDistanceCm();
      if (distance > 0 && distance <= 5.0) {
        espSerial.println("DISTANCE_CLOSE"); // שלח ל-ESP32 שהעצם קרוב
        Serial.println("Sent: DISTANCE_CLOSE (Object <= 5cm)");
      } else {
        espSerial.println("DISTANCE_FAR"); // שלח ל-ESP32 שהעצם רחוק
        Serial.println("Sent: DISTANCE_FAR (Object > 5cm)");
      }
    }
    else {
      Serial.print("Unknown command: ");
      Serial.println(command);
    }
  }

  if (potentiometerControlEnabled) {
    controlServos();
    delay(50);
  }
}

void resetServos() {
  servo1.write(90);
  servo2.write(90);
  servo3.write(90);
  servo4.write(90);
  servo5.write(90);
}

void controlServos() {
  int val1 = map(analogRead(A0), 0, 1023, 0, 180);
  int val2 = map(analogRead(A1), 0, 1023, 0, 180);
  int val3 = map(analogRead(A2), 0, 1023, 0, 180);
  int val4 = map(analogRead(A3), 0, 1023, 0, 180);
  int val5 = map(analogRead(A6), 0, 1023, 35, 90);
  
  servo1.write(val1);
  servo2.write(val2);
  servo3.write(val3);
  servo4.write(val4);
  servo5.write(val5);
}

// פונקציה למדידת מרחק בס"מ
float measureDistanceCm() {
  long duration;
  float distance_cm;

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  // שלח פולס של 10 מיקרו-שניות
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // קלוט את משך הפולס החוזר
  // טיימאאוט של 30000 מיקרו-שניות (30 מילי-שניות) כדי למנוע תקיעה אם אין הד
  duration = pulseIn(ECHO_PIN, HIGH, 30000); 

  // אם לא התקבל הד (duration == 0), החזר 0
  if (duration == 0) {
    return 0;
  }
  distance_cm = duration * 0.034 / 2;
  return distance_cm;
}
