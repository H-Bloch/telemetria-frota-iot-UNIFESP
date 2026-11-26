/*
 * DIAGNÓSTICO HC-05 -> ELM327 (plano B, na Heltec do carro)
 * ----------------------------------------------------------
 * Use quando o código 2 não conseguir ler o carro. Ele separa os casos:
 *   - o HC-05 não está conectado no ELM  (problema de pareamento/configuração)
 *   - o HC-05 conecta, mas picota/atrasa os comandos  (problema de envio)
 *   - o ELM responde, mas o carro não  (motor/ignição/protocolo)
 *
 * O que ele faz:
 *   1. Procura o ELM em 115200, 38400 e 9600 baud (HC-05 em modo NORMAL, não AT)
 *   2. Mostra a resposta CRUA de cada comando (com \r, > e tudo), sem filtrar nada
 *   3. Testa ATI 10 vezes seguidas e conta quantas vieram certas
 *   4. Pergunta ao carro: 0100 (PIDs suportados) e 010D (velocidade)
 *   5. Vira um terminal: o que você digitar vai direto pro ELM
 *      (Serial Monitor em 115200 e "Carriage return")
 *
 * ANTES: HC-05 ligado em modo normal (tire e recoloque o VCC SEM segurar o botão),
 * ELM na tomada, motor ligado, celular com Bluetooth desligado.
 * O LED do HC-05 deve dar 2 piscadas a cada 2 s (conectado).
 *
 * Fiação igual à do código 2: HC-05 TXD -> 7, RXD -> 2, VCC -> 5V, GND -> GND
 * Placa: "Heltec WiFi LoRa 32(V3)" | Serial Monitor: 115200
 */

#include <Arduino.h>

#define PIN_RX_HC05  7
#define PIN_TX_HC05  2

long baudUsado = 0;

// Mostra caracteres invisíveis pra dar pra ver o que chegou de verdade
String visivel(const String &s) {
  String o;
  for (char c : s) {
    if (c == '\r')      o += "\\r";
    else if (c == '\n') o += "\\n";
    else if (c < 32)    o += "<" + String((int)c) + ">";
    else                o += c;
  }
  return o;
}

// Envia o comando inteiro de uma vez e espera o '>' (ou o tempo acabar)
String enviar(const char *cmd, uint32_t timeoutMs) {
  while (Serial1.available()) Serial1.read();
  String linha = String(cmd) + "\r";
  Serial1.write((const uint8_t *)linha.c_str(), linha.length());

  String r;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (Serial1.available()) {
      char c = Serial1.read();
      r += c;
      if (c == '>') {
        delay(30);                                    // pega algum resto que venha junto
        while (Serial1.available()) r += (char)Serial1.read();
        return r;
      }
    }
    delay(2);
  }
  return r;
}

String mostrar(const char *cmd, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  String r = enviar(cmd, timeoutMs);
  Serial.printf("  %-6s (%4lu ms) -> %s\n", cmd, (unsigned long)(millis() - t0),
                r.length() ? visivel(r).c_str() : "(nada)");
  return r;
}

bool procurarELM() {
  const long bauds[] = {115200, 38400, 9600};
  for (long b : bauds) {
    Serial1.end();
    delay(50);
    Serial1.begin(b, SERIAL_8N1, PIN_RX_HC05, PIN_TX_HC05);
    delay(200);
    Serial.printf("\nTestando %ld baud...\n", b);
    for (int k = 0; k < 2; k++) {
      String r = mostrar("ATI", 2500);
      String u = r; u.toUpperCase();
      if (u.indexOf("ELM") >= 0) { baudUsado = b; return true; }
      if (r.length() > 0 && k == 0) continue;         // veio lixo: tenta de novo
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Diagnostico HC-05 -> ELM327 ===");

  // 1. Acha o ELM
  if (!procurarELM()) {
    Serial.println("\n[RESULTADO] O ELM327 NAO respondeu em nenhuma velocidade.");
    Serial.println("  Olhe o LED do HC-05:");
    Serial.println("  - Pisca RAPIDO: nao conectou no ELM. Rode o 1_configurar_hc05 de novo");
    Serial.println("    e me mande o log (senha, BIND e PAIR).");
    Serial.println("  - Pisca DEVAGAR: esta no modo AT. Tire e recoloque o VCC SEM segurar o botao.");
    Serial.println("  - 2 piscadas a cada 2 s: conectou, mas a serial nao bate. Confira TXD->7,");
    Serial.println("    RXD->2 e rode o configurador pra conferir o AT+UART.");
    Serial.println("\n(Terminal livre em 115200: o que digitar vai pro HC-05)");
    Serial1.end();
    Serial1.begin(115200, SERIAL_8N1, PIN_RX_HC05, PIN_TX_HC05);
    return;
  }
  Serial.printf("\n>>> ELM327 respondeu em %ld baud.\n", baudUsado);
  if (baudUsado != 115200)
    Serial.printf(">>> O codigo 2 usa 115200: mude o BAUD_HC05 dele para %ld (ou refaca o AT+UART).\n", baudUsado);

  // 2. Reset e configuração, mostrando tudo cru
  Serial.println("\nReset e configuracao:");
  mostrar("ATZ", 5000);
  delay(2000);
  while (Serial1.available()) Serial1.read();
  mostrar("ATE0", 3000);
  mostrar("ATL0", 3000);
  mostrar("ATS0", 3000);
  mostrar("ATH0", 3000);
  mostrar("ATSTFF", 3000);
  mostrar("ATSP0", 3000);

  // 3. Estabilidade: ATI 10x
  Serial.println("\nATI 10 vezes (testa se o HC-05 picota ou atrasa as respostas):");
  int certas = 0;
  for (int i = 0; i < 10; i++) {
    String r = enviar("ATI", 2500);
    String u = r; u.toUpperCase();
    bool ok = u.indexOf("ELM") >= 0 && u.indexOf("?") < 0;
    if (ok) certas++;
    Serial.printf("  %2d: %s %s\n", i + 1, ok ? "ok " : "RUIM", visivel(r).c_str());
    delay(150);
  }
  Serial.printf(">>> %d/10 respostas limpas. ", certas);
  if (certas >= 9)      Serial.println("O HC-05 nao picota: o codigo 2 deve escolher o prefixo 'nenhum'.");
  else if (certas >= 5) Serial.println("Picota as vezes: o codigo 2 deve compensar sozinho.");
  else                  Serial.println("Picota muito: me mande este log.");

  // 4. Carro
  Serial.println("\nPerguntando ao carro (motor ligado):");
  String r = mostrar("0100", 15000);
  String u = r; u.toUpperCase();
  if (u.indexOf("4100") >= 0) {
    Serial.println(">>> O carro respondeu. Lendo a velocidade 5 vezes:");
    for (int i = 0; i < 5; i++) { mostrar("010D", 4000); delay(300); }
    Serial.println("\n[RESULTADO] HC-05, ELM e carro OK. Se o codigo 2 nao funcionar, o problema");
    Serial.println("esta nele: me mande este log junto com o log do codigo 2.");
  } else if (u.indexOf("NODATA") >= 0 || u.indexOf("NO DATA") >= 0 || u.indexOf("UNABLE") >= 0) {
    Serial.println("\n[RESULTADO] O ELM perguntou e a ECU nao respondeu. Motor ligado?");
  } else {
    Serial.println("\n[RESULTADO] Resposta estranha no 0100: me mande este log.");
  }

  Serial.println("\n(Terminal livre: digite comandos pro ELM, ex. 010D, ATRV.");
  Serial.println(" Serial Monitor em 115200 e 'Carriage return')");
}

void loop() {
  // Ponte manual Serial Monitor <-> ELM (via HC-05)
  while (Serial.available())  Serial1.write(Serial.read());
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\r') Serial.write('\n');
    else           Serial.write(c);
  }
}
