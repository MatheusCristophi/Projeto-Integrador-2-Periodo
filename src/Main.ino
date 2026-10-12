#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPClienteSecure.h>
#include <esp_sleep.h>

#define PIN_TRIG 5
#define PIN_ECHO 18
#define PIN_LIGADO_DESLIGADO 14
#define ESTADO_WIFI false

// Velocidade do som no ar (~20 °C): 343 m/s = 0,0343 cm/µs
const float VELOCIDADE_SOM_CM_US = 0.0343;

// Tempo máximo de espera pelo eco (µs). 30000 µs ≈ 5 m de alcance.
const unsigned long TIMEOUT_ECO_US = 30000;

const float LIMIAR_NIVEL2_CM = 50.0;
const float LIMIAR_NIVEL3_CM = 20.0;

// ---- Varáveis para realizar a conexão por Wifi ----
const char *WIFI_SSID = "Wifi_Nome";
const char *WIFI_SENHA = "Senha_Wifi";
const unsigned long TIMEOUT_WIFI_MS = 15000;

// ---- Váriaveis para realizar a conexão com a api ----
// ---- /online/educacional / fagoc ----
const char *SENSOR_ID = "X0000042";
const char *API_URL = "https://ocurriculo.online/api/leitura/sensor/X0000042/NIVEL"; // placeholder
const char *API_KEY = "SUA_CHAVE_AQUI";
const int TIMEOUT_HTTP_MS = 10000;

// ---- Intervalo de deep sleep por nível (segundos) ----
const uint64_t SLEEP_NIVEL1_S = 5 * 60; // Nível 1: 5 minutos
const uint64_t SLEEP_NIVEL2_S = 2 * 60; // Nível 2: 2 minutos
const uint64_t SLEEP_NIVEL3_S = 30;     // Nível 3: 30 segundos
const uint64_t US_POR_SEGUNDO = 1000000ULL;

// Tempo para o sensor estabilizar após o ESP32 acordar
const unsigned long TEMPO_ESTABILIZACAO_MS = 100;

// Mantem na memória e sobrevive ao deep sleep
RTC_DATA_ATTR uint32_t contadorLeituras = 0;

// Retorna o tempo do eco em microssegundos
unsigned long medirTempoEco()
{
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(5);

    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    return pulseIn(PIN_ECHO, HIGH, TIMEOUT_ECO_US);
}

// Converte o tempo do eco (ida e volta) em distância em cm
float tempoParaDistanciaCm(unsigned long tempoUs)
{
    return (tempoUs * VELOCIDADE_SOM_CM_US) / 2.0;
}

// Classifica o nível (1, 2 ou 3) a partir da distância até a água
int classificarNivel(float distanciaCm)
{
    if (distanciaCm <= LIMIAR_NIVEL3_CM)
        return 3;
    if (distanciaCm <= LIMIAR_NIVEL2_CM)
        return 2;
    return 1;
}

// Retorna o intervalo de sleep (segundos) correspondente ao nível
uint64_t intervaloSleepSegundos(int nivel)
{
    switch (nivel)
    {
    case 3:
        return SLEEP_NIVEL3_S;
    case 2:
        return SLEEP_NIVEL2_S;
    default:
        return SLEEP_NIVEL1_S;
    }
}

void entrarEmDeepSleep(uint64_t segundos)
{
    Serial.print("Dormindo por ");
    Serial.print((unsigned long)segundos);
    Serial.println(" s...");
    Serial.flush(); // garante que o texto saiu antes de dormir

    esp_sleep_enable_timer_wakeup(segundos * US_POR_SEGUNDO);
    esp_deep_sleep_start(); // não retorna: ao acordar, o ESP32 reinicia no setup()
}

bool conectarWifi()
{
    Serial.print("Conectando ao WiFi");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_SENHA);

    unsigned long inicio = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - inicio < TIMEOUT_WIFI_MS)
    {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        ESTADO_WIFI = true;

        Serial.print("WiFi conectado. IP: ");
        Serial.println(WiFi.localIP());

        digitalWrite(PIN_LIGADO_DESLIGADO, LOW)
        return true;
    }

    ESTADO_WIFI = false;

    Serial.println("Falha ao conectar no WiFi.");
    return false;


}

void desligarWifi()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    ESTADO_WIFI = false;
}

bool obterDataHora(char* destino, size_t tamanho)
{
    // Horário de Brasília: UTC-3
    configTime(
        0,
        0,
        "pool.ntp.org",
        "time.google.com"
    );

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
        Serial.println("Falha ao iniciar a requisicao HTTP.");
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", API_KEY)

    int codigo = http.POST(
        reinterpret_cast<uint8_t*>(json),
        strlen(json)
    );

    bool sucesso = false;

    if (codigo > 0) {
        Serial.print("Status HTTP: ");
        Serial.println(codigo);

        String resposta = http.getString();

        Serial.print("Resposta: ");
        Serial.println(resposta);

        sucesso = (codigo >= 200 && codigo < 300);
    } else {
        Serial.print("Erro HTTP: ");
        Serial.println(HTTPClient::errorToString(codigo));
    }

    http.end();

    return sucesso;
}

void setup(){
    Serial.begin(115200);
    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);

    contadorLeituras++;
    Serial.print("Leitura #");
    Serial.println(contadorLeituras);

    delay(TEMPO_ESTABILIZACAO_MS);

    unsigned long tempoUs = medirTempoEco();
    int nivel = 1; // sem eco: mantém o intervalo normal

    if (tempoUs == 0)
    {
        Serial.println("Sem eco (fora de alcance ou erro de leitura)");
    }
    else
    {
        float distanciaCm = tempoParaDistanciaCm(tempoUs);
        nivel = classificarNivel(distanciaCm);

        Serial.print("Tempo: ");
        Serial.print(tempoUs);
        Serial.print(" us | Distancia: ");
        Serial.print(distanciaCm, 1);
        Serial.print(" cm | Nivel: ");
        Serial.println(nivel);

        pinMode(PIN_LIGADO_DESLIGADO, OUTPUT);

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

    entrarEmDeepSleep(intervaloSleepSegundos(nivel));
}

void loop(){

}