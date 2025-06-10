#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "inc/ssd1306.h"

// ====================== Configurações Gerais ======================
#define RED_LED_PIN       13
#define GREEN_LED_PIN     11
#define BLUE_LED_PIN      12
#define BUTTON_A_PIN      5
#define BUTTON_B_PIN      6

#define BUZZER_PIN        21
#define I2C_SDA           14
#define I2C_SCL           15

// --- Configurações do Microfone e Gravação ---adaw

#define MIC_PIN           28
#define MIC_ADC_CHANNEL   2
#define SAMPLING_RATE     8000
#define MAX_RECORDING_SECS 10
#define BUFFER_SIZE       (SAMPLING_RATE * MAX_RECORDING_SECS)

// --- Configurações do Joystick ---
#define JOYSTICK_X_PIN    27
#define JOYSTICK_Y_PIN    26
#define JOYSTICK_X_ADC_CH 1
#define JOYSTICK_Y_ADC_CH 0
#define ADC_THRESHOLD     1000

// --- Fator de Amplificação Digital ---
#define GAIN              2.0f

// ====================== Estruturas e Variáveis Globais ======================
typedef enum { STATE_IDLE, STATE_RECORDING, STATE_PLAYBACK } system_state_t;
volatile system_state_t current_state = STATE_IDLE;

volatile uint8_t recording_secs = 3;
volatile float gain_factor = 2.0f;

uint16_t audio_buffer[BUFFER_SIZE];
volatile uint32_t samples_recorded = 0;

uint dma_channel;
dma_channel_config dma_cfg;

// ====================== Funções Auxiliares ======================
void pwm_init_buzzer(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 4.f);
    pwm_init(slice_num, &config, true);
    pwm_set_gpio_level(pin, 0);
}

void play_tone(uint pin, uint frequency, uint duration_ms) {
    uint slice_num = pwm_gpio_to_slice_num(pin);
    if (frequency == 0) {
        pwm_set_gpio_level(pin, 0);
        sleep_ms(duration_ms);
        return;
    }
    uint32_t clock_freq = clock_get_hz(clk_sys);
    uint32_t top = clock_freq / frequency - 1;
    pwm_set_wrap(slice_num, top);
    pwm_set_gpio_level(pin, top / 2);
    sleep_ms(duration_ms);
    pwm_set_gpio_level(pin, 0);
}

void update_oled_display(char* line1, char* line2, struct render_area *area, uint8_t *buffer) {
    memset(buffer, 0, ssd1306_buffer_length);
    if (line1) ssd1306_draw_string(buffer, 0, 0, line1);
    if (line2) ssd1306_draw_string(buffer, 0, 16, line2);
    render_on_display(buffer, area);
}

// ====================== Lógica dos Estados e Controles ======================

void handle_joystick_controls(bool *x_moved, bool *y_moved, bool *display_update_flag) {
    adc_select_input(JOYSTICK_Y_ADC_CH);
    uint16_t adc_y = adc_read();
    if (adc_y < ADC_THRESHOLD && !*y_moved) {
        if (gain_factor > 2.0f) {
            gain_factor /= 2.0f;
            *display_update_flag = true;
            play_tone(BUZZER_PIN, 400, 30); // BIP de feedback
        }
        *y_moved = true;
    } else if (adc_y > (4095 - ADC_THRESHOLD) && !*y_moved) {
        if (gain_factor < 16.0f) {
            gain_factor *= 2.0f;
            *display_update_flag = true;
            play_tone(BUZZER_PIN, 400, 30); // BIP de feedback
        }
        *y_moved = true;
    } else if (adc_y >= ADC_THRESHOLD && adc_y <= (4095 - ADC_THRESHOLD)) {
        *y_moved = false;
    }

    adc_select_input(JOYSTICK_X_ADC_CH);
    uint16_t adc_x = adc_read();
    if (adc_x < ADC_THRESHOLD && !*x_moved) {
        if (recording_secs > 3) {
            recording_secs--;
            *display_update_flag = true;
            play_tone(BUZZER_PIN, 700, 30); // BIP de feedback
        }
        *x_moved = true;
    } else if (adc_x > (4095 - ADC_THRESHOLD) && !*x_moved) {
        if (recording_secs < MAX_RECORDING_SECS) {
            recording_secs++;
            *display_update_flag = true;
            play_tone(BUZZER_PIN, 700, 30); // BIP de feedback
        }
        *x_moved = true;
    } else if (adc_x >= ADC_THRESHOLD && adc_x <= (4095 - ADC_THRESHOLD)) {
        *x_moved = false;
    }
}

void handle_idle_state(bool *btn_a_pressed, bool *btn_b_pressed, bool *x_moved, bool *y_moved, bool *display_needs_update, struct render_area *area, uint8_t *buffer) {
    // BUG FIX: Lógica para resetar os botões ao entrar no estado IDLE
    static bool first_entry = true;
    if (first_entry) {
        *btn_a_pressed = false;
        *btn_b_pressed = false;
        first_entry = false;
    }
    
    gpio_put(RED_LED_PIN, 1);
    gpio_put(GREEN_LED_PIN, 0);
    gpio_put(BLUE_LED_PIN, 0);

    handle_joystick_controls(x_moved, y_moved, display_needs_update);

    if (*display_needs_update) {
        char line1[20], line2[20];
        sprintf(line1, "Tempo: %d s", recording_secs);
        sprintf(line2, "Ganho: x%.1f", gain_factor);
        update_oled_display(line1, line2, area, buffer);
        *display_needs_update = false;
    }

    if (!gpio_get(BUTTON_A_PIN) && !*btn_a_pressed) {
        *btn_a_pressed = true;
        first_entry = true; // Prepara para o próximo retorno ao IDLE
        current_state = STATE_RECORDING;
        *display_needs_update = true;
        play_tone(BUZZER_PIN, 800, 100);
    }
    if (!gpio_get(BUTTON_B_PIN) && !*btn_b_pressed) {
        *btn_b_pressed = true;
        if (samples_recorded > 0) {
            first_entry = true; // Prepara para o próximo retorno ao IDLE
            current_state = STATE_PLAYBACK;
            *display_needs_update = true;
            play_tone(BUZZER_PIN, 600, 100);
        } else {
            char err_ln1[] = "ERRO:";
            char err_ln2[] = "NADA GRAVADO!";
            update_oled_display(err_ln1, err_ln2, area, buffer);
            sleep_ms(2000);
            *display_needs_update = true;
        }
    }
}

void handle_recording_state(bool *display_needs_update, struct render_area *area, uint8_t *buffer) {
    gpio_put(RED_LED_PIN, 0);
    gpio_put(GREEN_LED_PIN, 0);
    gpio_put(BLUE_LED_PIN, 1);
    if(*display_needs_update) {
        char line1[] = "ESTADO: GRAVANDO";
        char line2[] = "Fale agora...";
        update_oled_display(line1, line2, area, buffer);
        *display_needs_update = false;
    }

    adc_fifo_setup(true, true, 1, false, false);
    adc_set_clkdiv(clock_get_hz(clk_adc) / SAMPLING_RATE);
    adc_select_input(MIC_ADC_CHANNEL);

    uint32_t samples_to_record = SAMPLING_RATE * recording_secs;
    dma_channel_configure(dma_channel, &dma_cfg, audio_buffer, &adc_hw->fifo, samples_to_record, true);

    adc_run(true);
    dma_channel_wait_for_finish_blocking(dma_channel);
    samples_recorded = samples_to_record;
    adc_run(false);
    
    adc_fifo_drain();
    adc_fifo_setup(false, false, 1, false, false);
    adc_set_clkdiv(0);

    char line1[] = "Gravacao";
    char line2[] = "Concluida!";
    update_oled_display(line1, line2, area, buffer);
    sleep_ms(1500);
    current_state = STATE_IDLE;
    *display_needs_update = true;
}

void handle_playback_state(bool *display_needs_update, struct render_area *area, uint8_t *buffer) {
    gpio_put(RED_LED_PIN, 0);
    gpio_put(GREEN_LED_PIN, 1);
    gpio_put(BLUE_LED_PIN, 0);
    if(*display_needs_update){
        char line1[] = "ESTADO: REPRODUZ.";
        char line2[] = "Tocando audio...";
        update_oled_display(line1, line2, area, buffer);
        *display_needs_update = false;
    }

    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_wrap(slice_num, (1 << 12) - 1);
    pwm_set_clkdiv(slice_num, 1.0f);
    pwm_set_enabled(slice_num, true);

    for (uint32_t i = 0; i < samples_recorded; i++) {
        int32_t sample = audio_buffer[i];
        sample -= 2048;
        sample *= gain_factor;
        sample += 2048;
        if (sample > 4095) sample = 4095;
        if (sample < 0) sample = 0;
        pwm_set_gpio_level(BUZZER_PIN, (uint16_t)sample);
        sleep_us(1000000 / SAMPLING_RATE);
    }
    
    pwm_set_enabled(slice_num, false);
    current_state = STATE_IDLE;
    *display_needs_update = true;
}

// ====================== Função Principal ======================
int main() {
    stdio_init_all();

    gpio_init(RED_LED_PIN);   gpio_set_dir(RED_LED_PIN, GPIO_OUT);
    gpio_init(GREEN_LED_PIN); gpio_set_dir(GREEN_LED_PIN, GPIO_OUT);
    gpio_init(BLUE_LED_PIN);  gpio_set_dir(BLUE_LED_PIN, GPIO_OUT);
    gpio_init(BUTTON_A_PIN);  gpio_set_dir(BUTTON_A_PIN, GPIO_IN); gpio_pull_up(BUTTON_A_PIN);
    gpio_init(BUTTON_B_PIN);  gpio_set_dir(BUTTON_B_PIN, GPIO_IN); gpio_pull_up(BUTTON_B_PIN);
    
    i2c_init(i2c1, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init();
    pwm_init_buzzer(BUZZER_PIN);

    adc_init();
    adc_gpio_init(MIC_PIN);
    adc_gpio_init(JOYSTICK_X_PIN);
    adc_gpio_init(JOYSTICK_Y_PIN);
    
    dma_channel = dma_claim_unused_channel(true);
    dma_cfg = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_dreq(&dma_cfg, DREQ_ADC);

    struct render_area frame_area;
    frame_area.start_column = 0;
    frame_area.end_column   = ssd1306_width - 1;
    frame_area.start_page   = 0;
    frame_area.end_page     = ssd1306_n_pages - 1;
    calculate_render_area_buffer_length(&frame_area);
    uint8_t oled_buffer[ssd1306_buffer_length];

    bool btn_a_pressed = false;
    bool btn_b_pressed = false;
    bool x_moved = false;
    bool y_moved = false;
    bool display_needs_update = true;

    while (true) {
        switch (current_state) {
            case STATE_IDLE:
                handle_idle_state(&btn_a_pressed, &btn_b_pressed, &x_moved, &y_moved, &display_needs_update, &frame_area, oled_buffer);
                break;
            case STATE_RECORDING:
                handle_recording_state(&display_needs_update, &frame_area, oled_buffer);
                break;
            case STATE_PLAYBACK:
                handle_playback_state(&display_needs_update, &frame_area, oled_buffer);
                break;
        }
        sleep_ms(20);
    }
    return 0;
}