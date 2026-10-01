#include <esp_sleep.h>

#define PIN_TRIG 5
#define PIN_ECHO 18

// Velocidade do som no ar (~20 °C): 343 m/s = 0,0343 cm/µs
const float VELOCIDADE_SOM_CM_US = 0.0343;

// Tempo máximo de espera pelo eco (µs). 30000 µs ≈ 5 m de alcance.
const unsigned long TIMEOUT_ECO_US = 30000;

const float LIMIAR_NIVEL2_CM = 50.0;
const float LIMIAR_NIVEL3_CM = 20.0;

// ---- Intervalo de deep sleep por nível (segundos) ----
const uint64_t SLEEP_NIVEL1_S = 5 * 60;  // Nível 1: 5 minutos
const uint64_t SLEEP_NIVEL2_S = 2 * 60;  // Nível 2: 2 minutos
const uint64_t SLEEP_NIVEL3_S = 30;      // Nível 3: 30 segundos
const uint64_t US_POR_SEGUNDO = 1000000ULL;

// Tempo para o sensor estabilizar após o ESP32 acordar
const unsigned long TEMPO_ESTABILIZACAO_MS = 100;

// Mantem na memória e sobrevive ao deep sleep
RTC_DATA_ATTR uint32_t contadorLeituras = 0;
