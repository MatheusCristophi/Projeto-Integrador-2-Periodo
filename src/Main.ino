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

// Retorna o tempo do eco em microssegundos
unsigned long medirTempoEco() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(5);

  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  return pulseIn(PIN_ECHO, HIGH, TIMEOUT_ECO_US);
}

// Converte o tempo do eco (ida e volta) em distância em cm
float tempoParaDistanciaCm(unsigned long tempoUs) {
  return (tempoUs * VELOCIDADE_SOM_CM_US) / 2.0;
}

// Classifica o nível (1, 2 ou 3) a partir da distância até a água
int classificarNivel(float distanciaCm) {
  if (distanciaCm <= LIMIAR_NIVEL3_CM) return 3;
  if (distanciaCm <= LIMIAR_NIVEL2_CM) return 2;
  return 1;
}

// Retorna o intervalo de sleep (segundos) correspondente ao nível
uint64_t intervaloSleepSegundos(int nivel) {
  switch (nivel) {
    case 3:  return SLEEP_NIVEL3_S;
    case 2:  return SLEEP_NIVEL2_S;
    default: return SLEEP_NIVEL1_S;
  }
}

void entrarEmDeepSleep(uint64_t segundos) {
  Serial.print("Dormindo por ");
  Serial.print((unsigned long)segundos);
  Serial.println(" s...");
  Serial.flush();  // garante que o texto saiu antes de dormir

  esp_sleep_enable_timer_wakeup(segundos * US_POR_SEGUNDO);
  esp_deep_sleep_start();  // não retorna: ao acordar, o ESP32 reinicia no setup()
}

