# Wristband wiring (ESP32 + MAX9814 + 2 vibration motors)

## Parts
- ESP32 dev board (ESP-WROOM-32)
- MAX9814 microphone module (solder the header pins on first)
- 2 coin vibration motors (10 mm)
- 2 NPN transistors (2N5551, 9013, S8050 or 2N2222 — **not** 9018)
- 2 diodes (1N4148, 1N4007, 1N5399… any of them)
- 2 resistors, 470 Ω (or 1 kΩ if that's what you have)
- 1 electrolytic capacitor, 100 µF (optional, but it keeps the mic quiet when the motors start)
- Breadboard + jumper wires

## 1. Microphone (3 wires)

| MAX9814 pin | goes to ESP32 |
|---|---|
| VDD | **3V3** (not 5V — the ESP32 can't read a 5 V signal) |
| GND | GND |
| OUT | **GPIO34** |
| GAIN | leave unconnected (60 dB). If sounds clip, connect to GND (50 dB) or VDD (40 dB) |
| AR | leave unconnected |

Pin names are printed on the board. Put the 100 µF capacitor between 3V3 and GND on the breadboard (the stripe on the capacitor marks the **GND** leg).

## 2. Motors (build this twice: motor A on GPIO25, motor B on GPIO26)

The ESP32 pin can't power a motor directly, so each motor is switched by a transistor:

```
                3V3 ───────────┬───────────┐
                               │           │
                            [motor]     [diode]   stripe (cathode) end toward 3V3
                               │           │
                               ├───────────┘
                               │
                         collector (C)
GPIO25/26 ──[470Ω]── base (B)    transistor
                         emitter (E)
                               │
                              GND
```

Step by step:
1. Put the transistor in the breadboard. **Check its pinout from the marking printed on it** (search e.g. "2N5551 pinout"). For most small TO-92 transistors, with the flat face toward you and the legs pointing down, the order is **E – B – C** left to right, but brands differ.
2. GPIO pin → resistor → **base**.
3. **Emitter** → GND.
4. **Collector** → one motor wire.
5. Other motor wire → **3V3**.
6. Diode across the motor: the end **with the stripe goes to 3V3**, the other end to the collector. (It protects the transistor from the motor's voltage spike.)

If a motor never buzzes, the transistor is probably the wrong way round — swap the emitter and collector legs.

Use the **3V3** pin for the motors. Don't use 5 V; these motors are made for about 3 V.

Both GNDs (mic, motors, ESP32) must be connected together.

## 3. Upload and test
1. Open `wristband.ino` in the Arduino IDE (set up the ESP32 board as described at the top of the file).
2. Upload, then open **Tools → Serial Monitor** at **115200 baud**.
3. On boot you should feel motor A, then motor B (that's the wiring test). Then stay quiet for 2 seconds while it learns the room.
4. Try each sound:
   - **Clap hard / knock on the desk** → `EVENT: BANG`, both motors, one long buzz
   - **Play a beep or alarm tone from your phone** → `EVENT: BEEP`, motor A, three quick buzzes
   - **Hum low, or play a low rumble** → `EVENT: RUMBLE`, motor B
   - **Talk near the mic** → `EVENT: VOICE`, one tiny tap
5. Watch the `rms / floor / level / tone / peakHz` numbers. If it triggers too much, lower `SENSITIVITY` at the top of the sketch. If it misses sounds, raise it.

## Troubleshooting
- **Upload stuck on "Connecting…"**: hold the BOOT button on the ESP32 until it starts. If the board never shows up as a port, try the other USB cable (some only charge).
- **It buzzes by itself**: lower `SENSITIVITY`, and check the mic's GND and the capacitor.
- **Never reacts**: check OUT → GPIO34, and that VDD is on 3V3.
- **Motors weak**: make sure the ESP32 is powered from a decent USB port or a power bank.

## Honest limits
This is a prototype. Its sound detection uses simple rules, so it will miss some sounds and mistake others, and the MAX9814's automatic gain keeps evening out loudness. Don't rely on it as a safety device (for example, instead of a real smoke or fire alarm).
