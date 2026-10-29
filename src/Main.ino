
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_sleep.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <time.h>

// ---------------- PINOS ----------------
#define PIN_TRIG 5
#define PIN_ECHO 18
#define PIN_DHT 4
#define PIN_LIGADO_DESLIGADO 14

#define TIPO_DHT DHT22

// ---------------- SENSORES ----------------
DHT dht(PIN_DHT, TIPO_DHT);

// Endereco I2C comum: 0x27
LiquidCrystal_I2C lcd(0x27, 16, 2);

const float VELOCIDADE_SOM_CM_US = 0.0343;
const unsigned long TIMEOUT_ECO_US = 30000;

const float LIMIAR_NIVEL2_CM = 50.0;
const float LIMIAR_NIVEL3_CM = 20.0;

// Limite de umidade relativa para ligar o LCD
const float LIMIAR_UMIDADE = 70.0;

// ---------------- WIFI E API ----------------
bool ESTADO_WIFI = false;

const char *WIFI_SSID = "Wifi_Nome";
const char *WIFI_SENHA = "Senha_Wifi";
const unsigned long TIMEOUT_WIFI_MS = 15000;

const char *SENSOR_ID = "X0000042";
const char *API_URL =
  "https://ocurriculo.online/api/leitura/sensor/X0000042/NIVEL";
const char *API_KEY = "SUA_CHAVE_AQUI";

const int TIMEOUT_HTTP_MS = 10000;

// ---------------- DEEP SLEEP ----------------
const uint64_t SLEEP_NIVEL1_S = 5 * 60;
const uint64_t SLEEP_NIVEL2_S = 2 * 60;
const uint64_t SLEEP_NIVEL3_S = 30;
const uint64_t US_POR_SEGUNDO = 1000000ULL;

const unsigned long TEMPO_ESTABILIZACAO_MS = 100;

RTC_DATA_ATTR uint32_t contadorLeituras = 0;

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
    case 3: return SLEEP_NIVEL3_S;
    case 2: return SLEEP_NIVEL2_S;
    default: return SLEEP_NIVEL1_S;
  }
}

// ---------------- LCD E UMIDADE ----------------
void mostrarTemperaturaSeUmido() {
  float umidade = dht.readHumidity();
  float temperatura = dht.readTemperature();

  if (isnan(umidade) || isnan(temperatura)) {
    Serial.println("Erro ao ler o DHT22.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro no sensor");
    lcd.setCursor(0, 1);
    lcd.print("DHT22");
    return;
  }

  Serial.print("Temperatura: ");
  Serial.print(temperatura, 1);
  Serial.println(" C");

  Serial.print("Umidade: ");
  Serial.print(umidade, 1);
  Serial.println(" %");

  if (umidade >= LIMIAR_UMIDADE) {
    lcd.backlight();
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("Umidade alta!");

    lcd.setCursor(0, 1);
    lcd.print("Temp: ");
    lcd.print(temperatura, 1);
    lcd.print((char)223);
    lcd.print("C");
  } else {
    // Umidade abaixo do limite: apaga a iluminacao.
    lcd.clear();
    lcd.noBacklight();
  }
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

    digitalWrite(PIN_LIGADO_DESLIGADO, LOW);
    return true;
  }

  ESTADO_WIFI = false;
  Serial.println("Falha ao conectar no WiFi.");
  return false;
}

void desligarWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  ESTADO_WIFI = false;
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

  strftime(destino, tamanho, "%Y-%m-%d %H:%M:%S",
           &infoHora);

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
    Serial.println(HTTPClient::errorToString(codigo));
  }

  http.end();
  return sucesso;
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LIGADO_DESLIGADO, OUTPUT);

  Wire.begin(21, 22);
  lcd.init();
  lcd.noBacklight();

  dht.begin();

  contadorLeituras++;
  Serial.print("Leitura #");
  Serial.println(contadorLeituras);

  delay(TEMPO_ESTABILIZACAO_MS);

  // Leitura de temperatura e umidade
  mostrarTemperaturaSeUmido();

  // Leitura do nivel da agua
  unsigned long tempoUs = medirTempoEco();
  int nivel = 1;

  if (tempoUs == 0) {
    Serial.println("Sem eco ou erro de leitura.");
  } else {
    float distanciaCm = tempoParaDistanciaCm(tempoUs);
    nivel = classificarNivel(distanciaCm);

    Serial.print("Distancia: ");
    Serial.print(distanciaCm, 1);
    Serial.print(" cm | Nivel: ");
    Serial.println(nivel);

    if (conectarWifi()) {
      bool enviado = enviarLeitura(distanciaCm, nivel);

      if (enviado) {
        Serial.println("Leitura enviada com sucesso.");
      } else {
        Serial.println("Falha ao enviar a leitura.");
      }
    }
  }

  desligarWifi();

  // Permite visualizar o LCD antes do deep sleep.
  delay(5000);

  lcd.noBacklight();

  entrarEmDeepSleep(intervaloSleepSegundos(nivel));
}

void entrarEmDeepSleep(uint64_t segundos) {
  Serial.print("Dormindo por ");
  Serial.print((unsigned long)segundos);
  Serial.println(" s...");

  Serial.flush();

  esp_sleep_enable_timer_wakeup(segundos * US_POR_SEGUNDO);
  esp_deep_sleep_start();
}

void loop() {
}
