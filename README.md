# EnviroSenseCube ESPHome - E2154JS0C1

Variante del firmware ESPHome per il monitor ambientale a batteria
EnviroSenseCube, basata su ESP8266, BME688, BSEC2, fuel gauge LC709203F e
display e-paper Pervasive Displays E2154JS0C1 a tre colori.

La branch Git si chiama `ESPHome-SE2154JS0C1` per seguire il nome richiesto;
il codice prodotto ufficiale del pannello è però **E2154JS0C1**, senza `S`
iniziale.

Questa implementazione usa la Native API di ESPHome: Home Assistant crea
direttamente le entità del dispositivo, senza webhook, MQTT, `input_text` o
sensori template.

## Hardware

| Componente | Configurazione |
| --- | --- |
| MCU | Adafruit HUZZAH ESP8266 |
| Sensore ambientale | Bosch BME688, I2C `0x77` |
| Fuel gauge | Adafruit LC709203F, I2C `0x0B` |
| Display | Pervasive Displays E2154JS0C1, 152×152 px, nero/bianco/rosso |
| Interfaccia pannello | PDI EXT3/EXT3.1 o circuito boost equivalente per Film J |
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

I GPIO indicano il collegamento logico tra HUZZAH e la scheda di pilotaggio.
Il pannello nudo a 24 pin richiede il circuito boost indicato dal produttore:
non deve essere collegato direttamente all'ESP8266 e non è elettricamente
intercambiabile con il precedente pannello Good Display usando soltanto il
connettore FPC.

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
- Il pannello Film J supporta il solo refresh globale; il tempo tipico indicato
  dal produttore è circa 14,5 secondi.
- Le variazioni del display vengono accorpate per almeno 6 cicli, circa 30
  minuti. Un refresh globale viene comunque forzato ogni 72 cicli, circa 6 ore,
  oppure tramite il pulsante Home Assistant **Refresh Display**.
- Valori numerici neri, icone e accenti rossi; anche IAQ da 100 in su e barra
  batteria al 20% o meno diventano rossi.
- Brevissimo lampeggio del LED blu in corrispondenza dell'heartbeat di circa
  30 minuti.
- Fuel gauge lasciato operativo durante il deep sleep; percentuale filtrata
  mediante tre campioni per ciclo e mediana di otto valori storici.

## Parametri modificabili nello YAML

Nel blocco `sensor: - platform: envirosense_cube` sono configurati offset e
soglie di pubblicazione:

```yaml
temperature_offset: -1.3
humidity_offset: -1.0
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
        ├── E2154JS0C1_lib.h
        ├── E2154JS0C1_lib.cpp
        └── icon_*.h
```

Il componente è locale: la cartella `components/envirosense_cube` deve trovarsi
accanto allo YAML anche nel server ESPHome Device Builder.

## Installazione in ESPHome Device Builder

1. In Device Builder scegliere **Crea dispositivo → Importa da file** e
   caricare `envirosensecube.yaml`.
2. Copiare `components/envirosense_cube` in
   `/config/esphome/components/envirosense_cube` sul server Home Assistant,
   e `flash_layout` in `/config/esphome/flash_layout`.
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
- Il primo checkpoint automatico in flash avviene con accuratezza almeno 2;
  i successivi sono distanziati di 72 cicli, circa 6 ore, e richiedono ancora
  accuratezza almeno 2.
- Il pulsante diagnostico **Save BSEC State** forza un checkpoint manuale.
- Un backup iniziale verificato viene conservato separatamente in LittleFS;
  **Restore BSEC2 Backup** lo ripristina. **Reset BSEC2 Calibration** invalida
  lo stato attivo e il checkpoint, senza eliminare il backup.
- Il layout flash riserva 32 KiB per il backup senza spostare le preferences.
  La formattazione avviene solo se l'intera area risulta vuota.
- **Diagnostic Logging** abilita i log INFO da HA; dopo ogni avvio resta spento,
  lasciando attivi avvisi ed errori.
- In OTA la sessione BSEC2 resta attiva e i cicli sono allineati a 300 secondi.
  Anche il deep sleep viene ridotto del tempo impiegato dal ciclo.
- La pubblicazione attende Home Assistant, non il semplice client dei log;
  alla riconnessione HA vengono reinviati i valori finiti disponibili.
- Il contenuto di calibrazione appartiene al dispositivo e non è incluso nel
  repository.

## Sicurezza e file esclusi

`secrets.yaml`, artefatti di compilazione, firmware, cache Python e stato BSEC
non devono essere versionati. Il repository contiene soltanto
`secrets.example.yaml` con valori segnaposto.

## Note di sviluppo

Il driver e-paper è un port minimale della sequenza globale Film J / driver C
del progetto ufficiale
[PDLS_EXT3_Basic_Global](https://github.com/PervasiveDisplays/PDLS_EXT3_Basic_Global),
distribuito con licenza CC BY-SA 4.0. Adafruit GFX compone due framebuffer da
1 bit, uno nero e uno rosso. Le scelte architetturali, la disposizione dello
stato RTC e le indicazioni per proseguire lo sviluppo sono documentate in
[`CONTEXT_HANDOFF.md`](CONTEXT_HANDOFF.md).
