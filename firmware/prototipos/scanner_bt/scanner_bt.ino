/*
 * SCANNER BLUETOOTH CLÁSSICO — descobre o nome e o MAC do ELM327
 * Arduino IDE: ESP32 Dev Module | Partition: Huge APP | Serial 115200
 * Com a ignição ligada e o Bluetooth do celular DESLIGADO.
 */
#include <BluetoothSerial.h>

BluetoothSerial SerialBT;

void escanear() {
  Serial.println("\nProcurando dispositivos Bluetooth classico por 10 s...");
  BTScanResults *r = SerialBT.discover(10000);
  if (!r || r->getCount() == 0) {
    Serial.println("Nada encontrado.");
    Serial.println("-> Se o celular enxerga o OBDII mas aqui nao aparece,");
    Serial.println("   seu ELM327 provavelmente e BLE (Bluetooth Low Energy).");
    return;
  }
  for (int i = 0; i < r->getCount(); i++) {
    BTAdvertisedDevice *d = r->getDevice(i);
    BTAddress a = d->getAddress();
    const uint8_t *m = *a.getNative();
    Serial.printf("\n[%d] Nome: \"%s\"  RSSI: %d\n", i,
                  d->haveName() ? d->getName().c_str() : "(sem nome)",
                  d->haveRSSI() ? d->getRSSI() : 0);
    Serial.printf("    MAC: %s\n", a.toString().c_str());
    Serial.printf("    Cole no codigo: uint8_t elmMac[6] = {0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X};\n",
                  m[0], m[1], m[2], m[3], m[4], m[5]);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  SerialBT.begin("ESP32_SCAN", true);
  escanear();
  Serial.println("\nAperte EN para escanear de novo.");
}

void loop() {}
