/*
  Bridge Wristband  -  "Feel the Sound"
  ESP32 + MAX9814 microphone + 2 coin vibration motors

  It listens to the room and turns sounds into different vibration patterns:
    BANG    loud sudden sound (door slam, knock, clap)  -> both motors, one long buzz
    BEEP    alarm / doorbell / phone ring (steady high tone) -> motor A, three quick buzzes
    RUMBLE  low rumble (engine, car horn, thunder)       -> motor B, two medium buzzes
    VOICE   someone talking nearby                       -> motor A, one tiny tap

  Arduino IDE setup (once):
    1. File > Preferences > "Additional boards manager URLs", add:
       https://espressif.github.io/arduino-esp32/package_esp32_index.json
    2. Tools > Board > Boards Manager > search "esp32" > install "esp32 by Espressif Systems"
    3. Tools > Board > ESP32 Arduino > "ESP32 Dev Module"
    4. Plug in the ESP32 with a micro-USB DATA cable, pick its port under Tools > Port
    5. Upload. Then open Tools > Serial Monitor at 115200 baud to watch what it hears.
       (If upload hangs on "Connecting....", hold the BOOT button on the board until it starts.)

  No extra libraries needed. Wiring is in WIRING.md.
*/

#include <Arduino.h>
#include <math.h>

// ======================= PINS =======================
const int MIC_PIN = 34;   // MAX9814 OUT -> GPIO34 (an ADC1 pin, input only)
const int MOTOR_A = 25;   // motor A transistor base (through a resistor)
const int MOTOR_B = 26;   // motor B transistor base (through a resistor)
const int LED_PIN = 2;    // the little blue LED on most ESP32 boards

// ======================= TUNING =======================
// 1 = calm (needs clear, strong sounds)  ...  10 = jumpy (reacts to small sounds)
const int  SENSITIVITY  = 5;
// true = print numbers for the Serial Plotter / Monitor so you can tune
const bool DEBUG_LEVELS = true;

// ======================= AUDIO SETTINGS =======================
const int   FS = 10000;                   // samples per second
const int   N  = 256;                     // samples per analysis window (~26 ms)
const uint32_t SAMPLE_US = 1000000UL / FS;
const float BIN_HZ = (float)FS / N;       // frequency width of one FFT bin (~39 Hz)

float x[N];       // audio samples
float im[N];      // imaginary part for the FFT
float hann[N];    // window shape, filled once in setup()

float floorRms   = 20.0f;    // learned background loudness
float prevRms    = 0.0f;
uint32_t holdoffUntil = 0;   // ignore the mic until this time (motor noise)

// ======================= EVENTS & PATTERNS =======================
enum Event { NONE = 0, BANG = 1, BEEP = 2, RUMBLE = 3, VOICE = 4 };
const char* EVENT_NAME[] = { "-", "BANG", "BEEP", "RUMBLE", "VOICE" };
const uint32_t COOLDOWN_MS[] = { 0, 800, 1500, 2000, 2500 };
uint32_t lastFired[5] = { 0, 0, 0, 0, 0 };

// A pattern is a list of durations in ms: ON, OFF, ON, OFF, ...
struct Pattern { const uint16_t* steps; uint8_t len; uint8_t motors; };  // motors: bit0 = A, bit1 = B
const uint16_t P_BANG[]   = { 500 };
const uint16_t P_BEEP[]   = { 90, 70, 90, 70, 90 };
const uint16_t P_RUMBLE[] = { 300, 100, 300 };
const uint16_t P_VOICE[]  = { 40 };
const Pattern PATTERNS[] = {
  { P_BANG,   1, 3 },
  { P_BEEP,   5, 1 },
  { P_RUMBLE, 3, 2 },
  { P_VOICE,  1, 1 },
};

const Pattern* cur = nullptr;
uint8_t  stepIdx = 0;
uint32_t stepStart = 0;

void setMotors(uint8_t mask, bool on) {
  digitalWrite(MOTOR_A, (on && (mask & 1)) ? HIGH : LOW);
  digitalWrite(MOTOR_B, (on && (mask & 2)) ? HIGH : LOW);
  digitalWrite(LED_PIN, on ? HIGH : LOW);
}

void startPattern(Event e) {
  cur = &PATTERNS[e - 1];
  stepIdx = 0;
  stepStart = millis();
  setMotors(cur->motors, true);
}

// returns true while a pattern is still playing
bool updateVibration() {
  if (!cur) return false;
  uint32_t now = millis();
  if (now - stepStart >= cur->steps[stepIdx]) {
    stepIdx++;
    stepStart = now;
    if (stepIdx >= cur->len) {
      setMotors(0, false);
      cur = nullptr;
      holdoffUntil = now + 300;     // motor noise fades before we listen again
      return false;
    }
    setMotors(cur->motors, (stepIdx % 2) == 0);
  }
  return true;
}

// ======================= FFT (no library needed) =======================
void fft(float* re, float* imag) {
  for (int i = 1, j = 0; i < N; i++) {              // reorder samples
    int bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = imag[i]; imag[i] = imag[j]; imag[j] = t; }
  }
  for (int len = 2; len <= N; len <<= 1) {          // combine
    float ang = -2.0f * PI / len, wr = cosf(ang), wi = sinf(ang);
    for (int i = 0; i < N; i += len) {
      float cr = 1.0f, ci = 0.0f;
      for (int k = 0; k < len / 2; k++) {
        int a = i + k, b = i + k + len / 2;
        float tr = re[b] * cr - imag[b] * ci;
        float ti = re[b] * ci + imag[b] * cr;
        re[b] = re[a] - tr;  imag[b] = imag[a] - ti;
        re[a] += tr;         imag[a] += ti;
        float ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = ncr;
      }
    }
  }
}

// ======================= LISTENING =======================
// Grabs N samples at a steady rate, removes the DC offset, returns loudness (RMS).
float sampleWindow() {
  uint32_t next = micros();
  float sum = 0;
  for (int i = 0; i < N; i++) {
    while ((int32_t)(micros() - next) < 0) { }
    next += SAMPLE_US;
    x[i] = (float)analogRead(MIC_PIN);
    sum += x[i];
  }
  float mean = sum / N, sq = 0;
  for (int i = 0; i < N; i++) {
    x[i] -= mean;
    sq += x[i] * x[i];
  }
  return sqrtf(sq / N);
}

Event classify(float rms) {
  // adapt the background level: follow quiet moments, creep up slowly otherwise
  if (rms < floorRms * 1.5f) floorRms = floorRms * 0.98f + rms * 0.02f;
  else                       floorRms *= 1.0004f;
  if (floorRms < 4.0f)   floorRms = 4.0f;
  if (floorRms > 600.0f) floorRms = 600.0f;

  float R     = 12.0f - SENSITIVITY;        // lower R = more sensitive
  float level = rms / floorRms;             // how many times louder than "normal"
  float rise  = rms - prevRms;              // sudden jump?
  prevRms = rms;

  // spectrum
  for (int i = 0; i < N; i++) { x[i] *= hann[i]; im[i] = 0; }
  fft(x, im);
  float low = 0, mid = 0, high = 0, peak = 0, sumP = 0, peakHz = 0;
  int cnt = 0;
  for (int k = 1; k < N / 2; k++) {
    float f = k * BIN_HZ;
    float p = x[k] * x[k] + im[k] * im[k];
    if (f >= 50 && f < 250)       low  += p;
    else if (f >= 250 && f < 2000) mid += p;
    else if (f >= 2000)           high += p;
    if (f >= 300) {
      sumP += p; cnt++;
      if (p > peak) { peak = p; peakHz = f; }
    }
  }
  float total    = low + mid + high + 1e-9f;
  float tonality = peak / ((sumP - peak) / (cnt - 1) + 1e-9f);   // ~1-5 for noise, huge for a pure tone

  if (DEBUG_LEVELS) {
    static uint8_t n = 0;
    if (++n >= 8) {
      n = 0;
      Serial.print("rms:"); Serial.print(rms);
      Serial.print(",floor:"); Serial.print(floorRms);
      Serial.print(",level:"); Serial.print(level);
      Serial.print(",tone:"); Serial.print(tonality);
      Serial.print(",peakHz:"); Serial.println(peakHz);
    }
  }

  // A sudden jump could be a bang OR just the start of a voice/alarm. Wait one window:
  // if the sound has already died away it was an impulse (BANG), otherwise it is a
  // sustained sound and we classify it by its spectrum below.
  static float pendingSpike = 0;
  bool afterSpike = false;
  if (pendingSpike > 0) {
    float sp = pendingSpike;
    pendingSpike = 0;
    if (rms < 0.5f * sp) return BANG;
    afterSpike = true;
  } else if (level > R * 1.2f && rise > floorRms * R * 0.8f) {
    pendingSpike = rms;
    return NONE;
  }

  if (level > R * 0.6f && tonality > 30 && peakHz > 1500 && peakHz < 4800)     return BEEP;
  if (level > R * 0.8f && low / total > 0.6f)                                  return RUMBLE;
  if (level > R * 0.5f && mid / total > 0.55f && tonality < 30)                return VOICE;
  if (afterSpike) return BANG;     // loud jump that is not a tone, rumble or voice = bang
  return NONE;
}

// ======================= SETUP / LOOP =======================
void setup() {
  Serial.begin(115200);
  pinMode(MOTOR_A, OUTPUT);
  pinMode(MOTOR_B, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  setMotors(0, false);

  analogReadResolution(12);                      // 0..4095
  analogSetPinAttenuation(MIC_PIN, ADC_11db);    // read up to ~3.3 V
  for (int i = 0; i < N; i++) hann[i] = 0.5f - 0.5f * cosf(2.0f * PI * i / (N - 1));

  Serial.println();
  Serial.println("Bridge Wristband starting...");

  // wiring self-test: you should feel motor A, then motor B
  Serial.println("Self-test: motor A");
  setMotors(1, true);  delay(250); setMotors(0, false); delay(250);
  Serial.println("Self-test: motor B");
  setMotors(2, true);  delay(250); setMotors(0, false); delay(400);

  // learn how loud the room normally is (stay quiet for ~2 seconds)
  Serial.println("Learning background noise - keep quiet for 2 seconds...");
  float acc = 0;
  const int W = 70;
  for (int i = 0; i < W; i++) acc += sampleWindow();
  floorRms = acc / W;
  if (floorRms < 4.0f) floorRms = 4.0f;
  prevRms = floorRms;
  Serial.print("Background level: "); Serial.println(floorRms);
  Serial.println("Listening!");
}

void loop() {
  if (updateVibration()) { delay(1); return; }             // busy buzzing: don't listen

  if (millis() < holdoffUntil) { sampleWindow(); return; } // let motor noise die down

  float rms = sampleWindow();
  Event e = classify(rms);
  if (e != NONE) {
    uint32_t now = millis();
    if (now - lastFired[e] >= COOLDOWN_MS[e]) {
      lastFired[e] = now;
      Serial.print("EVENT: "); Serial.println(EVENT_NAME[e]);
      startPattern(e);
    }
  }
}
