# EnviroSenseCube ESPHome

Firmware ESPHome per il monitor ambientale a batteria EnviroSenseCube, basato
su ESP8266, BME688, BSEC2, fuel gauge LC709203F e display e-paper da 0,97".

Questa implementazione usa la Native API di ESPHome: Home Assistant crea
direttamente le entità del dispositivo, senza webhook, MQTT, `input_text` o
sensori template.

## Correzione BSEC2 del 25 ottobre 2026

Il tempo BSEC2 ora include il deep sleep tramite un riferimento a 64 bit in
RTC, associato all'hash della calibrazione. Il risveglio segue `next_call` di
Bosch. Il formato dei salvataggi esistenti e i driver del display non cambiano.
La prima osservazione a OTA spento sul dispositivo bianco/nero è positiva;
la stabilità a lungo termine e sul tricolore deve ancora essere verificata.

Il wrapper BSD locale è documentato in
[`BSEC_WRAPPER_NOTICE.md`](components/envirosense_cube/BSEC_WRAPPER_NOTICE.md).
Test dell'orologio: `clang++ -std=c++11 -I. tests/bsec_clock_test.cpp -o /tmp/bsec-clock-test`
e poi `/tmp/bsec-clock-test`.

## Hardware

| Componente | Configurazione |
| --- | --- |
| MCU | Adafruit HUZZAH ESP8266 |
| Sensore ambientale | Bosch BME688, I2C `0x77` |
| Fuel gauge | Adafruit LC709203F, I2C `0x0B` |
| Display | Good Display GDEW0097T50, 184×88 px |
| Deep-sleep wake | GPIO16 collegato a RST |

Pin principali:

| Funzione | GPIO |
| --- | ---: |
| I2C SDA | 4 |
| I2C SCL | 5 |
| E-paper BUSY | 12 |
| E-paper RST | 0 |
| E-paper DC | 15 |
| E-paper CS / LED blu | 2 |

## Comportamento corrente

- Deep sleep di 300 secondi.
- Un ciclo completo BSEC2 ULP ogni 5 minuti; temperatura, umidità, pressione,
  gas e IAQ provengono dallo stesso ciclo.
- Wi-Fi normalmente disabilitato e attivato solo quando una soglia richiede
  la pubblicazione o scade l'heartbeat di 30 minuti.
- Nessun nuovo tentativo forzato al risveglio successivo dopo un invio fallito.
- Finestra OTA/API di emergenza di 20 secondi soltanto dopo un'accensione a
  freddo.
- Modalità manutenzione OTA persistente, comandata da Home Assistant: mantiene
  il dispositivo online e continua i cicli BSEC2 ogni 5 minuti.
- Aggiornamento e-paper non più frequente di 5 minuti, refresh parziale quando
  possibile e refresh completo di pulizia ogni 6 cicli.
- Brevissimo lampeggio del LED blu in corrispondenza dell'heartbeat di circa
  30 minuti.
- Fuel gauge lasciato operativo durante il deep sleep; percentuale filtrata
  mediante tre campioni per ciclo e mediana di otto valori storici.

## Parametri modificabili nello YAML

Nel blocco `sensor: - platform: envirosense_cube` sono configurati offset e
soglie di pubblicazione:

```yaml
temperature_offset: -2.3
humidity_offset: 0.0
pressure_offset: 0.0
temperature_threshold: 0.1
humidity_threshold: 0.5
pressure_threshold: 0.5
static_iaq_threshold: 10.0
battery_threshold: 5.0
```

La modifica di questi valori richiede una nuova compilazione e un caricamento
OTA. Lo Static IAQ è l'indicatore principale mostrato sul display; Home
Assistant espone anche Adaptive IAQ, accuratezza, stato di stabilizzazione,
CO2 equivalente e VOC equivalente.

## Struttura

```text
.
├── envirosensecube.yaml
├── secrets.example.yaml
└── components/
    └── envirosense_cube/
        ├── sensor.py
        ├── envirosense_cube.h
        ├── envirosense_cube.cpp
        ├── GDEW0097T50_lib.h
        ├── GDEW0097T50_lib.cpp
        └── icon_*.h
```

Il componente è locale: la cartella `components/envirosense_cube` deve trovarsi
accanto allo YAML anche nel server ESPHome Device Builder.

## Installazione in ESPHome Device Builder

1. In Device Builder scegliere **Crea dispositivo → Importa da file** e
   caricare `envirosensecube.yaml`.
2. Copiare `components/envirosense_cube` in
   `/config/esphome/components/envirosense_cube` sul server Home Assistant.
3. Copiare `secrets.example.yaml` come `secrets.yaml` oppure aggiungere i
   seguenti segreti all'archivio Secrets già presente:

   ```yaml
   wifi_ssid: "..."
   wifi_password: "..."
   api_encryption_key: "..."
   ota_password: "..."
   ```

4. Conservare l'attuale chiave API se il dispositivo è già registrato in Home
   Assistant; cambiarla interrompe l'autenticazione dell'integrazione esistente.
5. Creare in Home Assistant l'helper
   `input_boolean.envirosensecube_ota_mode`.
6. Nell'integrazione ESPHome abilitare il permesso che consente al dispositivo
   di eseguire azioni Home Assistant. Serve allo switch nativo
   **OTA Maintenance Mode** per comandare l'helper.
7. Convalidare la configurazione nel Device Builder prima di installarla.

Il file contiene attualmente l'indirizzo statico `192.168.1.31`; gateway,
subnet e indirizzo devono essere adattati alla rete di destinazione.

## Aggiornamento OTA

Metodo consigliato durante lo sviluppo:

1. Attivare `input_boolean.envirosensecube_ota_mode` mentre il nodo è online.
2. Attendere che **OTA Maintenance Active** risulti attivo.
3. Dal Device Builder scegliere **Installa → Wirelessly**.
4. Terminato il lavoro, disattivare la modalità OTA per ripristinare il deep
   sleep.

In alternativa, dopo una vera rimozione e riapplicazione dell'alimentazione,
la porta OTA rimane disponibile per circa 20 secondi.

Da riga di comando:

```bash
esphome compile envirosensecube.yaml
esphome upload envirosensecube.yaml --device 192.168.1.31
```

## Stato BSEC2

- Lo stato BSEC2 viene catturato in RTC a ogni campione gas valido e ripristinato
  dopo il deep sleep.
- Con accuratezza 3 viene creato automaticamente un checkpoint in flash ogni
  72 campioni, pari a circa 6 ore.
- Il pulsante diagnostico **Save BSEC State** forza un checkpoint manuale.
- Il contenuto di calibrazione appartiene al dispositivo e non è incluso nel
  repository.

## Sicurezza e file esclusi

`secrets.yaml`, artefatti di compilazione, firmware, cache Python e stato BSEC
non devono essere versionati. Il repository contiene soltanto
`secrets.example.yaml` con valori segnaposto.

## Note di sviluppo

Il driver e-paper è specifico del progetto e usa Adafruit GFX per comporre il
framebuffer. Le scelte architetturali, la disposizione dello stato RTC e le
indicazioni per proseguire lo sviluppo sono documentate in
[`CONTEXT_HANDOFF.md`](CONTEXT_HANDOFF.md).

## License

Copyright 2026 alcor55 and contributors.

Il codice originale, la configurazione e la documentazione di EnviroSenseCube
sono distribuiti con licenza **Apache-2.0**. Il testo ufficiale integrale,
senza condizioni aggiuntive, è in [LICENSE](LICENSE); le attribuzioni sono
in [NOTICE](NOTICE).

Il wrapper Bosch incluso conserva la licenza **BSD-3-Clause** e le sue
attribuzioni. Il binario BSEC2 e le altre dipendenze di compilazione mantengono
le proprie condizioni: non sono convertiti in Apache-2.0. Vedere
[THIRD_PARTY.md](THIRD_PARTY.md) prima di redistribuire sorgenti o firmware.
