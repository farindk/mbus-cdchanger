/*
  Alpine M-Bus CD-changer controller.

  MIT License

  Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/


#include <LiquidCrystal.h>

const char* nibble = "0123456789abcdef";

// --- ISR

volatile unsigned long lastTime = 0;
volatile uint8_t mbus;

volatile uint8_t flushed = 1;
volatile uint8_t bits = 0;
volatile uint8_t nBits = 0;

volatile uint8_t ignore_input = 0;

volatile uint8_t input_buffer[256];
volatile uint8_t input_ptr = 0;

// --- main thread

uint8_t command[16];
uint8_t commandLength = 0;

const uint8_t commandLengthTable[16] = {
  1, 1, 1, 1, 1, 1, 1, 1,
  2, 15, 12, 11, 12, 10, 9, 7
};

struct cd_time {
  uint8_t track;

  uint8_t min10, min01;
  uint8_t sec10, sec01;

  void serialPrint_track() {
    Serial.print((int)(track));
  }

  void serialPrint_time() {
    Serial.write('0' + min10);
    Serial.write('0' + min01);
    Serial.write(':');
    Serial.write('0' + sec10);
    Serial.write('0' + sec01);
  }
};

struct cd_info : public cd_time {
  bool known = false;
  bool empty = false;
};

cd_info cdinfo[6];

uint8_t current_disk = 1;
uint8_t current_track = 1;

uint8_t selected_disk = 1;
uint8_t selected_track = 1;

cd_time current_time;

enum class State {
  Loading,
  Failure_NoDisc,
  Stopped_Info,
  Stopped_Select,
  Playing_Info,
  Playing_Select
};

volatile State state = State::Stopped_Info;

LiquidCrystal lcd(6, 9, 5,4,3,2);

byte char_play[8] = {
  B11000,
  B11100,
  B11110,
  B11111,
  B11110,
  B11100,
  B11000,
  B00000
};

byte char_pause[8] = {
  B11011,
  B11011,
  B11011,
  B11011,
  B11011,
  B11011,
  B11011,
  B00000
};

byte char_stop[8] = {
  B00000,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B00000,
  B00000
};

byte char_empty[8] = {
  B00000,
  B01110,
  B10001,
  B10101,
  B10001,
  B01110,
  B00000,
  B00000
};

byte char_full[8] = {
  B00000,
  B01110,
  B11111,
  B11011,
  B11111,
  B01110,
  B00000,
  B00000
};

byte char_rot[4][8] = { { B00000,
                          B01110,
                          B11101,
                          B11001,
                          B11111,
                          B01110,
                          B00000,
                          B00000 },
                        { B00000,
                          B01110,
                          B11111,
                          B11011,
                          B11001,
                          B01110,
                          B00000,
                          B00000 },
                        { B00000,
                          B01110,
                          B11111,
                          B10011,
                          B10111,
                          B01110,
                          B00000,
                          B00000 },
                        { B00000,
                          B01110,
                          B10011,
                          B11011,
                          B11111,
                          B01110,
                          B00000,
                          B00000 } };

byte char_unknown[8] = {
  B00000,
  B00000,
  B00100,
  B01110,
  B00100,
  B00000,
  B00000,
  B00000
};

byte char_ellipsis[8] = {
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B10101,
  B00000
};



void output_nibble() {
  char c = nibble[bits];

  if (input_ptr < 256) {
    input_buffer[input_ptr++] = bits;
  }

  bits = 0;
  nBits = 0;
  flushed = 0;
}

void flush_bits() {
  nBits = 0;
  bits = 0;
}

void add_bit(uint8_t b) {
  bits <<= 1;
  bits |= b;
  nBits++;

  if (nBits == 4) {
    output_nibble();
  }
}

void isr7() {
  if (ignore_input) {
    return;
  }

  unsigned long time = micros();
  unsigned long duration = time - lastTime;
  lastTime = time;

  if (mbus == LOW) {
    if (duration > (unsigned long)400 && duration < (unsigned long)1000) {
      add_bit(0);
    } else if (duration > (unsigned long)1400 && duration < (unsigned long)2400) {
      add_bit(1);
    }
  }

  if (mbus == HIGH && duration > (unsigned long)4000) {
    input_ptr = 0;
    nBits = 0;
    bits = 0;
  }

  mbus = digitalRead(7);
}

void setup() {
  lastTime = micros();

  pinMode(7, INPUT);
  pinMode(8, OUTPUT);
  digitalWrite(8, LOW);

  pinMode(10, INPUT_PULLUP);
  pinMode(16, INPUT_PULLUP);
  pinMode(15, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
  pinMode(18, INPUT_PULLUP);
  pinMode(19, INPUT_PULLUP);
  pinMode(20, INPUT_PULLUP);
  pinMode(21, INPUT_PULLUP);

  lcd.begin(8, 2);
  lcd.clear();
  delay(100);
  //lcd.print("Hello\0");

  lcd.createChar(1, char_play);
  lcd.createChar(2, char_pause);
  lcd.createChar(3, char_ellipsis);
  lcd.createChar(4, char_empty);
  lcd.createChar(5, char_full);
  lcd.createChar(6, char_rot[0]);
  lcd.createChar(7, char_unknown);
  lcd.setCursor(0, 1);
  lcd.write(1);
  lcd.write(2);
  lcd.write(3);
  lcd.write(4);
  lcd.write(5);
  lcd.write(6);
  lcd.write(7);

  mbus = digitalRead(7);

  attachInterrupt(digitalPinToInterrupt(7), isr7, CHANGE);
}

#define CHAR_PLAY 1
#define CHAR_PAUSE 2
#define CHAR_STOP 3
#define CHAR_ELLIPSIS 3

#define CHAR_EMPTY 4
#define CHAR_FULL 5
#define CHAR_ROTATING 6
#define CHAR_UNKNOWN 7

uint16_t cnt = 0;
uint8_t led = 0;
uint8_t checksum = 0;

void transmit_nibble(uint8_t v) {

  ignore_input = 1;

  int i;
  for (i = 0; i < 4; i++) {
    unsigned long startTime = micros();
    digitalWrite(8, HIGH);  // this is inverted
    unsigned long onTime = (v & 0x8) ? 1800 : 600;

    for (;;) {
      unsigned long now = micros();
      unsigned long duration = now - startTime;
      if (duration >= onTime) {
        break;
      }
    }

    digitalWrite(8, LOW);  // this is inverted

    for (;;) {
      unsigned long now = micros();
      unsigned long duration = now - startTime;
      if (duration >= 3000) {
        break;
      }
    }

    v <<= 1;
  }

  ignore_input = 0;

  lastTime = micros();
}


uint8_t cmd_buf[20];
uint8_t queued_command_length = 0;

#define FLAG_PLAY 1
#define FLAG_PAUSE 2
#define FLAG_RANDOM 4
#define FLAG_STOP 0x40

void send_queued_command() {
  for (int i = 0; i < queued_command_length; i++) {
    transmit_nibble(cmd_buf[i]);
  }

  queued_command_length = 0;
}

void send_nibble(uint8_t v) {
  cmd_buf[queued_command_length++] = v;
  checksum ^= v;
}

void send_ping() {
  checksum = 0;
  send_nibble(1);
  send_nibble(8);
  send_nibble(checksum + 1);
}

void send_set_disk(uint8_t disk, uint8_t track, uint8_t flags) {
  checksum = 0;
  send_nibble(1);
  send_nibble(1);
  send_nibble(3);
  send_nibble(disk);
  send_nibble(track / 10);
  send_nibble(track % 10);
  send_nibble(flags >> 4);
  send_nibble(flags & 0xF);
  send_nibble(checksum + 1);
}

void send_play_state(uint8_t flags) {
  checksum = 0;
  send_nibble(1);
  send_nibble(1);
  send_nibble(1);
  send_nibble(flags >> 4);
  send_nibble(flags & 0xF);
  send_nibble(checksum + 1);
}

void process_command() {
  if (commandLength < 3) {
    commandLength = 0;
    return;
  }

  // check checksum

  uint8_t checksum = 0;
  for (int i = 0; i < commandLength - 1; i++) {
    checksum ^= command[i];
  }
  checksum = (checksum + 1) & 0xF;

  if (checksum != command[commandLength - 1]) {
    Serial.print("INVALID CHECKSUM ");
    Serial.print(nibble[checksum]);
    Serial.print(":");
    Serial.println(nibble[command[commandLength - 1]]);
    commandLength = 0;
    return;
  }

  /*
  Serial.print("CMD: ");
  for (int i=0;i<commandLength;i++) {
    Serial.print(nibble[command[i]]);
  }
  Serial.println("");
  */
  
  if (commandLength == 16 && command[0] == 9 && command[1] == 9) {
    cd_time t = current_time;
    t.track = (command[3] * 10 + command[4]);
    t.min10 = command[7];
    t.min01 = command[8];
    t.sec10 = command[9];
    t.sec01 = command[10];
    uint8_t index = (command[5] * 10) + command[6];
    uint8_t flags3 = command[14];

    /*
    Serial.print("track: ");
    t.serialPrint_track();
    Serial.print(" time: ");
    t.serialPrint_time();
    Serial.print('\n');
    */
    
    if (flags3 & FLAG_PLAY) {
    }

    if (flags3 & 0x02) {
      state = State::Stopped_Info;
    } else if (state == State::Playing_Select) {
      // NOP, stay in this state
    } else {
      state = State::Playing_Info;
    }

    if (t.track != 0) {
      current_track = t.track;
      current_time = t;
    }
  } else if (commandLength == 13 && command[0] == 9 && command[1] == 0x0c) {
    uint8_t disk = command[2];

    if (disk >= 1 && disk <= 6) {
      cd_info& info = cdinfo[disk - 1];

      info.track = (command[5] * 10 + command[6]);
      info.min10 = command[7];
      info.min01 = command[8];
      info.sec10 = command[9];
      info.sec01 = command[10];

      info.known = true;
      info.empty = false;

      current_disk = disk;
      selected_disk = disk;

      /*
      Serial.print("diskinfo: ");
      Serial.write('0' + disk);
      Serial.print(" track: ");
      info.serialPrint_track();
      Serial.print(" time: ");
      info.serialPrint_time();
      Serial.print('\n');
      */
    }
  } else if (commandLength == 12 && command[0] == 9 && command[1] == 0x0b) {
    uint8_t disk = command[3];
    uint8_t status = command[2];
    if (disk >= 1 && disk <= 6) {

      if (status == 0x0d) {
        if (!cdinfo[disk - 1].known) {
          cdinfo[disk - 1].known = true;
          cdinfo[disk - 1].empty = true;
        }
      } else if (status == 0x09) {
        cdinfo[disk - 1].known = true;
        cdinfo[disk - 1].empty = false;
      }
    }

    if (status == 0x0a) {
      for (int i = 0; i < 6; i++) {
        cdinfo[i].known = false;
        cdinfo[i].empty = false;
      }
    }

    /*
    Serial.print("changing disk: ");
    Serial.write('0' + disk);
    Serial.print(" state: ");
    Serial.write(nibble[command[2]]);
    Serial.print('\n');
    */
    
    current_disk = disk;

    // ejecting: 9 d 8 a
    // inserting: b c d  (unsuccessful / empty)
    // inserting: b c 8 9 (successful)
  }

  // 9ba10020022c   // eject ?

  commandLength = 0;
}

char blinking(char on, char off, bool enable) {
  if (!enable) {
    return on;
  }

  unsigned long t = millis();
  if ((t / 256) % 2) {
    return on;
  } else {
    return off;
  }
}

void lcdWriteTrack(uint8_t track, bool blink) {
  if (track < 10) {
    lcd.write(' ');
  } else {
    lcd.write(blinking('0' + track/10, ' ', blink));
  }

  lcd.write(blinking('0' + track%10, ' ', blink));
}

void lcdWriteTime(const cd_time& cd, bool blink) {
  if (cd.min10 == 0) {
    lcd.write(' ');
  } else {
    lcd.write(blinking('0' + cd.min10, ' ', blink));
  }

  lcd.write(blinking('0' + cd.min01, ' ', blink));
  lcd.write(':');
  lcd.write(blinking('0' + cd.sec10, ' ', blink));
  lcd.write(blinking('0' + cd.sec01, ' ', blink));
}

char* string_loading = "Loading ";

void update_display()
{
  lcd.setCursor(0, 0);
  if (state == State::Loading) {
    string_loading[7] = CHAR_ELLIPSIS;
    lcd.print(string_loading);
  } else if (state == State::Failure_NoDisc) {
    lcd.print("No disc ");
  } else if (state == State::Stopped_Info) {
    const cd_info& cd = cdinfo[selected_disk - 1];
    if (cd.known && !cd.empty) {
      lcdWriteTrack(cd.track, false);
      lcd.write(' ');
      lcdWriteTime(cd, false);
    } else {
      lcd.print(" - --:--");
    }
  } else if (state == State::Stopped_Select || state == State::Playing_Select) {
    const cd_info& cd = cdinfo[selected_disk - 1];
    if (cd.known && !cd.empty) {
      lcdWriteTrack(selected_track, true);
      lcd.write('/');
      lcdWriteTrack(cd.track, false);
      lcd.print("   ");
    } else {
      lcd.print(" - --:--");
    }
  } else if (state == State::Playing_Info) {
    const cd_info& cd = cdinfo[current_disk - 1];
    if (cd.known && !cd.empty) {
      lcdWriteTrack(current_time.track, false);
      lcd.write(' ');
      lcdWriteTime(current_time, false);
    } else {
      lcd.print(" - --:--");
    }
  }

  // -------------------------

  lcd.setCursor(0, 1);
  if (state == State::Playing_Info || state == State::Playing_Select) {
    lcd.write(CHAR_PLAY);
  } else {
    lcd.write(' ');
  }

  lcd.write(' ');

  bool disk_blinking = (state == State::Loading || state == State::Stopped_Info || state == State::Stopped_Select || state == State::Playing_Select);
  for (int i = 0; i < 6; i++) {
    if (!cdinfo[i].known) {
      lcd.write(blinking(CHAR_UNKNOWN, ' ', disk_blinking && selected_disk == i + 1));
    } else if (cdinfo[i].empty) {
      lcd.write(blinking(CHAR_EMPTY, ' ', disk_blinking && selected_disk == i + 1));
    } else {
      char d = ((state == State::Playing_Info || state == State::Playing_Select) && current_disk == i+1) ? CHAR_ROTATING : CHAR_FULL;
      lcd.write(blinking(d, ' ', disk_blinking && selected_disk == i + 1));
    }
  }
}

void loop() {
  // put your main code here, to run repeatedly:

  unsigned long t = millis();

  uint8_t rotation_state = (t / 256) % 4;
  static uint8_t last_rotation_state = 0;
  if (rotation_state != last_rotation_state) {
    lcd.createChar(6, char_rot[rotation_state]);
    last_rotation_state = rotation_state;
  }

  update_display();

  noInterrupts();

  unsigned long now = micros();
  unsigned long duration = now - lastTime;


  while (input_ptr > 0 && input_buffer[0] != 9) {
    memmove(input_buffer, input_buffer + 1, input_ptr - 1);
    input_ptr--;
  }

  if (input_ptr >= 3) {
    uint8_t len = commandLengthTable[input_buffer[1]] + 1; // +1 for checksum
    if (input_ptr >= len) {
      memcpy(command, input_buffer, len);
      memmove(input_buffer, input_buffer + len, input_ptr - len);
      input_ptr -= len;
      commandLength = len;
    }
  }

  interrupts();

  if (commandLength > 0) {
    process_command();
  }

  if (mbus == HIGH && duration > (unsigned long)(20000UL)) {
    if (queued_command_length > 0) {
      send_queued_command();
    }
  }

  bool btn_play = (digitalRead(10) == LOW);
  bool btn_stop = (digitalRead(16) == LOW);
  bool btn_next_track = (digitalRead(14) == LOW);
  bool btn_prev_track = (digitalRead(15) == LOW);
  bool btn_next_disk = (digitalRead(18) == LOW);
  bool btn_prev_disk = (digitalRead(19) == LOW);

  bool all_released = !btn_play && !btn_stop && !btn_next_track && !btn_prev_track;
  static bool wait_for_release = false;

  if (wait_for_release) {
    wait_for_release = !all_released;
    if (wait_for_release == false) {
      delay(200);
    }
  } else if (btn_play) {
    wait_for_release = true;
    delay(200);

    if (state == State::Playing_Select) {
      state = State::Playing_Info;
    }

    if (cdinfo[selected_disk-1].known && cdinfo[selected_disk-1].empty) {
      return;
    }

    send_set_disk(selected_disk, selected_track, 0x10);
  } else if (btn_stop) {
    static uint8_t flagcnt = 0x01;

    send_set_disk(current_disk, 0, 0x20);
    delay(200);
    wait_for_release = true;
  } else if (btn_next_track) {
    const cd_info& cd = cdinfo[selected_disk - 1];
    if (!cd.known) return;

    if (selected_track < cd.track) {
      selected_track++;
      delay(200);
      wait_for_release = true;
    }

      if (state == State::Stopped_Info) {
        state = State::Stopped_Select;
      }
      else if (state == State::Playing_Info) {
        state = State::Playing_Select;
      }
  } else if (btn_prev_track) {
    const cd_info& cd = cdinfo[selected_disk - 1];
    if (!cd.known) return;

    if (selected_track > 1) {
      selected_track--;
      delay(200);
      wait_for_release = true;
    }

      if (state == State::Stopped_Info) {
        state = State::Stopped_Select;
      }
      else if (state == State::Playing_Info) {
        state = State::Playing_Select;
      }
  } else if (btn_next_disk) {
    if (selected_disk < 6) {
      selected_disk++;
      delay(200);
      wait_for_release = true;

      if (cdinfo[selected_disk - 1].empty) {
        selected_track = 1;
      }
      else {
        uint8_t nTracks = cdinfo[selected_disk - 1].track;
        if (selected_track > nTracks) {
          selected_track = nTracks;
        }
      }

      if (state == State::Playing_Info) {
        state = State::Playing_Select;
      }
    }
  } else if (btn_prev_disk) {
    if (selected_disk > 1) {
      selected_disk--;
      delay(200);
      wait_for_release = true;

      if (cdinfo[selected_disk - 1].empty) {
        selected_track = 1;
      }
      else {
        uint8_t nTracks = cdinfo[selected_disk - 1].track;
        if (selected_track > nTracks) {
          selected_track = nTracks;
        }
      }

      if (state == State::Playing_Info) {
        state = State::Playing_Select;
      }
    }
  }
}
