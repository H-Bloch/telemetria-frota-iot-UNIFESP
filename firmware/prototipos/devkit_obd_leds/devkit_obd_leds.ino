/*
 * ESP32 DevKit — VELOCÍMETRO OBD2 SÓ COM LEDs + BUZZER + SERIAL
 * ----------------------------------------------------------
 * - Conecta no ELM327 por Bluetooth clássico
 * - Lê a velocidade (PID 01 0D) a cada ~200 ms e mostra no Serial Monitor
 * - LED conexão (GPIO 25): aceso = lendo o carro | piscando = BT ok, carro não responde
 * - LED limite (GPIO 26) + buzzer (GPIO 27): acima de 60 km/h
 * - LED azul da placa (GPIO 2) acompanha o LED de conexão
 *
 * Arduino IDE:
 *   Placa: "ESP32 Dev Module"
 *   Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
 *   Serial Monitor: 115200
 */

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <BluetoothSerial.h>

// ======================= CONFIGURAÇÃO =======================
#define ELM_NOME   "OBDII"     // nome que aparece no pareamento
#define ELM_PIN    "1234"      // PIN comum: 1234 ou 0000

#define USAR_MAC   true        // conecta pelo MAC (mais confiável)
uint8_t elmMac[6] = {0x00, 0x10, 0xCC, 0x4F, 0x36, 0x03};   // MAC do seu OBDII

#define PIN_LED_CONEXAO 25
#define PIN_LED_LIMITE  26
#define PIN_BUZZER      27
#define PIN_LED_PLACA   2

#define LIMITE_KMH      60
#define HISTERESE_KMH   2
#define BIP_ON_MS       150
#define BIP_OFF_MS      350

#define INTERVALO_LEITURA_MS 200
#define FALHAS_PARA_SEM_ECU  10
// ============================================================

BluetoothSerial SerialBT;

// Estado compartilhado com a tarefa dos LEDs
enum Estado { SEM_ELM, SEM_ECU, LENDO };
volatile Estado estado = SEM_ELM;
volatile int velocidade = 0;

bool elmInicializado = false;
int falhasSeguidas = 0;

// ---------- TAREFA DAS SAÍDAS (roda em paralelo) ----------
// Assim o buzzer e o pisca-pisca não travam enquanto espera o ELM responder
void tarefaSaidas(void *) {
  bool acima = false;
  for (;;) {
    Estado e = estado;
    int v = velocidade;
    uint32_t t = millis();

    // LED de conexão
    bool ledCon = (e == LENDO) ? true : (e == SEM_ECU) ? ((t / 400) % 2) : false;
    digitalWrite(PIN_LED_CONEXAO, ledCon);
    digitalWrite(PIN_LED_PLACA, ledCon);

    // Limite com histerese
    if (e != LENDO) acima = false;
    else if (v > LIMITE_KMH) acima = true;
    else if (v <= LIMITE_KMH - HISTERESE_KMH) acima = false;

    digitalWrite(PIN_LED_LIMITE, acima);
    bool bip = acima && ((t % (BIP_ON_MS + BIP_OFF_MS)) < BIP_ON_MS);
    digitalWrite(PIN_BUZZER, bip);

    vTaskDelay(pdMS_TO_TICKS(10));
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
        if (resp.length() == 0) continue;   // prompt atrasado do comando anterior: ignora
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
  delay(1500);                                   // clones demoram pra voltar do reset
  while (SerialBT.available()) SerialBT.read();  // descarta o que sobrou do reset

  // ATSTFF = ELM espera até ~1 s pela ECU antes de dizer NO DATA
  const char *cfg[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSTFF", "ATSP0"};
  for (const char *c : cfg) {
    String r = elmComando(c, 3000);
    Serial.printf("  %-5s -> %s\n", c, r.c_str());
    delay(100);
  }

  String r = elmComando("0100", 15000);   // procura o protocolo do carro
  Serial.println("  0100  -> " + r);
  if (r.indexOf("UNABLE") >= 0 || r.indexOf("NODATA") >= 0)
    Serial.println("  (ELM ok, mas o carro nao respondeu: ignicao ligada? motor ligado?)");
  return r.indexOf("4100") >= 0;
}

int lerVelocidade() {
  String r = elmComando("010D", 4000);   // clone v2.1 é lento: espera até 4 s
  int i = r.indexOf("410D");
  if (i < 0 || (int)r.length() < i + 6) {
    // NODATA é normal nesse clone (~30%): não polui o Serial Monitor
    if (r.indexOf("NODATA") < 0) Serial.println("  010D falhou -> " + r);
    return -1;
  }
  return (int)strtol(r.substring(i + 4, i + 6).c_str(), NULL, 16);
}

bool conectarELM() {
  Serial.println("Conectando ao ELM327 via Bluetooth...");
  bool ok = false;
  if (USAR_MAC) {
    // Tenta 3 jeitos: canal automático, canal 1, canal 1 sem criptografia
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
  Serial.println(ok ? "  Bluetooth conectado!" : "  Falhou. Ignição ligada? Nome/MAC certo?");
  return ok;
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Velocimetro OBD2 (DevKit) ===");

  pinMode(PIN_LED_CONEXAO, OUTPUT);
  pinMode(PIN_LED_LIMITE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_PLACA, OUTPUT);

  // Autoteste: tudo liga por meio segundo
  digitalWrite(PIN_LED_CONEXAO, HIGH);
  digitalWrite(PIN_LED_LIMITE, HIGH);
  digitalWrite(PIN_BUZZER, HIGH);
  delay(500);
  digitalWrite(PIN_LED_CONEXAO, LOW);
  digitalWrite(PIN_LED_LIMITE, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  xTaskCreatePinnedToCore(tarefaSaidas, "saidas", 2048, NULL, 1, NULL, 0);

  SerialBT.begin("ESP32_OBD", true);   // modo master
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  SerialBT.setPin(ELM_PIN, strlen(ELM_PIN));
#else
  SerialBT.setPin(ELM_PIN);
#endif
}

void loop() {
  static uint32_t ultimaLeitura = 0;
  static uint32_t ultimaTentativa = 0;

  if (!SerialBT.connected()) {
    estado = SEM_ELM;
    elmInicializado = false;
    if (millis() - ultimaTentativa > 5000) {
      ultimaTentativa = millis();
      conectarELM();
    }
    delay(100);
    return;
  }

  if (!elmInicializado) {
    estado = SEM_ECU;
    elmInicializado = inicializarELM();
    if (!elmInicializado) {
      Serial.println("  Carro não respondeu. Tentando de novo em 3 s...");
      delay(3000);
      return;
    }
    Serial.println("ELM327 pronto, lendo velocidade.");
    falhasSeguidas = 0;
  }

  if (millis() - ultimaLeitura >= INTERVALO_LEITURA_MS) {
    ultimaLeitura = millis();
    int v = lerVelocidade();
    if (v >= 0) {
      velocidade = v;
      estado = LENDO;
      falhasSeguidas = 0;
      Serial.printf("Velocidade: %3d km/h %s\n", v, v > LIMITE_KMH ? "  <<< ACIMA DO LIMITE" : "");
    } else if (++falhasSeguidas >= FALHAS_PARA_SEM_ECU) {
      estado = SEM_ECU;
      elmInicializado = false;
    }
  }
}
