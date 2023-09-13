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
#define BUZZER 6
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

volatile boolean btn_pressed = false;
volatile boolean display_needs_full_redraw = true;

volatile unsigned long pulse_dt_us = ULONG_MAX;
volatile bool pulse_update = false;

// Display driver parameters:          CLK data CS  DC RST
U8X8_SSD1327_WS_128X128_4W_SW_SPI u8x8(13, 11,  10, 7, 8  );

void setup(void) {
  analogReference(EXTERNAL); // AREF an 3.3 V von Nano angeschlossen

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

void handle_btn() {
  u8x8.clearDisplay();
  display_needs_full_redraw = true;

  digitalWrite(PAUSE_LASER, LOW);
  digitalWrite(AIR_EXTINGUISH, LOW);

  set_led(GREEN);
  btn_pressed = true;
}

volatile unsigned long pulse_previous_us = 0;
void handle_pulse() {
  unsigned long now = micros();

  pulse_dt_us = now - pulse_previous_us;

  pulse_previous_us = now;

  if (pulse_previous_us != 0) {
    pulse_update = true;
  }
}

void loop(void) {
  static uint8_t warning_state = 0;
  static uint8_t sensitivity_percent_previous = -1; // to check if the display needs an update
  static double pulse_hz = 0;

  boolean update_pulse_display = false;

  // Beep if fire was detected
  if (pulse_update) {
    set_led(RED);
    digitalWrite(BUZZER, HIGH);
    delay(50);
    set_led(GREEN);
    digitalWrite(BUZZER, LOW);
    pulse_update = false;
    pulse_hz = 1000000.0/pulse_dt_us;
    update_pulse_display = true;
  } else if (pulse_hz != 0 && micros() - pulse_previous_us > 10000000) {
    pulse_hz = 0;
    update_pulse_display = true;
  }

  uint32_t potVal = analogRead(POT_INPUT); // liest den Analogwert vom Potentiometer

  uint8_t sensitivity_percent = map(potVal, 0, 1020, 50, 100);
  double effective_pulse_limit_hz = ((double) PULSE_LIMIT_HZ)/(sensitivity_percent)*100;

  if (warning_state == 0) {
    //--- Zustand: Keine Warnungen ---//
    
    if (display_needs_full_redraw) {
      set_led(GREEN);
      digitalWrite(BUZZER, LOW);

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
      Serial.println(pulse_dt_us);
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

  // Check for state transitions
  uint8_t warning_state_previous = warning_state;
  if (pulse_hz > effective_pulse_limit_hz) {
    warning_state++;
    display_needs_full_redraw = true;
    pulse_hz = 0;
  } else {
    warning_state = 0;
  }

  if (warning_state_previous == warning_state) return;

  // Entering a new state

  if (warning_state == 1) {
    //--- Zustand: Schwellwert einmalig überschritten ---//
    u8x8.clearDisplay();
    set_led(RED);
    digitalWrite(BUZZER, HIGH); // Schalte den Piezo-Lautsprecher an.

    u8x8.setFont(u8x8_font_px437wyse700b_2x2_f);
    u8x8.drawString(1, 10, "GEFAHR!");
    u8x8.setFont(u8x8_font_8x13_1x2_r);
    u8x8.drawString(11, 12, "     ");

    delay(1000);
    digitalWrite(BUZZER, LOW); // Schalte den Piezo-Lautsprecher aus.
    delay(1000);
  }

  if (warning_state == 2) {
    //--- Zustand: Schwellwert doppelt überschritten: Not-Aus ---//
    warning_state = 0;
    
    u8x8.clearDisplay();
    u8x8.setFont(u8x8_font_px437wyse700b_2x2_f);
    u8x8.drawString(2, 5, "START");
    u8x8.drawString(1, 8, "NOT-AUS");
    //  u8x8.drawString(1, 11, "");
    delay(2000);

    btn_pressed = false;
    set_led(RED);
    digitalWrite(BUZZER, HIGH); // Schalte den Piezo-Lautsprecher an.

    delay(1000); // x Sekunden bis Auslösung
  
    u8x8.clearDisplay();
    u8x8.setFont(u8x8_font_px437wyse700b_2x2_f);
    u8x8.drawString(0, 1, "*Taster*");
    u8x8.setFont(u8x8_font_8x13_1x2_r);
    u8x8.drawString(0, 4, "druecken, wenn");
    u8x8.drawString(0, 6, "Feuer GELOESCHT!");
    u8x8.drawString(0, 9, "Fortfahren  mit");
    u8x8.drawString(3, 11, "* ENTER * ");
    u8x8.drawString(0, 13, "auf LASER-PANEL");

    digitalWrite(PAUSE_LASER, HIGH);  // Oeffner von Relais 1 betätigen  (Door Protection)

    while (!btn_pressed) {
      // halt until button is pressed
      delay(100);
    }
    set_led(GREEN);
  }
}

