// MemorySequence

#include <avr/wdt.h>
#include <util/delay.h>

#define COLOR_GREEN_MASK 0b00000001
#define COLOR_RED_MASK 0b00000010
#define COLOR_BLUE_MASK 0b00010000
#define COLOR_YELLOW_MASK 0b00001000
#define COLOR_ALL_MASK (COLOR_GREEN_MASK | COLOR_RED_MASK | COLOR_YELLOW_MASK | COLOR_BLUE_MASK)
#define BUZZER_MASK 0b00000100

#define WRONG_TONE 255
#define BUTTON_TONE_DURATION 9
#define ERROR_TONE_DURATION 50

#define JINGLE_NOTE_DURATION 6
#define FANFARE_NOTE_DURATION 4
#define MELODY_NOTE_GAP 2

#define GAME_DELAY_SHORT 6
#define GAME_DELAY_MEDIUM 12
#define GAME_DELAY_LONG 31
#define ERROR_DELAY 16

#ifdef WDTCR
#  define WDT_CSR WDTCR
#  define WDT_IE WDTIE
#else
#  define WDT_CSR WDTCSR
#  define WDT_IE WDIE
#endif

#if F_CPU >= 16000000L
#  define CS_TONE ((1 << CS01) | (1 << CS00))
#  define CS_BUZZ (1 << CS02)
#else
#  define CS_TONE (1 << CS01)
#  define CS_BUZZ (1 << CS01)
#endif

#define leds_off()            \
  do {                        \
    DDRB &= ~COLOR_ALL_MASK; \
    PORTB |= COLOR_ALL_MASK; \
  } while (0)
#define leds_on()              \
  do {                         \
    DDRB |= COLOR_ALL_MASK;   \
    PORTB &= ~COLOR_ALL_MASK; \
  } while (0)
#define leds_set(on) \
  do {               \
    if (on)          \
      leds_on();     \
    else             \
      leds_off();    \
  } while (0)
#define buttons_pressed() (~PINB & COLOR_ALL_MASK)

const uint8_t masks[4] PROGMEM = {COLOR_GREEN_MASK, COLOR_RED_MASK, COLOR_YELLOW_MASK, COLOR_BLUE_MASK};
const uint8_t tones[4] PROGMEM = {119, 141, 178, 212};
const uint8_t patterns[4] PROGMEM = {8, 14, 20, 31};

struct Note {
  uint8_t tone;
  uint8_t led;
};

const Note jingle[] PROGMEM = {
  {0, COLOR_GREEN_MASK},
  {1, COLOR_RED_MASK},
  {2, COLOR_YELLOW_MASK},
  {3, COLOR_BLUE_MASK}
};

const Note fanfare[] PROGMEM = {
  {2, COLOR_YELLOW_MASK},
  {3, COLOR_BLUE_MASK},
  {2, COLOR_YELLOW_MASK},
  {3, COLOR_BLUE_MASK},
  {0, COLOR_ALL_MASK},
  {1, COLOR_ALL_MASK},
  {2, COLOR_ALL_MASK},
  {3, COLOR_ALL_MASK}
};

volatile uint32_t time;
uint8_t pattern[31];

#if F_CPU >= 16000000L
ISR(WDT_vect) {
}
void delay_wdt(uint8_t t) {
  while (t--) _delay_ms(16);
}
#else
ISR(WDT_vect) {
  time++;
}
void delay_wdt(uint8_t t) {
  uint32_t target = time + t;
  while (time < target);
}
#endif

#if F_CPU >= 16000000L
ISR(TIMER0_COMPA_vect) {
  PORTB ^= BUZZER_MASK;
}
#else
ISR(TIM0_COMPA_vect) {
  PORTB ^= BUZZER_MASK;
}
#endif

void setup() {
#if F_CPU < 16000000L
  cli();
  wdt_reset();
  MCUSR &= ~(1 << WDRF);
  WDT_CSR |= (1 << WDCE) | (1 << WDE);
  WDT_CSR = (1 << WDT_IE);
#endif
  leds_off();
  TCCR0A = 0;
  TCCR0B = 0;
  TCNT0 = 0;
  TIMSK0 = 0;
  sei();
}

void loop() {
  leds_off();

  play_melody(jingle, sizeof(jingle) / sizeof(Note), JINGLE_NOTE_DURATION, MELODY_NOTE_GAP);
  delay_wdt(GAME_DELAY_MEDIUM);
  uint8_t btnIdx = wait_for_button(true);
  delay_wdt(GAME_DELAY_LONG);

  uint8_t max_len = pgm_read_byte(&patterns[btnIdx]);
  generate_pattern(max_len);
  for (uint8_t len = 1; len <= max_len; len++) {
    show_pattern(len);
    if (!check_answer(len)) return;
    delay_wdt(GAME_DELAY_LONG);
  }
  delay_wdt(GAME_DELAY_MEDIUM);
  play_melody(fanfare, sizeof(fanfare) / sizeof(Note), FANFARE_NOTE_DURATION, MELODY_NOTE_GAP);
  delay_wdt(GAME_DELAY_MEDIUM);
}

void generate_pattern(uint8_t max_len) {
  uint8_t seed = time;
  uint8_t repeat = 0, last = 4;
  for (uint8_t i = 0; i < max_len; i++) {
    uint8_t r;
    do {
      seed ^= seed << 7;
      seed ^= seed >> 5;
      seed ^= seed << 3;
      r = (seed ^ (seed >> 4)) & 3;
    } while (r == last && repeat >= 2);
    repeat = (r == last) ? repeat + 1 : 0;
    last = r;
    pattern[i] = r;
  }
}

void show_pattern(uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    uint8_t btn = pattern[i];
    play_tone(pgm_read_byte(&tones[btn]), BUTTON_TONE_DURATION, pgm_read_byte(&masks[btn]));
    delay_wdt(GAME_DELAY_SHORT);
  }
}

uint8_t check_answer(uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    uint8_t btnIdx = wait_for_button(false);
    play_tone(pgm_read_byte(&tones[btnIdx]), BUTTON_TONE_DURATION, pgm_read_byte(&masks[btnIdx]));
    while (buttons_pressed()) delay_wdt(1);
    if (btnIdx != pattern[i]) {
      play_tone(WRONG_TONE, ERROR_TONE_DURATION, COLOR_ALL_MASK);
      delay_wdt(ERROR_DELAY);
      return 0;
    }
  }
  return 1;
}

uint8_t wait_for_button(bool blink) {
  uint8_t led_state = 0;
  uint8_t blink_count = 0;

  for (;;) {
    if (blink && blink_count == 0) {
      leds_set(led_state);
    }

    delay_wdt(2);
    leds_off();
    if (blink) delay_wdt(2);

    uint8_t clicked = buttons_pressed();
    if (clicked) {
      while (buttons_pressed()) delay_wdt(1);
      if (clicked & COLOR_GREEN_MASK) return 0;
      if (clicked & COLOR_RED_MASK) return 1;
      if (clicked & COLOR_YELLOW_MASK) return 2;
      if (clicked & COLOR_BLUE_MASK) return 3;
    }

    if (blink) {
      blink_count++;
      if (blink_count >= 24) {
        blink_count = 0;
        led_state ^= 1;
      }
    }
  }
}

void play_tone(uint8_t frequency, uint8_t duration, uint8_t leds) {
  PORTB = 0;
  DDRB = leds | BUZZER_MASK;
  TCCR0A = (1 << WGM01);
  TCNT0 = 0;
  OCR0A = frequency;
  TCCR0B = (1 << WGM02) | CS_TONE;
  TIMSK0 = (1 << OCIE0A);
  delay_wdt(duration);
  TIMSK0 = 0;
  TCCR0A = 0;
  TCCR0B = 0;
  leds_off();
}

void play_melody(const Note* notes, uint8_t len, uint8_t duration, uint8_t gap) {
  for (uint8_t i = 0; i < len; i++) {
    uint8_t tone_idx = pgm_read_byte(&notes[i].tone);
    uint8_t led_mask = pgm_read_byte(&notes[i].led);
    play_tone(pgm_read_byte(&tones[tone_idx]), duration, led_mask);
    delay_wdt(gap);
  }
}
