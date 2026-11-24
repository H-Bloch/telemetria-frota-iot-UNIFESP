/*
 * ESP32 DevKit — PONTE ELM327 -> ESP32-S3 (SERIAL COM FIO)
 * ----------------------------------------------------------
 * - Conecta no ELM327 (Bluetooth clássico) pelo MAC
 * - Lê a velocidade (PID 01 0D)
 * - Envia pro S3 pela Serial2 a cada 100 ms, em texto:  V,<bt>,<ecu>,<vel>\n
 *     ex.: "V,1,1,62"  = BT conectado, carro respondendo, 62 km/h
 * - LED azul da placa (GPIO 2): aceso = lendo o carro | piscando = BT ok, carro não responde
 *
 * LIGAÇÃO:
 *   DevKit GPIO 17 (TX2)  --->  S3 GPIO 18 (RX)
 *   DevKit GND            ----  S3 GND          (obrigatório)
 *
 * Arduino IDE:
 *   Placa: "ESP32 Dev Module"
 *   Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
 *   Upload Speed: 115200
 *   Serial Monitor: 115200
 */

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <BluetoothSerial.h>

// ======================= CONFIGURAÇÃO =======================
#define ELM_NOME   "OBDII"
#define ELM_PIN    "1234"
#define USAR_MAC   true
uint8_t elmMac[6] = {0x00, 0x10, 0xCC, 0x4F, 0x36, 0x03};   // MAC do seu OBDII

#define PIN_TX_S3            17   // TX2 -> RX do S3
#define PIN_RX_S3            16   // RX2 (não usado, mas a Serial2 precisa de um pino)
#define BAUD_S3              115200

#define PIN_LED_PLACA        2
#define INTERVALO_LEITURA_MS 200
#define INTERVALO_ENVIO_MS   100
#define FALHAS_PARA_SEM_ECU  10
// ============================================================

BluetoothSerial SerialBT;

// Estado atual (escrito pelo loop, lido pela tarefa de envio)
volatile uint8_t stBt  = 0;
volatile uint8_t stEcu = 0;
volatile uint8_t stVel = 0;

bool elmInicializado = false;
int  falhasSeguidas  = 0;

// ---------- TAREFA DE ENVIO (roda em paralelo) ----------
// Continua mandando status pro S3 mesmo quando o loop está travado
// esperando o ELM327 (connect e 0100 podem levar vários segundos)
void tarefaEnvio(void *) {
  char linha[24];
  for (;;) {
    snprintf(linha, sizeof(linha), "V,%u,%u,%u\n", stBt, stEcu, stVel);
    Serial2.print(linha);

    bool led = stEcu ? true : (stBt ? ((millis() / 400) % 2) : false);
    digitalWrite(PIN_LED_PLACA, led);

    vTaskDelay(pdMS_TO_TICKS(INTERVALO_ENVIO_MS));
  }
}

// ---------- ELM327 ----------
String elmComando(const char *cmd, uint32_t timeoutMs = 2000) {
  while (SerialBT.available()) SerialBT.read();
  String linha = String(cmd) + "\r";            // comando + \r num pacote só
  SerialBT.write((const uint8_t *)linha.c_str(), linha.length());

  String resp;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (SerialBT.available()) {
      char c = SerialBT.read();
      if (c == '>') {
        if (resp.length() == 0) continue;   // prompt atrasado do comando anterior
        resp.toUpperCase();
        return resp;
      }
      if (c != '\r' && c != '\n' && c != ' ' && c != '\0') resp += c;
    }
    delay(2);
  }
  resp.toUpperCase();
  return resp;
}

bool inicializarELM() {
  Serial.println("Inicializando ELM327...");
  Serial.println("  ATZ   -> " + elmComando("ATZ", 5000));
  delay(1500);
  while (SerialBT.available()) SerialBT.read();

  const char *cfg[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSTFF", "ATSP0"};
  for (const char *c : cfg) {
    String r = elmComando(c, 3000);
    Serial.printf("  %-5s -> %s\n", c, r.c_str());
    delay(100);
  }

  String r = elmComando("0100", 15000);
  Serial.println("  0100  -> " + r);
  if (r.indexOf("UNABLE") >= 0 || r.indexOf("NODATA") >= 0)
    Serial.println("  (ELM ok, mas o carro nao respondeu: ignicao ligada?)");
  return r.indexOf("4100") >= 0;
}

int lerVelocidade() {
  String r = elmComando("010D", 4000);
  int i = r.indexOf("410D");
  if (i < 0 || (int)r.length() < i + 6) {
    if (r.indexOf("NODATA") < 0) Serial.println("  010D falhou -> " + r);
    return -1;
  }
  return (int)strtol(r.substring(i + 4, i + 6).c_str(), NULL, 16);
}

bool conectarELM() {
  Serial.println("Conectando ao ELM327 via Bluetooth...");
  bool ok = false;
  if (USAR_MAC) {
    Serial.println("  tentativa 1: MAC, canal automatico");
    ok = SerialBT.connect(elmMac);
    if (!ok) {
      Serial.println("  tentativa 2: MAC, canal 1");
      ok = SerialBT.connect(elmMac, 1);
    }
    if (!ok) {
      Serial.println("  tentativa 3: MAC, canal 1, sem seguranca");
      ok = SerialBT.connect(elmMac, 1, ESP_SPP_SEC_NONE, ESP_SPP_ROLE_MASTER);
    }
  } else {
    ok = SerialBT.connect(ELM_NOME);
  }
  Serial.println(ok ? "  Bluetooth conectado!" : "  Falhou. Ignição ligada?");
  return ok;
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Ponte ELM327 -> ESP32-S3 (serial) ===");

  pinMode(PIN_LED_PLACA, OUTPUT);

  Serial2.begin(BAUD_S3, SERIAL_8N1, PIN_RX_S3, PIN_TX_S3);
  xTaskCreatePinnedToCore(tarefaEnvio, "envio", 3072, NULL, 1, NULL, 0);

  SerialBT.begin("ESP32_OBD", true);   // modo master
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  SerialBT.setPin(ELM_PIN, strlen(ELM_PIN));
#else
  SerialBT.setPin(ELM_PIN);
#endif
}

void loop() {
  static uint32_t ultimaLeitura   = 0;
  static uint32_t ultimaTentativa = 0;

  if (!SerialBT.connected()) {
    stBt = 0; stEcu = 0; stVel = 0;
    elmInicializado = false;
    if (millis() - ultimaTentativa > 5000) {
      ultimaTentativa = millis();
      if (conectarELM()) stBt = 1;
    }
    delay(100);
    return;
  }

  stBt = 1;

  if (!elmInicializado) {
    stEcu = 0;
    elmInicializado = inicializarELM();
    if (!elmInicializado) {
      Serial.println("  Carro não respondeu. Tentando de novo em 3 s...");
      delay(3000);
      return;
    }
    Serial.println("ELM327 pronto, enviando velocidade pro S3.");
    falhasSeguidas = 0;
  }

  if (millis() - ultimaLeitura >= INTERVALO_LEITURA_MS) {
    ultimaLeitura = millis();
    int v = lerVelocidade();
    if (v >= 0) {
      stVel = (uint8_t)v;
      stEcu = 1;
      falhasSeguidas = 0;
      Serial.printf("Velocidade: %3d km/h\n", v);
    } else if (++falhasSeguidas >= FALHAS_PARA_SEM_ECU) {
      stEcu = 0;
      elmInicializado = false;
    }
  }
}
