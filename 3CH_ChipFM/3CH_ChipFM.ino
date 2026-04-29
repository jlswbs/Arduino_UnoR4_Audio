// 3 channel lofi FM chiptune generator //

#include "FspTimer.h"

FspTimer audio_timer;

#define SAMPLE_RATE 16000
#define BPM 135

#define FIXED_SHIFT 16
#define FIXED_SCALE (1UL << FIXED_SHIFT)

const uint32_t STEP_LEN_SAMPLES = (uint32_t)SAMPLE_RATE * 60 / BPM / 4;

volatile uint32_t global_sample_count = 0;
volatile uint32_t phase_1 = 0;
volatile uint32_t phase_2 = 0;
volatile uint32_t incr_1 = 0;
volatile uint32_t incr_2 = 0;
volatile uint8_t drum_type = 0;

uint32_t scale_incs[6];
uint16_t lfsr = 0xACE1u;

inline uint8_t fastNoise() {

  uint8_t bit = lfsr & 1;
  lfsr >>= 1;
  if (bit) lfsr ^= 0xB400u;
  return lfsr & 0xFF;

}
const uint8_t decay_table[32] PROGMEM = {

  255, 220, 190, 160, 135, 115, 100, 85,
  70, 60, 50, 42, 35, 30, 25, 20,
  16, 12, 10, 8, 6, 5, 4, 3,
  2, 1, 1, 0, 0, 0, 0, 0

};

void timer_callback(timer_callback_args_t __attribute((unused)) *p_args) {

  global_sample_count++;
  uint32_t local_idx = global_sample_count % STEP_LEN_SAMPLES;
  uint8_t env = pgm_read_byte(&decay_table[(local_idx >> 6) & 0x1F]);

  phase_1 += incr_1 + (phase_2 >> 8); 
  phase_2 += incr_2;
  
  int8_t tri = (int8_t)((phase_2 >> 8) ^ (phase_2 >> 15));
  int8_t osc1 = (phase_1 & 0x8000) ? 20 : -20;
  
  int16_t osc1_env = (osc1 * env) >> 8;
  int16_t tri_env = (tri * env) >> 10;
  
  int16_t noise = 0;
  if (drum_type == 0) {
     noise = (local_idx < 400) ? (fastNoise() & env) : 0;
  } else if (drum_type == 2) {
     noise = (fastNoise() ^ (local_idx << 2)) & env;
     noise >>= 2;
  }

  int32_t raw_signal = (osc1_env + tri_env + (noise - 64));
  raw_signal = (raw_signal * (10 + (env >> 3))) / 10; 

  int32_t mixed = 2048 + (raw_signal << 4);

  if(mixed < 0) mixed = 0;
  if(mixed > 4095) mixed = 4095;
  
  analogWrite(DAC, (uint16_t)mixed);

}

bool beginTimer(float rate) {

  uint8_t timer_type = GPT_TIMER;
  int8_t tindex = FspTimer::get_available_timer(timer_type);
  if (tindex==0){
    FspTimer::force_use_of_pwm_reserved_timer();
    tindex = FspTimer::get_available_timer(timer_type);
  }
  if (tindex==0){
    return false;
  }

  if(!audio_timer.begin(TIMER_MODE_PERIODIC, timer_type, tindex, rate, 0.0f, timer_callback)){
    return false;
  }

  if (!audio_timer.setup_overflow_irq()){
    return false;
  }

  if (!audio_timer.open()){
    return false;
  }

  if (!audio_timer.start()){
    return false;
  }

  return true;

}

void setup() {

  const float freqs[] = {73.42, 82.41, 98.00, 110.0, 123.47, 146.83};
  for(uint8_t i = 0; i < 6; i++) scale_incs[i] = (uint32_t)((freqs[i] * FIXED_SCALE) / SAMPLE_RATE);
  
  analogWriteResolution(12);
  beginTimer(SAMPLE_RATE);

  lfsr = random(0xFFFF);

}

void loop() {

  static uint32_t last_step = 999;
  uint32_t current_step = global_sample_count / STEP_LEN_SAMPLES;

  if(current_step != last_step) {

    last_step = current_step;
    uint32_t r = random(0xFFFF); 

    static uint8_t last_note = 0;
    if ((r & 0xFF) > 70) { 
       last_note = (r >> 4) % 6; 
    }
    
    uint8_t oct = ((r >> 8) % 10 > 7) ? 3 : 2;
    incr_1 = scale_incs[last_note] << oct;
    incr_2 = scale_incs[(last_note + (r >> 12) % 3) % 6] << 1;

    if ((current_step % 4) == 0) {
      drum_type = ((r & 0x3) > 0) ? 0 : 3; 
    } else {
      drum_type = (r >> 14) & 0x3;
    }

  }

}