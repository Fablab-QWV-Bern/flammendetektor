/*
  Flammendetektor für Lasercutter
  v6 2023-08-13

  Peter Schurter
  Matthias Roggo 

  Quartierwerkstatt Vikoria Bern
*/

#include <Wire.h>
#include <Arduino.h>
#include <U8x8lib.h>
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif

// Config
#define PULSE_LIMIT_HZ 1.0  // Pulsrate, über welcher eine Warnung ausgelöst wird

// Pins
#define LED_R 4
#define LED_G 5
#define LED_B 1
#define BUZZER 6  // Piezo-Lautsprecher
#define BUTTON 2
#define PAUSE_LASER 12  // Relais zur Pausierung des Lasers
#define AIR_EXTINGUISH 9  // Relais zur Druckluftsteuerung
#define SENSOR 3
#define POT_INPUT PIN_A0

enum LedColor {
  OFF = 0B000,
  GREEN = 0B010,
  RED = 0B100,
  BLUE = 0B001,
  PINK = 0B101
};

volatile boolean display_needs_full_redraw = true;

// Any timestamps will overflow after 49 days. This is much longer than the
// expected operation time of the laser and can be ignored.
volatile unsigned long pulse_dt_ms = ULONG_MAX;
volatile bool pulse_update = false; // flag to notifiy about new pulses (and trigger frequency estimation)

// Display driver parameters:          CLK data CS  DC RST
U8X8_SSD1327_WS_128X128_4W_SW_SPI u8x8(13, 11,  10, 7, 8  );

void setup(void) {
  analogReference(EXTERNAL); // AREF connected to Nanos 3.3V

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(SENSOR, INPUT);
  pinMode (PAUSE_LASER, OUTPUT);
  pinMode (AIR_EXTINGUISH, OUTPUT);

  digitalWrite(BUZZER, LOW);
  set_led(BLUE);

  Serial.begin(9600);
  u8x8.begin();

  u8x8.setFont(u8x8_font_px437wyse700b_2x2_f);
  u8x8.drawString(0, 1, "FLAMMEN");
  u8x8.drawString(0, 5, "Detektor");

  u8x8.setFont(u8x8_font_8x13_1x2_r);
  u8x8.drawString(3, 8, "Info im");
  u8x8.drawString(3, 10, "Handbuch");
  u8x8.drawString(3, 12, "beachten");

  delay(3000);

  attachInterrupt(digitalPinToInterrupt(BUTTON), handle_button, RISING);
  attachInterrupt(digitalPinToInterrupt(SENSOR), handle_pulse_flank, CHANGE);

  u8x8.clearDisplay() ;
  set_led(GREEN);
}

LedColor set_led(LedColor color) {
    static LedColor current = OFF;
    digitalWrite(LED_R, color & 0b100);
    digitalWrite(LED_G, color & 0b010);
    digitalWrite(LED_B, color & 0b001);
    LedColor previous = current;
    current = color;
    return previous;
};

void handle_pulse_flank() {
  if (digitalRead(SENSOR)) {
    handle_pulse_end();
  } else {
    handle_pulse_start();
  }
}

volatile unsigned long pulse_start_ms = 0;
void handle_pulse_start() {
  pulse_start_ms = millis();
}

volatile unsigned long pulse_previous_ms = 0;
void handle_pulse_end() {
  unsigned long now = millis();

  unsigned long pulse_width_ms = now - pulse_start_ms;
  if (pulse_width_ms < 13 || pulse_width_ms > 17) {
    // This was a glitch, not a proper pulse
    Serial.print("Glitch detected (");
    Serial.print(pulse_width_ms);
    Serial.println(" ms)");
    return;
  }
  Serial.print("Pulse detected with ");
  Serial.print(pulse_width_ms);
  Serial.println(" ms");

  pulse_dt_ms = now - pulse_previous_ms;
  pulse_previous_ms = now;

  if (pulse_previous_ms != 0) {
    pulse_update = true;
  }
}

volatile uint8_t warning_state = 0; // the state machines "state"
volatile boolean button_pressed = false; // flag to check for button presses
void loop(void) {
  static uint8_t sensitivity_percent_previous = -1; // to check if the display needs an update
  static double pulse_hz = 0;
  static unsigned long pause_state_transition = 0; // allows to pause within a state after entering it

  boolean display_pulse_hz_update = false;

  // Handle detected pulse
  if (pulse_update) {
    // Update pulse rate estimation
    LedColor previous = set_led(PINK);
    digitalWrite(BUZZER, HIGH);
    delay(50);
    if (warning_state != 4 || button_pressed) {
      // In state 4 (emergency stop), the buzzer runs continuously, unless the button was pressed. 
      // We don't want to interfere with that!
      digitalWrite(BUZZER, LOW);
    }
    set_led(previous);
    pulse_update = false;
    pulse_hz = 1000.0/pulse_dt_ms;
    display_pulse_hz_update = true;

  } else if (pulse_hz != 0 && millis() - pulse_previous_ms > 10*1000) {
    // Reset pulse rate estimation to 0 if nothing happened for 10 seconds
    pulse_hz = 0;
    display_pulse_hz_update = true;
    // TODO: As soon as the pulse period exceeds the previous one, do the update.
  }

  uint32_t potVal = analogRead(POT_INPUT);

  uint8_t sensitivity_percent = map(potVal, 0, 1020, 50, 100);
  double effective_pulse_limit_hz = ((double) PULSE_LIMIT_HZ)/(sensitivity_percent)*100;

  if (warning_state == 0) {
    //--- Update within state: No warnings ---//

    if (display_needs_full_redraw) {
      u8x8.clearDisplay();
      u8x8.setFont(u8x8_font_px437wyse700b_2x2_f);
      u8x8.drawString(1, 2, " ");
      u8x8.drawString(1, 2, "Sensor-");
      u8x8.setFont(u8x8_font_8x13_1x2_r);
      u8x8.drawString(0, 5, " ");
      u8x8.drawString(0, 5, "EMPFINDLICHKEIT");
      u8x8.drawString(1, 10, "         ");
      u8x8.drawString(1, 8, "          ");
    }

    if (display_pulse_hz_update || display_needs_full_redraw) {
      u8x8.setFont(u8x8_font_8x13_1x2_r);
      u8x8.drawString(0, 12, "Pulse:       ");
      u8x8.setCursor(7, 12);
      u8x8.print(pulse_hz);
      u8x8.print(" Hz  ");

      Serial.print("Intervall: ");
      Serial.println(pulse_dt_ms);
    }

    if (sensitivity_percent != sensitivity_percent_previous || display_needs_full_redraw) {
      u8x8.setFont(u8x8_font_8x13_1x2_r);
      u8x8.setCursor(6, 8);
      u8x8.print(sensitivity_percent);
      u8x8.print(" % ");

      u8x8.drawString(0, 14, "Limit:       ");
      u8x8.setCursor(7, 14);
      u8x8.print(effective_pulse_limit_hz);
      u8x8.print(" Hz  ");
      sensitivity_percent_previous = sensitivity_percent;
    }
    display_needs_full_redraw = false;
  }

  if (pause_state_transition > millis()) return;

  // Check for state transitions
  if (pulse_hz < effective_pulse_limit_hz){
    // Pulse frequency is below limit.
    if (warning_state != 0) {
      warning_state = 0;
      display_needs_full_redraw = true;
      pause_state_transition = 0;
    }
    return;
  } 

  // Pulse frequency is above limit.
  // Move to next warning state (if not at highest)
  if (warning_state < 4) {
    warning_state++;
    display_needs_full_redraw = true;
  } else {
    // No state change, since we're already at the highest
    return;
  }

  if (warning_state == 0) {
    //--- Entering state: No warnings ---//
    set_led(GREEN);
    digitalWrite(BUZZER, LOW);
    digitalWrite(PAUSE_LASER, LOW);
  }

  if (warning_state == 1 || warning_state == 2) {
    //--- Entering state: Limit exceeded once or twice ---//
    u8x8.clearDisplay();
    set_led(RED);

    u8x8.setFont(u8x8_font_px437wyse700b_2x2_f);
    u8x8.drawString(1, 5, "GEFAHR!");
    u8x8.setCursor(6, 9);
    u8x8.print(warning_state);

    pause_state_transition = millis() + 2*1000;
  }

  if (warning_state == 3) {
    //--- Entering state: Announce emergency pause ---//
    digitalWrite(BUZZER, HIGH);
    set_led(RED);
    
    u8x8.clearDisplay();
    u8x8.setFont(u8x8_font_px437wyse700b_2x2_f);
    u8x8.drawString(2, 5, "START");
    u8x8.drawString(1, 8, "NOT-AUS");

    pause_state_transition = millis() + 2*1000;
  }

  if (warning_state == 4) {
    //--- Entering state: Emergency pause ---//
    button_pressed = false;
    digitalWrite(BUZZER, HIGH);
    set_led(RED);

    u8x8.clearDisplay();
    u8x8.setFont(u8x8_font_8x13_1x2_r);
    u8x8.drawString(0, 1, "Taster druecken!");
    u8x8.drawString(0, 4, "Wenn Feuer");
    u8x8.drawString(2, 6, "GELOESCHT!");
    u8x8.drawString(0, 9, "Fortfahren  mit");
    u8x8.drawString(3, 11, "* ENTER * ");
    u8x8.drawString(0, 13, "auf LASER-PANEL");

    digitalWrite(PAUSE_LASER, HIGH);
  }
}

void handle_button() {
  digitalWrite(BUZZER, LOW);
  button_pressed = true;
}