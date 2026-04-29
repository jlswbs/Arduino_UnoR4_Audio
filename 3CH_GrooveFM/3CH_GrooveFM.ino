// 3 channel lofi groove FM generator + reverb //

#include "FspTimer.h"

FspTimer audio_timer;

#define SAMPLE_RATE 44100
#define BPM 135

#define REV_SIZE 4410
uint16_t revBuf[REV_SIZE];
uint16_t revIdx = 0;
uint8_t revFeedback = 190;

uint32_t phase[3] = {0, 0, 0};
uint32_t baseInc[3] = {0, 0, 0};
uint8_t  modDepth[3] = {140, 180, 120};

volatile uint32_t stepCounter = 0;
volatile uint8_t pendingSteps = 0;

uint32_t samplesPerStep;

uint16_t lfsr = 0xACE1u;

uint32_t midiTable[128];

inline uint8_t fastNoise() {

  uint8_t bit = lfsr & 1;
  lfsr >>= 1;
  if (bit) lfsr ^= 0xB400u;
  return lfsr & 0xFF;

}

inline uint32_t midiToPhaseInc(uint8_t midi) {
  return midiTable[midi];
}

const uint8_t scale[] = {48, 51, 53, 55, 58, 60, 63, 67};
#define SCALE_LEN 8

const int8_t transitions[] = {-2, -1, 0, 1, 2, 3, -3};

uint8_t currentLeadIndex = 0;
uint8_t currentBassIndex = 0;

uint8_t nextNote(uint8_t currentIndex) {

  int8_t move = transitions[fastNoise() % 7];
  int8_t next = currentIndex + move;

  if (next < 0) next = 0;
  if (next >= SCALE_LEN) next = SCALE_LEN - 1;

  return next;

}


void timer_callback(timer_callback_args_t __attribute((unused)) *p_args) {

  phase[2] += baseInc[2];
  uint32_t modOut = phase[2] >> 23;

  uint32_t pm2 = (uint32_t)modOut * modDepth[1] >> 3;
  phase[1] += baseInc[1] + pm2 + ((phase[0] >> 26) * 18);

  uint32_t pm1 = (uint32_t)modOut * modDepth[0] >> 2;
  phase[0] += baseInc[0] + pm1 + ((phase[1] >> 25) * 22);

  if (modDepth[2] > 80) {
    phase[2] += (phase[2] >> 20) * (modDepth[2] >> 4);
  }

  uint8_t bassSquare = (phase[0] & 0x80000000UL) ? 128 : 22;
  uint8_t leadSaw   = phase[1] >> 24;
  uint8_t modGrit   = modOut >> 5;

  static uint8_t kick = 0;

  uint16_t dry = (uint16_t)bassSquare + leadSaw + modGrit + (fastNoise() >> 5);
  uint16_t wet = revBuf[revIdx];
  uint32_t mixed = dry + (wet >> 1);
  
  if (mixed > 4095) mixed = 4095;

  uint16_t nextVal = (uint16_t)(((uint32_t)dry * 120 + (uint32_t)wet * revFeedback) >> 8);
  revBuf[revIdx] = (nextVal + wet) >> 1;

  revIdx++;
  if (revIdx >= REV_SIZE) revIdx = 0;

  analogWrite(DAC, (uint16_t)mixed);

  stepCounter++;

  if (stepCounter >= samplesPerStep) {
    stepCounter = 0;
    pendingSteps++;
  }

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

  samplesPerStep = (uint32_t)SAMPLE_RATE * 60UL / BPM / 4;

  for (int i = 0; i < 128; i++) {
    float freq = 440.0f * powf(2.0f, (i - 69) / 12.0f);
    midiTable[i] = (uint32_t)(freq * (4294967296.0f / SAMPLE_RATE) + 0.5f);
  }

  analogWriteResolution(12);
  beginTimer(SAMPLE_RATE);

  lfsr = random(0xFFFF);

  currentLeadIndex = fastNoise() % SCALE_LEN;
  currentBassIndex = fastNoise() % SCALE_LEN;

}

void loop() {

  while (pendingSteps > 0) {
    
    pendingSteps--;

    currentLeadIndex = nextNote(currentLeadIndex);

    if ((fastNoise() & 3) == 0) {
      currentBassIndex = nextNote(currentBassIndex);
    }

    uint8_t leadNote = scale[currentLeadIndex];
    uint8_t bassNote = scale[currentBassIndex] - 12;

    if (fastNoise() & 1) {
      baseInc[1] = midiToPhaseInc(leadNote);
    } else {
      baseInc[1] = 0;
    }

    baseInc[0] = midiToPhaseInc(bassNote);

    baseInc[2] = baseInc[1] * (2 + (fastNoise() & 3));

    modDepth[0] = (modDepth[0] * 250 + fastNoise()) >> 8;
    modDepth[1] = (modDepth[1] * 246 + fastNoise()) >> 8;
    modDepth[2] = 80 + (fastNoise() & 127);

  }

}