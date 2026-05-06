/*
  Arduino MEGA - Control IBT-2 + DRV8825
  Basado en el código simple que SÍ funciona del ESP32
  
  Conexiones DRV8825:
  - STEP: pin que genera los pulsos
  - DIR: dirección (HIGH=adelante, LOW=atrás)
  - EN: enable activo LOW (LOW=habilitado, HIGH=deshabilitado)
*/

#include <Arduino.h>

// ---- IBT-2 #1 (Tracción) ----
const uint8_t RPWM1 = 5;
const uint8_t LPWM1 = 6;
const uint8_t REN1  = 22;
const uint8_t LEN1  = 23;

// ---- IBT-2 #2 (Dirección) ----
const uint8_t RPWM2 = 7;
const uint8_t LPWM2 = 8;
const uint8_t REN2  = 24;
const uint8_t LEN2  = 25;

// ---- DRV8825 (NEMA) - IGUAL QUE EL CÓDIGO QUE FUNCIONA ----
const uint8_t DIR1  = 28;
const uint8_t STEP1 = 26;
const uint8_t EN1   = 27;

const uint32_t BAUD = 115200;
const uint8_t  NEMA_SPD_MIN = 60;
const uint8_t  NEMA_SPD_MAX = 255;
uint8_t DEFAULT_NEMA_SPD = 200;

// Timeout de seguridad
unsigned long lastCommandTime = 0;
const unsigned long TIMEOUT_MS = 2000;

// ===================== NUEVO: Control NEMA estilo ESP32 =====================
struct NemaControl {
  bool running = false;        // ¿Está girando?
  bool direction = true;       // true=FWD(HIGH), false=BACK(LOW)
  uint16_t delayMicros = 750;  // Delay entre pulsos (igual que tu código ESP32)
  unsigned long lastStepTime = 0;
  bool stepState = false;      // Estado actual del pin STEP
};

NemaControl nema1;

// Convertir velocidad (60-255) a delayMicroseconds
uint16_t speedToDelay(uint8_t spd) {
  if (spd < NEMA_SPD_MIN) spd = NEMA_SPD_MIN;
  if (spd > NEMA_SPD_MAX) spd = NEMA_SPD_MAX;
  
  // Mapeo: velocidad baja (60) = delay alto (1500us = lento)
  //        velocidad alta (255) = delay bajo (400us = rápido)
  const uint16_t DELAY_SLOW = 1500;  // Velocidad mínima
  const uint16_t DELAY_FAST = 400;   // Velocidad máxima
  
  uint16_t mapped = map(spd, NEMA_SPD_MIN, NEMA_SPD_MAX, DELAY_SLOW, DELAY_FAST);
  return mapped;
}

void nemaStart(bool forward, uint8_t speed) {
  nema1.running = true;
  nema1.direction = forward;
  nema1.delayMicros = speedToDelay(speed);
  
  // Configurar dirección (igual que tu código ESP32)
  digitalWrite(DIR1, forward ? HIGH : LOW);
  
  // Habilitar driver (EN activo LOW)
  digitalWrite(EN1, LOW);
  
  // Resetear estado
  nema1.stepState = false;
  digitalWrite(STEP1, LOW);
  nema1.lastStepTime = micros();
}

void nemaStop() {
  nema1.running = false;
  digitalWrite(EN1, HIGH);  // Deshabilitar driver
  digitalWrite(STEP1, LOW);
}

// Esta función debe llamarse continuamente en loop() - CLAVE
void nemaService() {
  if (!nema1.running) return;
  
  unsigned long now = micros();
  
  // Generar pulsos EXACTAMENTE como tu código ESP32
  if (now - nema1.lastStepTime >= nema1.delayMicros) {
    nema1.lastStepTime = now;
    
    // Toggle del pin STEP (HIGH → LOW → HIGH → LOW...)
    nema1.stepState = !nema1.stepState;
    digitalWrite(STEP1, nema1.stepState ? HIGH : LOW);
  }
}

// ===================== IBT-2 (sin cambios) =====================
void driveStop()  { analogWrite(RPWM1,0); analogWrite(LPWM1,0); }
void driveFwd(uint8_t spd)  { analogWrite(LPWM1,0); analogWrite(RPWM1,spd); }
void driveBack(uint8_t spd) { analogWrite(RPWM1,0); analogWrite(LPWM1,spd); }

void steerStop() { analogWrite(RPWM2,0); analogWrite(LPWM2,0); }
void steerLeft(uint8_t spd)  { analogWrite(RPWM2,0); analogWrite(LPWM2,spd); }
void steerRight(uint8_t spd) { analogWrite(LPWM2,0); analogWrite(RPWM2,spd); }

// ===================== Parser =====================
String line;
static uint8_t clamp255(int v) { 
  if(v<0) v=0; 
  if(v>255) v=255; 
  return (uint8_t)v; 
}

void execCommand(const String& cmd) {
  lastCommandTime = millis();
  
  // ===== TRACCIÓN =====
  if (cmd.startsWith("DRV:FWD:")) {
    uint8_t s = clamp255(cmd.substring(8).toInt());
    driveFwd(s);
    Serial1.println(F("OK:DRV:FWD"));
    
  } else if (cmd.startsWith("DRV:BACK:")) {
    uint8_t s = clamp255(cmd.substring(9).toInt());
    driveBack(s);
    Serial1.println(F("OK:DRV:BACK"));
    
  } else if (cmd == "DRV:STOP") {
    driveStop();
    Serial1.println(F("OK:DRV:STOP"));

  // ===== DIRECCIÓN =====
  } else if (cmd.startsWith("STR:LEFT:")) {
    uint8_t s = clamp255(cmd.substring(9).toInt());
    steerLeft(s);
    Serial1.println(F("OK:STR:LEFT"));
    
  } else if (cmd.startsWith("STR:RIGHT:")) {
    uint8_t s = clamp255(cmd.substring(10).toInt());
    steerRight(s);
    Serial1.println(F("OK:STR:RIGHT"));
    
  } else if (cmd == "STR:STOP") {
    steerStop();
    Serial1.println(F("OK:STR:STOP"));

  // ===== NEMA (método N1) =====
  } else if (cmd.startsWith("N1:FWD:")) {
    uint8_t s = clamp255(cmd.substring(7).toInt());
    nemaStart(true, s);  // forward = HIGH en DIR
    Serial1.println(F("OK:N1:FWD"));
    
  } else if (cmd.startsWith("N1:BACK:")) {
    uint8_t s = clamp255(cmd.substring(8).toInt());
    nemaStart(false, s);  // backward = LOW en DIR
    Serial1.println(F("OK:N1:BACK"));
    
  } else if (cmd == "N1:STOP") {
    nemaStop();
    Serial1.println(F("OK:N1:STOP"));

  // ===== NEMA (atajos NEMA:UP/DOWN/STOP) =====
  } else if (cmd.startsWith("NEMA:UP")) {
    uint8_t s = DEFAULT_NEMA_SPD;
    if (cmd.length() > 8 && cmd.charAt(8) == ':') {
      s = clamp255(cmd.substring(9).toInt());
    }
    nemaStart(true, s);
    Serial1.println(F("OK:NEMA:UP"));

  } else if (cmd.startsWith("NEMA:DOWN")) {
    uint8_t s = DEFAULT_NEMA_SPD;
    if (cmd.length() > 10 && cmd.charAt(10) == ':') {
      s = clamp255(cmd.substring(11).toInt());
    }
    nemaStart(false, s);
    Serial1.println(F("OK:NEMA:DOWN"));

  } else if (cmd == "NEMA:STOP") {
    nemaStop();
    Serial1.println(F("OK:NEMA:STOP"));

  } else {
    Serial1.println(F("ERR:CMD"));
  }
}

// ===== Timeout de seguridad =====
void checkSafetyTimeout() {
  static bool alreadyStopped = false;
  
  if (millis() - lastCommandTime > TIMEOUT_MS) {
    if (!alreadyStopped) {
      driveStop();
      steerStop();
      nemaStop();
      alreadyStopped = true;
      Serial.println(F("⚠️ TIMEOUT: Sistema detenido"));
      Serial1.println(F("TIMEOUT:SAFETY_STOP"));
    }
  } else {
    alreadyStopped = false;
  }
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial1.begin(BAUD);

  // ===== IBT-2 Setup =====
  pinMode(RPWM1, OUTPUT); pinMode(LPWM1, OUTPUT);
  pinMode(REN1,  OUTPUT); pinMode(LEN1,  OUTPUT);
  pinMode(RPWM2, OUTPUT); pinMode(LPWM2, OUTPUT);
  pinMode(REN2,  OUTPUT); pinMode(LEN2,  OUTPUT);
  
  digitalWrite(REN1, HIGH); digitalWrite(LEN1, HIGH);
  digitalWrite(REN2, HIGH); digitalWrite(LEN2, HIGH);
  
  driveStop();
  steerStop();

  // ===== DRV8825 Setup (IGUAL QUE TU CÓDIGO ESP32) =====
  pinMode(STEP1, OUTPUT);
  pinMode(DIR1,  OUTPUT);
  pinMode(EN1,   OUTPUT);
  
  digitalWrite(EN1, HIGH);   // Deshabilitado al inicio (activo LOW)
  digitalWrite(STEP1, LOW);
  digitalWrite(DIR1, LOW);

  lastCommandTime = millis();
  
  Serial.println(F("========================================"));
  Serial.println(F("✓ MEGA Listo - IBT-2 + DRV8825"));
  Serial.println(F("✓ Control NEMA estilo ESP32"));
  Serial.println(F("✓ Timeout de seguridad: 2s"));
  Serial.println(F("========================================"));
  Serial.println();
  Serial.println(F("Comandos disponibles:"));
  Serial.println(F("  DRV:FWD:<spd> | DRV:BACK:<spd> | DRV:STOP"));
  Serial.println(F("  STR:LEFT:<spd> | STR:RIGHT:<spd> | STR:STOP"));
  Serial.println(F("  N1:FWD:<spd> | N1:BACK:<spd> | N1:STOP"));
  Serial.println(F("  NEMA:UP[:<spd>] | NEMA:DOWN[:<spd>] | NEMA:STOP"));
  Serial.println(F("========================================"));
}

// ===================== LOOP =====================
void loop() {
  // Parser de comandos Serial1
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (line.length()) {
        execCommand(line);
        line = "";
      }
    } else if (line.length() < 64) {
      line += c;
    }
  }
  
  // ¡CRÍTICO! Llamar a nemaService() continuamente
  // Esto genera los pulsos STEP de forma no bloqueante
  nemaService();
  
  // Verificar timeout de seguridad
  checkSafetyTimeout();
}