# Telemetria de Frota IoT — UNIFESP

Velocímetro OBD2 com alerta de velocidade e telemetria por LoRa, com registro em Google Sheets.

```
ELM327 (OBD2) ──BT clássico──> HC-05 ──UART──> Heltec V3 (carro) ──LoRa 915 MHz──> Heltec V3 (estação) ──Wi-Fi──> Google Sheets
                                                 │ OLED animado                       │ OLED animado
                                                 │ LED azul / LED vermelho / buzzer   │ fila local (LittleFS)
```

## Estrutura

| Pasta | Conteúdo |
|---|---|
| `firmware/no-veiculo/1_configurar_hc05` | Configura o HC-05 como mestre e o pareia com o ELM327. Roda **uma vez** na Heltec do carro. |
| `firmware/no-veiculo/2_heltec_carro_hc05` | Firmware final da Heltec do carro: lê o OBD pelo HC-05, tela, LEDs, buzzer, transmissão LoRa e atualização pelo navegador (OTA). |
| `firmware/gateway/3_heltec_estacao_receptor` | Firmware da estação: recebe por LoRa, guarda numa fila local e envia para o Google Sheets pelo Wi-Fi. Inclui OTA pelo GitHub. |
| `firmware/gateway/versao.txt` | Número da versão publicada para o OTA da estação. |
| `db/4_google_apps_script.gs` | Apps Script que recebe os dados e cria as abas `OBD Leituras` e `OBD Viagens`. |
| `firmware/prototipos/` | Versões anteriores com o ESP32 DevKit (scanner Bluetooth, velocímetro com LEDs e ponte serial). |
| `hardware/pci`, `enclosure`, `node-red` | Ainda não iniciados. |

## Hardware

- ELM327 Bluetooth clássico (clone v2.1), MAC `00:10:CC:4F:36:03`, PIN `1234`
- Módulo HC-05
- 2× Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262 + OLED 128×64), sempre **com a antena LoRa conectada**
- LED azul + resistor de 100 Ω, LED vermelho + resistor de 330 Ω, buzzer ativo

### Fiação da Heltec do carro

| Heltec | Liga em |
|---|---|
| 5V | VCC do HC-05 |
| GND | GND do HC-05, catodos dos LEDs e − do buzzer |
| 7 | TXD do HC-05 |
| 2 | RXD do HC-05 |
| 4 | + do buzzer |
| 5 | resistor de 330 Ω → LED vermelho (acima de 60 km/h) |
| 6 | resistor de 100 Ω → LED azul (conectado ao carro) |

Não ligue nada nos pinos 17, 18, 21 e 36: eles são usados pela tela. O EN e o STATE do HC-05 ficam sem ligação.

A estação usa só a USB e a antena.

## Arduino IDE

- Placa: **Heltec WiFi LoRa 32(V3)**. Serial Monitor em 115200.
- Bibliotecas: **U8g2** (olikraus) e **RadioLib** (jgromes). Os códigos compilam com a RadioLib 7.x e 8.x.
- Mantenha um Partition Scheme com OTA (o padrão da Heltec V3 já tem).

## Como pôr para funcionar

1. **HC-05** (no carro, com a ignição ligada e o ELM na tomada): grave o `1_configurar_hc05`. Para entrar no modo AT, tire o VCC do HC-05, segure o botão dele, recoloque o VCC e solte (o LED pisca devagar). Depois aperte o RST da Heltec. Esse código configura ROLE=1, CMODE=0, BIND com o MAC do ELM, PIN 1234 e UART 115200.
2. **Carro**: grave o `2_heltec_carro_hc05` e reinicie o HC-05 sem segurar o botão. Quando ele conecta ao ELM, o LED dele passa a dar duas piscadas a cada 2 s.
3. **Planilha**: numa planilha nova, abra Extensões → Apps Script, cole o `db/4_google_apps_script.gs` e execute `testar`. Depois vá em Implantar → App da Web (Executar como: Eu, Acesso: Qualquer pessoa) e copie a URL `/exec`.
4. **Estação**: na primeira gravação por USB, preencha `WIFI_SSID`, `WIFI_SENHA` e `URL_SCRIPT` no `3_heltec_estacao_receptor.ino`. A placa salva esses valores na memória. **Não faça commit desses valores**: no repositório eles ficam vazios (`""`), e a placa continua usando os que salvou. O `CHAVE` tem que ser igual nos dois lados (estação e Apps Script).
5. Para testar a planilha sem o carro, aperte o botão PRG da estação ou digite `t` no Serial Monitor. A resposta esperada é `[Sheets] 1 linhas -> HTTP 302 OK`.

## Atualização sem cabo (OTA)

- **Carro**: com o carro parado, segure o PRG por 3 s. A placa cria a rede `OBD-CARRO`. Conecte nela, abra `http://192.168.4.1` (usuário `admin`) e envie o `.ino.bin` gerado em Sketch → Export Compiled Binary. Aumente o `FW_VERSAO` a cada versão nova.
- **Estação**: a cada 5 min a placa lê `firmware/gateway/versao.txt` neste repositório. Se o número for maior que o `FW_VERSAO` dela, ela baixa `firmware/gateway/firmware.bin` e reinicia. Para publicar uma versão: aumente o `FW_VERSAO`, exporte o binário, salve como `firmware/gateway/firmware.bin`, atualize o `versao.txt` com o mesmo número e faça push. Para forçar a checagem, segure o PRG por 3 s ou digite `u`. O repositório precisa ser público para o `raw.githubusercontent.com` funcionar.

## Dados enviados

Pacote LoRa (1 por segundo):
`OBD,<viagem>,<seq>,<estado>,<vel>,<temp>,<rpm>,<dist_m>,<comb_ml>,<vmax>,<ultrap>`.
Estados: 0 = iniciando, 1 = procurando o ELM, 2 = carro não responde, 3 = lendo.

O consumo é **estimado** (speed-density, a partir de MAP, RPM e temperatura do ar, cilindrada de 1,6 L), porque o carro não tem sensor MAF. Para calibrar, ajuste `EFIC_VOLUMETRICA` comparando com o abastecimento real.

## Situação

- ✔ Leitura de velocidade testada no carro (ELM327 → HC-05 → Heltec), com LoRa entre as duas Heltec.
- ✔ Envio estação → Wi-Fi → Google Sheets testado com a linha de teste.
- ⏳ Falta testar no carro o HC-05 novo (firmware `hc05V2.3_le`), os dados reais chegando na planilha e o fechamento de viagem.
- ⏳ Falta testar o OTA nas placas.
- ⏳ PCI e gabinete.
