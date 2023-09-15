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
#define PAUSE_LASER 9  // Relais zur Pausierung des Lasers
#define AIR_EXTINGUISH 12  // Relais zur Druckluftsteuerung
#define SENSOR 3
#define POT_INPUT PIN_A0

enum led_color {
  OFF = 0B000,
  GREEN = 0B010,
  RED = 0B100,
  BLUE = 0B001
};

volatile boolean display_needs_full_redraw = true;

volatile unsigned long pulse_dt_ms = ULONG_MAX;
volatile bool pulse_update = false;

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

  attachInterrupt(digitalPinToInterrupt(BUTTON), handle_btn, RISING);
  attachInterrupt(digitalPinToInterrupt(SENSOR), handle_pulse, FALLING);

  u8x8.clearDisplay() ;
  set_led(GREEN);
}

void set_led(led_color color) {
    digitalWrite(LED_R, color & 0b100);
    digitalWrite(LED_G, color & 0b010);
    digitalWrite(LED_B, color & 0b001);
};

volatile unsigned long pulse_previous_ms = 0;
void handle_pulse() {
  unsigned long now = millis();

  pulse_dt_ms = now - pulse_previous_ms;
  pulse_previous_ms = now;

  if (pulse_previous_ms != 0) {
    pulse_update = true;
  }
}

volatile uint8_t warning_state = 0;
void loop(void) {
  static uint8_t sensitivity_percent_previous = -1; // to check if the display needs an update
  static double pulse_hz = 0;
  unsigned long pause_measurement_until = 0;

  boolean update_pulse_display = false;

  // Handle detected pulse
  if (pulse_update) {
    // Update pulse rate estimation
    set_led(RED);
    digitalWrite(BUZZER, HIGH);
    delay(50);
    set_led(GREEN);
    digitalWrite(BUZZER, LOW);
    pulse_update = false;
    pulse_hz = 1000.0/pulse_dt_ms;
    update_pulse_display = true;

  } else if (pulse_hz != 0 && millis() - pulse_previous_ms > 10*1000) {
    // Reset pulse rate estimation to 0 if nothing happened for 10 seconds
    pulse_hz = 0;
    update_pulse_display = true;
  }

  uint32_t potVal = analogRead(POT_INPUT);

  uint8_t sensitivity_percent = map(potVal, 0, 1020, 50, 100);
  double effective_pulse_limit_hz = ((double) PULSE_LIMIT_HZ)/(sensitivity_percent)*100;

  if (warning_state == 0) {
    //--- Zustand: Keine Warnungen ---//
    set_led(GREEN);
    digitalWrite(BUZZER, LOW);
    
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

    if (update_pulse_display || display_needs_full_redraw) {
      u8x8.setFont(u8x8_font_8x13_1x2_r);
      u8x8.drawString(0, 12, "Pulse:       ");
      u8x8.setCursor(7, 12);
      u8x8.print(pulse_hz);
      u8x8.print(" Hz  ");

      Serial.print("pulse");
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
  } else {
    sensitivity_percent_previous = 0;
  }

  if (pause_measurement_until > millis()) return;

  // Check for state transitions
  if (pulse_hz > effective_pulse_limit_hz) {
    warning_state++;
    display_needs_full_redraw = true;
  } else {
    warning_state = 0;
    return;
  }

  // State was changed.

  if (warning_state >= 1 && warning_state < 3) {
    //--- Zustand: Schwellwert überschritten ---//
    u8x8.clearDisplay();
    set_led(RED);
    digitalWrite(BUZZER, HIGH);

    u8x8.setFont(u8x8_font_px437wyse700b_2x2_f);
    u8x8.drawString(1, 10, "GEFAHR!");
    u8x8.setFont(u8x8_font_8x13_1x2_r);
    u8x8.setCursor(11, 12);
    u8x8.print(warning_state);
    //u8x8.drawString(11, 12, "     ");

    digitalWrite(BUZZER, LOW);
    pause_measurement_until = millis() + 1000000;
  }

  if (warning_state == 3) {
    //--- Zustand: Not-Stopp androhen
    
    u8x8.clearDisplay();
    u8x8.setFont(u8x8_font_px437wyse700b_2x2_f);
    u8x8.drawString(2, 5, "START");
    u8x8.drawString(1, 8, "NOT-AUS");

    set_led(RED);
    digitalWrite(BUZZER, HIGH);

    pause_measurement_until = millis() + 3000000;

    warning_state = 4;
    return;
  }

  if (warning_state == 4) {
    // Not-Stopp ausführen
    u8x8.clearDisplay();
    u8x8.setFont(u8x8_font_px437wyse700b_2x2_f);
    u8x8.drawString(0, 1, "*Taster*");
    u8x8.setFont(u8x8_font_8x13_1x2_r);
    u8x8.drawString(0, 4, "druecken, wenn");
    u8x8.drawString(0, 6, "Feuer GELOESCHT!");
    u8x8.drawString(0, 9, "Fortfahren  mit");
    u8x8.drawString(3, 11, "* ENTER * ");
    u8x8.drawString(0, 13, "auf LASER-PANEL");

    digitalWrite(PAUSE_LASER, HIGH);
  }
}

void handle_btn() {
  u8x8.clearDisplay();
  display_needs_full_redraw = true;

  if (warning_state == 4) {
    warning_state = 0;
  }
}