#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <DHT.h>
#include <time.h>

// ---------------- PINOS ESP8266 ----------------
// GPIOs reais, não números de pinos do ESP32

#define PIN_TRIG  5   // GPIO5 - D1
#define PIN_ECHO  4   // GPIO4 - D2
#define PIN_DHT   14  // GPIO14 - D5
#define PIN_BUZZ  12  // GPIO12 - D6

#define TIPO_DHT DHT22

// ---------------- SENSORES ----------------
DHT dht(PIN_DHT, TIPO_DHT);

const float VELOCIDADE_SOM_CM_US = 0.0343;
const unsigned long TIMEOUT_ECO_US = 30000;

const float LIMIAR_NIVEL2_CM = 50.0;
const float LIMIAR_NIVEL3_CM = 20.0;

const float LIMIAR_UMIDADE = 70.0;

// ---------------- WIFI E API ----------------
bool ESTADO_WIFI = false;

const char *WIFI_SSID = "SEU_WIFI";
const char *WIFI_SENHA = "SUA_SENHA";

const unsigned long TIMEOUT_WIFI_MS = 15000;

const char *SENSOR_ID = "X0000042";

const char *API_URL =
  "https://ocurriculo.online/api/leitura/sensor/X0000042/NIVEL";

const char *API_KEY = "SUA_API_KEY";

const int TIMEOUT_HTTP_MS = 10000;

// ---------------- DEEP SLEEP ----------------
const uint64_t SLEEP_NIVEL1_S = 5 * 60;
const uint64_t SLEEP_NIVEL2_S = 2 * 60;
const uint64_t SLEEP_NIVEL3_S = 30;

const uint64_t US_POR_SEGUNDO = 1000000ULL;

const unsigned long TEMPO_ESTABILIZACAO_MS = 100;

// ---------------- RTC MEMORY ----------------
// O ESP8266 reinicia após acordar do Deep Sleep.
// A memória RTC preserva o contador entre ciclos.

uint32_t contadorLeituras = 0;

struct RTCData {
  uint32_t contador;
  uint32_t checksum;
};

RTCData dadosRTC;

// ---------------- RTC ----------------
uint32_t calcularChecksum(uint32_t valor) {
  return valor ^ 0xA55AA55A;
}

void carregarContadorRTC() {
  if (ESP.rtcUserMemoryRead(
        0,
        (uint32_t*)&dadosRTC,
        sizeof(dadosRTC))) {

    if (dadosRTC.checksum == calcularChecksum(dadosRTC.contador)) {
      contadorLeituras = dadosRTC.contador;
    } else {
      contadorLeituras = 0;
    }
  } else {
    contadorLeituras = 0;
  }
}

void salvarContadorRTC() {
  dadosRTC.contador = contadorLeituras;
  dadosRTC.checksum = calcularChecksum(contadorLeituras);

  ESP.rtcUserMemoryWrite(
    0,
    (uint32_t*)&dadosRTC,
    sizeof(dadosRTC
  ));
}

// ---------------- ULTRASSOM ----------------
unsigned long medirTempoEco() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(5);

  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  return pulseIn(PIN_ECHO, HIGH, TIMEOUT_ECO_US);
}

float tempoParaDistanciaCm(unsigned long tempoUs) {
  return (tempoUs * VELOCIDADE_SOM_CM_US) / 2.0;
}

int classificarNivel(float distanciaCm) {
  if (distanciaCm <= LIMIAR_NIVEL3_CM) return 3;
  if (distanciaCm <= LIMIAR_NIVEL2_CM) return 2;
  return 1;
}

uint64_t intervaloSleepSegundos(int nivel) {
  switch (nivel) {
    case 3:
      return SLEEP_NIVEL3_S;

    case 2:
      return SLEEP_NIVEL2_S;

    default:
      return SLEEP_NIVEL1_S;
  }
}

// ---------------- TEMPERATURA E UMIDADE ----------------
void mostrarTemperaturaSeUmido() {
  float umidade = dht.readHumidity();
  float temperatura = dht.readTemperature();

  if (isnan(umidade) || isnan(temperatura)) {
    Serial.println("Falha na leitura do DHT22.");
    return;
  }

  Serial.print("Temperatura: ");
  Serial.print(temperatura, 1);
  Serial.println(" C");

  Serial.print("Umidade: ");
  Serial.print(umidade, 1);
  Serial.println(" %");
}

// ---------------- WIFI ----------------
bool conectarWifi() {
  Serial.print("Conectando ao WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);

  unsigned long inicio = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - inicio < TIMEOUT_WIFI_MS) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    ESTADO_WIFI = true;

    Serial.print("WiFi conectado. IP: ");
    Serial.println(WiFi.localIP());

    return true;
  }

  ESTADO_WIFI = false;
  Serial.println("Falha ao conectar no WiFi.");

  return false;
}

void desligarWifi() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  ESTADO_WIFI = false;

  Serial.println("WiFi desligado.");
}

// ---------------- DATA E HORA ----------------
bool obterDataHora(char* destino, size_t tamanho) {
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  setenv("TZ", "BRT3", 1);
  tzset();

  struct tm infoHora;

  if (!getLocalTime(&infoHora, 10000)) {
    Serial.println("Nao foi possivel sincronizar o horario.");
    return false;
  }

  strftime(
    destino,
    tamanho,
    "%Y-%m-%d %H:%M:%S",
    &infoHora
  );

  return true;
}

// ---------------- ENVIO PARA API ----------------
bool enviarLeitura(float distanciaCm, int nivel) {
  char dataHora[24];

  if (!obterDataHora(dataHora, sizeof(dataHora))) {
    Serial.println("Leitura nao enviada: horario indisponivel.");
    return false;
  }

  char json[256];

  snprintf(
    json,
    sizeof(json),
    "{"
      "\"id_sensor\":\"%s\","
      "\"cota_adotada\":\"%.1f\","
      "\"nome_serie\":\"NIVEL\","
      "\"unidade_medida\":\"CM\","
      "\"data_hora\":\"%s\","
      "\"modelo_sensor\":\"HC-SR04\","
      "\"tipo_sensor\":\"ULTRASSOM\""
    "}",
    SENSOR_ID,
    distanciaCm,
    dataHora
  );

  Serial.println("Enviando leitura para a API:");
  Serial.println(json);

  WiFiClientSecure clienteSeguro;
  clienteSeguro.setInsecure();

  HTTPClient http;
  http.setTimeout(TIMEOUT_HTTP_MS);

  if (!http.begin(clienteSeguro, API_URL)) {
    Serial.println("Falha ao iniciar requisicao HTTP.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", API_KEY);

  int codigo = http.POST(
    reinterpret_cast<uint8_t*>(json),
    strlen(json)
  );

  bool sucesso = false;

  if (codigo > 0) {
    Serial.print("Status HTTP: ");
    Serial.println(codigo);

    Serial.println(http.getString());

    sucesso = (codigo >= 200 && codigo < 300);
  } else {
    Serial.print("Erro HTTP: ");
    Serial.println(http.errorToString(codigo);
  }

  http.end();

  return sucesso;
}

// ---------------- DEEP SLEEP ----------------
void entrarEmDeepSleep(uint64_t segundos) {
  Serial.print("Dormindo por ");
  Serial.print((unsigned long)segundos);
  Serial.println(" s...");

  Serial.flush();

  // ESP8266 Deep Sleep
  // GPIO16 (D0) deve ser conectado ao RST
  ESP.deepSleep(segundos * US_POR_SEGUNDO, WAKE_RF_DISABLED);
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("Inicializando ESP8266 ESP-12E...");

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BUZZ, OUTPUT);

  digitalWrite(PIN_TRIG, LOW);
  digitalWrite(PIN_BUZZ, LOW);

  // Inicializa sensor DHT22
  dht.begin();

  // Recupera contador da memoria RTC
  carregarContadorRTC();

  contadorLeituras++;
  salvarContadorRTC();

  Serial.print("Leitura #");
  Serial.println(contadorLeituras);

  delay(TEMPO_ESTABILIZACAO_MS);

  // Leitura temperatura e umidade
  mostrarTemperaturaSeUmido();

  // ---------------- LEITURA DO NIVEL ----------------
  unsigned long tempoUs = medirTempoEco();

  int nivel = 1;
  float distanciaCm = -1;

  if (tempoUs == 0) {
    Serial.println("Sem eco ou erro de leitura.");
  } else {
    distanciaCm = tempoParaDistanciaCm(tempoUs);

    nivel = classificarNivel(distanciaCm);

    Serial.print("Distancia: ");
    Serial.print(distanciaCm, 1);
    Serial.print(" cm | Nivel: ");
    Serial.println(nivel);

    // ---------------- WIFI E ENVIO ----------------
    if (conectarWifi()) {
      bool enviado = enviarLeitura(distanciaCm, nivel);

      if (enviado) {
        Serial.println("Leitura enviada com sucesso.");
      } else {
        Serial.println("Falha ao enviar a leitura.");
      }
    }
  }

  // Desliga WiFi
  desligarWifi();

  // Pequena pausa antes do Deep Sleep
  delay(1000);

  // Dorme conforme nivel
  entrarEmDeepSleep(intervaloSleepSegundos(nivel));
}

void loop() {
}
