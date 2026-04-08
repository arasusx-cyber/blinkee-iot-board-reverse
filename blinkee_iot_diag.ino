/*
  blinkee_iot_diag.ino
  Diagnostic firmware for Blinkee IoT board (STM32F030CCT6)

  Service UART:
    PB11 = RX
    PB10 = TX

  Tested blocks:
    - backup battery ADC (PA0)
    - main battery ADC (PA1) [divider ratio configurable]
    - KX124 accel over I2C
    - MAX-M8W GPS over UART
    - SARA-G340 modem over UART
    - basic GPIO control

  Notes:
    - Adjust ADC divider ratios before trusting voltage numbers.
    - Modem PWR_ON polarity/pulse length may need tuning.
    - GPS baud may vary; sketch tries a few common values.
*/

#include <Arduino.h>
#include <Wire.h>

// =========================
// Pin map
// =========================
static const uint8_t PIN_IOT_BAT_ADC   = PA0;
static const uint8_t PIN_MAIN_BAT_ADC  = PA1;

static const uint8_t PIN_MODEM_CTS     = PA8;
static const uint8_t PIN_MODEM_RESET_N = PA11;
static const uint8_t PIN_MODEM_PWR_ON  = PA12;

static const uint8_t PIN_GPS_TIMEPULSE = PA15;
static const uint8_t PIN_GPS_EN        = PB5;

static const uint8_t PIN_ACCEL_INT1    = PB12;
static const uint8_t PIN_ACCEL_SCL     = PB13;
static const uint8_t PIN_ACCEL_SDA     = PB14;
static const uint8_t PIN_ACCEL_INT2    = PB15;

static const uint8_t PIN_SCOOTER_WAKE  = PC13;
static const uint8_t PIN_SIREN         = PC14;

// =========================
// UARTs
// =========================
// STM32 Arduino: HardwareSerial(RX, TX)
HardwareSerial ServiceSerial(PB11, PB10);
HardwareSerial GpsSerial(PB3, PB4);
HardwareSerial ModemSerial(PA10, PA9);

// =========================
// Config
// =========================
static const uint32_t SERVICE_BAUD = 115200;

// ADC assumptions
static const float ADC_VREF = 3.30f;
static const uint16_t ADC_MAX = 4095;

// Tune these after real divider measurement
static const float DIVIDER_IOT_BAT  = 3.20f;   // example only
static const float DIVIDER_MAIN_BAT = 15.00f;  // example only

// KX124
static const uint8_t KX124_ADDR      = 0x1E;
static const uint8_t KX124_WHO_AM_I  = 0x0F;
static const uint8_t KX124_CNTL1     = 0x1B;
static const uint8_t KX124_XOUT_L    = 0x06;

// Modem
static const bool MODEM_PWR_ON_ACTIVE_LOW = true;   // likely for SARA, adjust if needed
static const uint16_t MODEM_PWR_ON_PULSE_MS = 2500; // tune if needed
static const bool MODEM_RESET_ACTIVE_LOW = true;
static const uint16_t MODEM_RESET_PULSE_MS = 200;

// GPS
static const uint32_t gpsBauds[] = {9600, 38400, 115200};

// =========================
// Helpers
// =========================
float adcToVoltage(uint16_t raw, float divider) {
  float pinV = (raw * ADC_VREF) / ADC_MAX;
  return pinV * divider;
}

uint16_t readAdcAvg(uint8_t pin, uint8_t samples = 16) {
  uint32_t acc = 0;
  for (uint8_t i = 0; i < samples; i++) {
    acc += analogRead(pin);
    delay(2);
  }
  return acc / samples;
}

void printLine() {
  ServiceSerial.println(F("------------------------------------------------------------"));
}

void printHeader(const __FlashStringHelper* h) {
  printLine();
  ServiceSerial.println(h);
  printLine();
}

bool i2cReadReg8(uint8_t addr, uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((int)addr, 1) != 1) return false;
  val = Wire.read();
  return true;
}

bool i2cWriteReg8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return (Wire.endTransmission() == 0);
}

bool i2cReadBuf(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((int)addr, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

void scanI2C() {
  printHeader(F("I2C scan"));
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      ServiceSerial.print(F("I2C device at 0x"));
      if (addr < 16) ServiceSerial.print('0');
      ServiceSerial.println(addr, HEX);
    }
  }
}

void printVoltages() {
  printHeader(F("ADC voltages"));
  uint16_t rawIot = readAdcAvg(PIN_IOT_BAT_ADC);
  uint16_t rawMain = readAdcAvg(PIN_MAIN_BAT_ADC);

  float vIot = adcToVoltage(rawIot, DIVIDER_IOT_BAT);
  float vMain = adcToVoltage(rawMain, DIVIDER_MAIN_BAT);

  ServiceSerial.print(F("IOT_BAT raw="));
  ServiceSerial.print(rawIot);
  ServiceSerial.print(F("  approx_V="));
  ServiceSerial.println(vIot, 3);

  ServiceSerial.print(F("MAIN_BAT raw="));
  ServiceSerial.print(rawMain);
  ServiceSerial.print(F("  approx_V="));
  ServiceSerial.println(vMain, 3);
}

void printInputs() {
  printHeader(F("GPIO status"));
  ServiceSerial.print(F("MODEM_CTS="));
  ServiceSerial.println(digitalRead(PIN_MODEM_CTS));

  ServiceSerial.print(F("GPS_TIMEPULSE="));
  ServiceSerial.println(digitalRead(PIN_GPS_TIMEPULSE));

  ServiceSerial.print(F("ACCEL_INT1="));
  ServiceSerial.println(digitalRead(PIN_ACCEL_INT1));

  ServiceSerial.print(F("ACCEL_INT2="));
  ServiceSerial.println(digitalRead(PIN_ACCEL_INT2));
}

void accelTest() {
  printHeader(F("KX124 test"));

  uint8_t who = 0x00;
  if (!i2cReadReg8(KX124_ADDR, KX124_WHO_AM_I, who)) {
    ServiceSerial.println(F("KX124 not responding"));
    return;
  }

  ServiceSerial.print(F("WHO_AM_I = 0x"));
  ServiceSerial.println(who, HEX);

  // Try to put sensor into operating mode lightly
  // CNTL1 content depends on desired range/resolution; keep gentle/minimal here
  uint8_t cntl1 = 0;
  i2cReadReg8(KX124_ADDR, KX124_CNTL1, cntl1);
  ServiceSerial.print(F("CNTL1 before = 0x"));
  ServiceSerial.println(cntl1, HEX);

  // Read raw XYZ registers
  uint8_t buf[6];
  if (i2cReadBuf(KX124_ADDR, KX124_XOUT_L, buf, sizeof(buf))) {
    int16_t x = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t y = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t z = (int16_t)((buf[5] << 8) | buf[4]);

    ServiceSerial.print(F("RAW X=")); ServiceSerial.print(x);
    ServiceSerial.print(F("  Y="));   ServiceSerial.print(y);
    ServiceSerial.print(F("  Z="));   ServiceSerial.println(z);
  } else {
    ServiceSerial.println(F("Could not read XYZ registers"));
  }
}

void gpsPower(bool on) {
  digitalWrite(PIN_GPS_EN, on ? HIGH : LOW);
  ServiceSerial.print(F("GPS_EN -> "));
  ServiceSerial.println(on ? F("ON") : F("OFF"));
}

void gpsTestOnce(uint32_t baud, uint32_t listenMs = 2000) {
  GpsSerial.end();
  delay(50);
  GpsSerial.begin(baud);

  ServiceSerial.print(F("[GPS] Listening at "));
  ServiceSerial.print(baud);
  ServiceSerial.println(F(" baud"));

  uint32_t t0 = millis();
  bool got = false;
  while (millis() - t0 < listenMs) {
    while (GpsSerial.available()) {
      char c = GpsSerial.read();
      ServiceSerial.write(c);
      got = true;
    }
  }
  if (!got) {
    ServiceSerial.println(F("[GPS] no data"));
  }
}

void gpsScan() {
  printHeader(F("GPS scan"));
  gpsPower(true);
  delay(500);

  for (uint32_t i = 0; i < sizeof(gpsBauds)/sizeof(gpsBauds[0]); i++) {
    gpsTestOnce(gpsBauds[i], 2500);
    printLine();
  }
}

void modemPulsePwrOn() {
  ServiceSerial.println(F("[MODEM] PWR_ON pulse"));
  if (MODEM_PWR_ON_ACTIVE_LOW) {
    digitalWrite(PIN_MODEM_PWR_ON, LOW);
    delay(MODEM_PWR_ON_PULSE_MS);
    digitalWrite(PIN_MODEM_PWR_ON, HIGH);
  } else {
    digitalWrite(PIN_MODEM_PWR_ON, HIGH);
    delay(MODEM_PWR_ON_PULSE_MS);
    digitalWrite(PIN_MODEM_PWR_ON, LOW);
  }
}

void modemResetPulse() {
  ServiceSerial.println(F("[MODEM] RESET pulse"));
  if (MODEM_RESET_ACTIVE_LOW) {
    digitalWrite(PIN_MODEM_RESET_N, LOW);
    delay(MODEM_RESET_PULSE_MS);
    digitalWrite(PIN_MODEM_RESET_N, HIGH);
  } else {
    digitalWrite(PIN_MODEM_RESET_N, HIGH);
    delay(MODEM_RESET_PULSE_MS);
    digitalWrite(PIN_MODEM_RESET_N, LOW);
  }
}

void modemSendAT(const char *cmd, uint32_t waitMs = 1500) {
  ServiceSerial.print(F(">> "));
  ServiceSerial.println(cmd);

  ModemSerial.print(cmd);
  ModemSerial.print("\r");

  uint32_t t0 = millis();
  while (millis() - t0 < waitMs) {
    while (ModemSerial.available()) {
      ServiceSerial.write(ModemSerial.read());
    }
  }
  ServiceSerial.println();
}

void modemBasicTest() {
  printHeader(F("MODEM basic test"));

  ServiceSerial.print(F("CTS pin state = "));
  ServiceSerial.println(digitalRead(PIN_MODEM_CTS));

  // Common SARA baud candidates
  const uint32_t bauds[] = {115200, 9600, 38400};
  const char *cmds[] = {"AT", "ATE0", "ATI", "AT+CPIN?", "AT+CSQ", "AT+CREG?", "AT+CGATT?"};

  for (uint8_t b = 0; b < sizeof(bauds)/sizeof(bauds[0]); b++) {
    printLine();
    ServiceSerial.print(F("[MODEM] Trying baud "));
    ServiceSerial.println(bauds[b]);
    ModemSerial.end();
    delay(50);
    ModemSerial.begin(bauds[b]);

    for (uint8_t i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
      modemSendAT(cmds[i], 1200);
    }
  }
}

void modemTransparentBridge(uint32_t baud = 115200) {
  printHeader(F("MODEM bridge mode"));
  ServiceSerial.println(F("Bridge active. Service UART <-> Modem UART"));
  ServiceSerial.println(F("Send ~~~ on its own line to exit."));
  ModemSerial.end();
  delay(50);
  ModemSerial.begin(baud);

  String line;
  while (true) {
    while (ServiceSerial.available()) {
      char c = ServiceSerial.read();
      ModemSerial.write(c);
      line += c;
      if (line.endsWith("~~~\r") || line.endsWith("~~~\n")) {
        ServiceSerial.println(F("\n[bridge exit]"));
        return;
      }
      if (line.length() > 8) line.remove(0, line.length() - 8);
    }
    while (ModemSerial.available()) {
      ServiceSerial.write(ModemSerial.read());
    }
  }
}

void beep(uint16_t ms = 200) {
  digitalWrite(PIN_SIREN, HIGH);
  delay(ms);
  digitalWrite(PIN_SIREN, LOW);
}

void printHelp() {
  printHeader(F("Commands"));
  ServiceSerial.println(F("h  - help"));
  ServiceSerial.println(F("v  - read ADC voltages"));
  ServiceSerial.println(F("i  - read GPIO status"));
  ServiceSerial.println(F("s  - I2C scan"));
  ServiceSerial.println(F("a  - KX124 test"));
  ServiceSerial.println(F("g  - GPS baud scan"));
  ServiceSerial.println(F("1  - GPS ON"));
  ServiceSerial.println(F("0  - GPS OFF"));
  ServiceSerial.println(F("m  - modem basic AT test"));
  ServiceSerial.println(F("p  - modem PWR_ON pulse"));
  ServiceSerial.println(F("r  - modem RESET pulse"));
  ServiceSerial.println(F("b  - bridge service UART <-> modem"));
  ServiceSerial.println(F("w  - scooter WAKEUP pulse"));
  ServiceSerial.println(F("x  - siren beep"));
}

void scooterWakePulse(uint16_t ms = 500) {
  ServiceSerial.println(F("[SCOOTER] WAKEUP pulse"));
  digitalWrite(PIN_SCOOTER_WAKE, HIGH);
  delay(ms);
  digitalWrite(PIN_SCOOTER_WAKE, LOW);
}

void setupPins() {
  pinMode(PIN_GPS_EN, OUTPUT);
  pinMode(PIN_SCOOTER_WAKE, OUTPUT);
  pinMode(PIN_SIREN, OUTPUT);

  pinMode(PIN_MODEM_PWR_ON, OUTPUT);
  pinMode(PIN_MODEM_RESET_N, OUTPUT);

  pinMode(PIN_MODEM_CTS, INPUT_PULLUP);
  pinMode(PIN_GPS_TIMEPULSE, INPUT);
  pinMode(PIN_ACCEL_INT1, INPUT);
  pinMode(PIN_ACCEL_INT2, INPUT);

  digitalWrite(PIN_GPS_EN, LOW);
  digitalWrite(PIN_SCOOTER_WAKE, LOW);
  digitalWrite(PIN_SIREN, LOW);

  // Safe idle
  digitalWrite(PIN_MODEM_PWR_ON, MODEM_PWR_ON_ACTIVE_LOW ? HIGH : LOW);
  digitalWrite(PIN_MODEM_RESET_N, MODEM_RESET_ACTIVE_LOW ? HIGH : LOW);
}

void setup() {
  setupPins();

  analogReadResolution(12);

  Wire.setSDA(PIN_ACCEL_SDA);
  Wire.setSCL(PIN_ACCEL_SCL);
  Wire.begin();

  ServiceSerial.begin(SERVICE_BAUD);
  delay(300);

  printHeader(F("Blinkee IoT board diagnostic"));
  ServiceSerial.println(F("STM32F030CCT6 diag firmware"));
  printVoltages();
  printInputs();
  printHelp();
}

void loop() {
  if (ServiceSerial.available()) {
    char c = ServiceSerial.read();

    switch (c) {
      case 'h': printHelp(); break;
      case 'v': printVoltages(); break;
      case 'i': printInputs(); break;
      case 's': scanI2C(); break;
      case 'a': accelTest(); break;
      case 'g': gpsScan(); break;
      case '1': gpsPower(true); break;
      case '0': gpsPower(false); break;
      case 'm': modemBasicTest(); break;
      case 'p': modemPulsePwrOn(); break;
      case 'r': modemResetPulse(); break;
      case 'b': modemTransparentBridge(115200); break;
      case 'w': scooterWakePulse(); break;
      case 'x': beep(); break;
      case '\r':
      case '\n':
        break;
      default:
        ServiceSerial.print(F("Unknown command: "));
        ServiceSerial.println(c);
        break;
    }
  }
}
