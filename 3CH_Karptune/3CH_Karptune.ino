// 3 channel karplus-strong generator //

#include "FspTimer.h"

FspTimer audio_timer;

#define SAMPLE_RATE 22050
#define BPM 128
#define FIXED_SHIFT 16
#define FIXED_SCALE (1UL << FIXED_SHIFT)

#define BUF_SIZE_1 150
#define BUF_SIZE_2 200
#define BUF_SIZE_3 60

int16_t delay_line1[BUF_SIZE_1];
int16_t delay_line2[BUF_SIZE_2];
int16_t delay_line3[BUF_SIZE_3];

uint16_t ptr1 = 0, ptr2 = 0, ptr3 = 0;
uint16_t len1 = BUF_SIZE_1, len2 = BUF_SIZE_2, len3 = BUF_SIZE_3;

volatile uint32_t global_sample_count = 0;
const uint32_t STEP_LEN_SAMPLES = (uint32_t)SAMPLE_RATE * 60 / BPM / 4;

uint32_t scale_freqs[] = {110, 130, 146, 164, 196, 220};

uint16_t lfsr = 0xACE1u;
inline int16_t noise_gen() {
    
    uint8_t bit = lfsr & 1;
    lfsr >>= 1;
    if (bit) lfsr ^= 0xB400u;
    return (int16_t)((lfsr & 0xFF) - 128) << 4;

}

void timer_callback(timer_callback_args_t __attribute((unused)) *p_args) {

    global_sample_count++;

    int16_t out1 = delay_line1[ptr1];
    uint16_t next_ptr1 = (ptr1 + 1 >= len1) ? 0 : ptr1 + 1;
    delay_line1[ptr1] = ((out1 + delay_line1[next_ptr1]) >> 1) * 995 >> 10;
    ptr1 = next_ptr1;

    int16_t out2 = delay_line2[ptr2];
    int16_t feedback2 = (out2 ^ (out2 >> 2)) * 990 >> 10;
    delay_line2[ptr2] = feedback2;
    ptr2 = (ptr2 + 1 >= len2) ? 0 : ptr2 + 1;

    int16_t out3 = delay_line3[ptr3];
    delay_line3[ptr3] = (out3 * 960 >> 10);
    ptr3 = (ptr3 + 1 >= len3) ? 0 : ptr3 + 1;

    int32_t mixed = 2048 + (out1 >> 2) + (out2 >> 2) + (out3 >> 1);
    
    if(mixed < 0) mixed = 0;
    if(mixed > 4095) mixed = 4095;
    analogWrite(DAC, (uint16_t)mixed);

}

void trigger(int channel, uint32_t freq) {

    uint16_t samples = SAMPLE_RATE / freq;
    if (channel == 1) {
        len1 = (samples > BUF_SIZE_1) ? BUF_SIZE_1 : samples;
        for(int i=0; i<len1; i++) delay_line1[i] = noise_gen();
    } else if (channel == 2) {
        len2 = (samples > BUF_SIZE_2) ? BUF_SIZE_2 : samples;
        for(int i=0; i<len2; i++) delay_line2[i] = noise_gen() ^ 0x0F0F;
    } else if (channel == 3) {
        len3 = BUF_SIZE_3;
        for(int i=0; i<len3; i++) delay_line3[i] = noise_gen();
    }

}

bool beginTimer(float rate) {

    uint8_t timer_type = GPT_TIMER;
    int8_t tindex = FspTimer::get_available_timer(timer_type);
    if (tindex==0) { FspTimer::force_use_of_pwm_reserved_timer(); tindex = FspTimer::get_available_timer(timer_type); }
    if(!audio_timer.begin(TIMER_MODE_PERIODIC, timer_type, tindex, rate, 0.0f, timer_callback)) return false;
    audio_timer.setup_overflow_irq();
    audio_timer.open();
    audio_timer.start();
    return true;

}

void setup() {

    analogWriteResolution(12);
    lfsr = random(1, 65535);
    beginTimer(SAMPLE_RATE);

}

void loop() {

    static uint32_t last_step = 999;
    uint32_t step_idx = global_sample_count / STEP_LEN_SAMPLES;

    if(step_idx != last_step) {
        last_step = step_idx;
        uint32_t r = random(0, 65535);

        if(step_idx % 2 == 0) {
            trigger(1, scale_freqs[r % 6] << (r % 2 + 1));
        }

        if((r & 0xF) > 10) {
            trigger(2, scale_freqs[r % 3] << 1);
        }

        if(step_idx % 4 == 0 || (step_idx % 8 == 6 && (r & 1))) {
            trigger(3, 50);
        }
    }

}