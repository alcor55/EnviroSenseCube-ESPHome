# EnviroSenseCube ESPHome — Context Handoff

## Continuità temporale BSEC2 — 25 ottobre 2026

La sessione continua in OTA recupera accuracy 3 e run-in completato; l'utente
osserva invece Static IAQ 0 e accuracy 0 per giorni in modalità deep sleep.
Non è ancora dimostrata la causa esclusiva. Nessun reset di calibrazione.

La correzione aggiunge un riferimento temporale a 64 bit in un record
RTC separato da 24 byte (chiave `0xEC5CE505`), associato all'hash del blob BSEC2.
`SupportData` resta a 272 byte, e gli offset delle preferences esistenti non
cambiano. Il sonno programmato viene contato solo su reset da deep sleep;
su perdita di alimentazione o fallback flash si parte con un nuovo riferimento
temporale, conservando il blob di calibrazione. Il sonno segue `next_call`
della libreria anziché soltanto un intervallo esterno.

La copia BSD locale del solo wrapper Bosch (`sleep_aware_bsec2.*`) aggiunge
l'offset temporale e l'accesso a `next_call`. Algoritmo BSEC binario, profilo
ULP, formato dello stato e driver BME68x restano invariati. Vedere
`components/envirosense_cube/BSEC_WRAPPER_NOTICE.md`.

Test host dell'orologio passati e compilazione ESPHome 2025.7.5 riuscita.
Prima osservazione sul dispositivo bianco/nero positiva: tra le 20:25 e le
20:56 del 25 ottobre, OTA risulta spento, accuracy resta 3 e run-in completato,
mentre IAQ statico passa da 175 a 144 e adattivo da 74 a 68. Gli screenshot
confermano nuove letture, non il numero esatto di deep sleep effettuati.
Consolidamento e trasferimento alla tricolore richiesti dall'utente; resta
necessaria l'osservazione prolungata e la verifica sul dispositivo tricolore.
Nessun caricamento firmware automatico.

Stato del progetto al 16 settembre 2026. Questo documento serve a riprendere
lo sviluppo senza ricostruire le decisioni prese durante la migrazione dal
firmware Arduino a ESPHome.

## Obiettivo

Il firmware deve comportarsi come un normale dispositivo ESPHome in Home
Assistant, mantenendo consumi ridotti, letture batteria stabili, uno stato
BSEC2 valido attraverso il deep sleep e un display e-paper leggibile. Il
firmware Arduino originale è un progetto separato e non viene modificato.

## Piattaforma e dipendenze

- Target: Adafruit HUZZAH ESP8266, framework Arduino.
- Ultima compilazione locale verificata con ESPHome `2025.7.5` e piattaforma
  `platformio/espressif8266@4.2.1`.
- Bosch BME68x Sensor Library `1.3.40408`.
- Bosch BSEC2 Software Library `1.10.2610` (BSEC `2.6.1.0`).
- Adafruit LC709203F `1.3.4`.
- Adafruit GFX `1.11.11`.
- Driver locale Good Display GDEW0097T50.

Il componente ESPHome è implementato in `components/envirosense_cube` e viene
caricato tramite `external_components` con sorgente locale.

## Flusso operativo

### Avvio a freddo

1. L'assenza dello stato RTC identifica un cold boot.
2. Viene aperta una finestra Wi-Fi/API/OTA di 20 secondi.
3. Il firmware avvia il ciclo BSEC2 e prosegue normalmente.
4. Se esiste un checkpoint BSEC in flash, viene ripristinato.

### Risveglio ordinario

1. Il dispositivo ripristina `SupportData` dall'RTC.
2. Wi-Fi rimane spento.
3. BME688 e BSEC2 vengono inizializzati in modalità ULP.
4. Viene ripristinato lo stato BSEC2 dall'RTC.
5. Vengono acquisiti BSEC2, T/H/P, gas e batteria.
6. Il nuovo stato BSEC viene ricatturato in RTC.
7. Display e pubblicazione vengono valutati attraverso le soglie.
8. Se non occorre pubblicare, il dispositivo torna direttamente in deep sleep
   per 300 secondi.

Il vecchio ciclo intermedio T/P/H-only da 150 secondi è stato eliminato. Ogni
risveglio esegue ora un ciclo BSEC2 completo ogni circa 5 minuti.

### Pubblicazione Native API

Wi-Fi viene abilitato soltanto quando:

- temperatura, umidità, pressione o Static IAQ superano la rispettiva soglia;
- la batteria varia almeno del 5%;
- scade l'heartbeat di 6 cicli, circa 30 minuti;
- è attiva la manutenzione OTA.

Il firmware attende autenticazione e sottoscrizione del client API prima di
pubblicare. I valori non finiti non vengono inviati a Home Assistant, evitando
stati `NaN` nei cicli incompleti o durante il riavvio. Un fallimento di rete o
API non imposta un retry obbligatorio al risveglio successivo.

## Modalità OTA

L'helper Home Assistant autorevole è:

```text
input_boolean.envirosensecube_ota_mode
```

Il binary sensor interno importa l'helper e lo switch template
**OTA Maintenance Mode** lo rende visibile nella pagina del dispositivo.
L'integrazione ESPHome deve consentire al nodo di eseguire azioni Home
Assistant.

Quando OTA è attivo:

- deep sleep viene impedito;
- Wi-Fi resta abilitato;
- il ciclo BSEC2 continua ogni 300 secondi;
- lo stato viene comunque aggiornato in RTC.

Quando OTA viene spento, il firmware consente nuovamente il deep sleep.

## BSEC2 e calibrazione

- Profilo: BME688 3,3 V, sample rate ULP da 300 secondi.
- Output richiesti: IAQ adattivo, Static IAQ, raw T/P/H/gas,
  stabilization status, run-in status, CO2 equivalente e breath VOC
  equivalente.
- Il display usa Static IAQ; Home Assistant espone sia Static sia Adaptive IAQ.
- Lo stato BSEC viene salvato in RTC a ogni campione valido.
- Il checkpoint automatico in flash avviene ogni 72 campioni soltanto con
  accuratezza almeno 3, per limitare l'usura.
- Il pulsante **Save BSEC State** forza il checkpoint dell'ultimo blob RTC
  valido anche quando l'accuratezza corrente non è disponibile.

Durante la diagnosi è stato dimostrato che BSEC2 produce output validi: in
modalità LP continua l'accuratezza è passata da 0 a 1 e IAQ ha iniziato a
variare. Successivamente il profilo ULP di produzione ha ripristinato lo stato
e prodotto valori Static e Adaptive IAQ variabili. I firmware temporanei di
test e recovery sono stati rimossi.

## Stato RTC

`SupportData` occupa 272 byte e contiene:

- riferimenti dell'ultima pubblicazione;
- snapshot necessario a ricostruire il precedente framebuffer e-paper;
- storico compatto della batteria;
- contatori di display, heartbeat e checkpoint;
- flag operativi, incluso OTA;
- blob BSEC2.

Esiste una migrazione one-shot dal precedente layout RTC da 320 byte. Il bit 5
dei flag, usato in passato per alternare BSEC e TPH-only, è ora riservato per
preservare il layout. Non riordinare o ridimensionare `SupportData` senza
progettare una nuova migrazione e aggiornare lo `static_assert`.

Le chiavi preference correnti sono:

- RTC: `0xEC5CE502`;
- checkpoint BSEC flash: `0xEC5CE503`.

## Batteria

Il LC709203F viene inizializzato con un percorso che preserva lo stato nei
risvegli da deep sleep. Per ogni ciclo:

1. vengono acquisiti tre campioni di tensione e percentuale;
2. si usa la mediana dei campioni validi;
3. la percentuale viene inserita in uno storico RTC di otto valori;
4. la percentuale pubblicata è la mediana dello storico.

La soglia di pubblicazione riguarda esclusivamente la percentuale filtrata ed
è configurata nello YAML a 5%. Il fuel gauge rimane operativo durante il sonno
per non perdere il proprio inseguimento dello stato di carica.

## Display e-paper

- Risoluzione logica: 184×88 px.
- Framebuffer composto con `GFXcanvas1`.
- Icone conservate in header indipendenti.
- Quattro misure principali: temperatura, umidità, pressione e Static IAQ.
- Pressione e IAQ sono interi; temperatura e umidità mostrano una cifra
  decimale.
- Batteria rappresentata da icona e barra orizzontale in basso.
- Nessun grafico storico locale.
- Refresh parziale full-screen quando esiste uno snapshot compatibile.
- Refresh completo di pulizia ogni 6 cicli, circa 30 minuti, o su richiesta.
- Timeout complessivo dell'operazione display: 20 secondi.
- Il pulsante **Refresh Display** forza temporaneamente un refresh completo.

GPIO2 è condiviso tra chip-select e LED blu. Il lampeggio heartbeat viene
eseguito solo dopo la transazione e-paper, quando SPI è inattivo.

## Configurazione modificabile

Offset e soglie sono dichiarati in `envirosensecube.yaml` e validati da
`sensor.py`. Non sono più costanti hard-coded nel C++:

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

Cambiarli dallo YAML nel Device Builder richiede compilazione e upload. Una
possibile evoluzione è esporli come entità `number` persistenti; durante il
deep sleep sarebbero regolabili attivando prima OTA.

## Entità Home Assistant

Misure principali:

- Temperature;
- Humidity;
- Pressure;
- Gas Resistance;
- Static IAQ;
- Adaptive IAQ;
- IAQ Accuracy;
- CO2 Equivalent;
- Breath VOC Equivalent;
- Battery.

Diagnostica e controllo:

- Battery Raw;
- Battery Voltage;
- Battery Reading Valid;
- BSEC Stabilized;
- BSEC Run In Complete;
- OTA Maintenance Active;
- OTA Maintenance Mode;
- Refresh Display;
- Save BSEC State.

Se Home Assistant continua a rendere indisponibili le entità durante il deep
sleep dopo una precedente installazione recovery dichiarata senza deep sleep,
rimuovere e riaggiungere soltanto la voce dell'integrazione ESPHome. Lo storico
delle entità va valutato prima di questa operazione.

## Build verificata

Ultima compilazione locale dopo l'eliminazione dei cicli TPH-only:

```text
RAM:   41076 / 81920 byte (50.1%)
Flash: 550139 / 1044464 byte (52.7%)
```

Comandi di verifica:

```bash
esphome config envirosensecube.yaml
esphome compile envirosensecube.yaml
```

Il caricamento è intenzionalmente manuale. Non automatizzare upload o flash
senza conferma dell'operatore.

## Vincoli e punti di attenzione

- Non versionare mai `secrets.yaml`, chiavi API/OTA o credenziali Wi-Fi.
- Non cancellare o cambiare le preference BSEC senza considerare la
  calibrazione accumulata.
- Conservare il percorso state-preserving del LC709203F nei risvegli ordinari.
- Il BSEC2 ULP dipende da un intervallo vicino a 300 secondi; prove accelerate
  devono usare un firmware diagnostico separato.
- L'indirizzo IP statico è attualmente `192.168.1.31`.
- Il Device Builder deve ricevere anche la cartella del componente locale.
- Verificare la compatibilità del componente prima di aggiornare ESPHome oltre
  la versione con cui è stato compilato.

## Prossimi passi ragionevoli

1. Completare l'importazione nel Device Builder del server Home Assistant.
2. Convalidare e compilare con la versione ESPHome del server.
3. Valutare entità `number` per offset e soglie modificabili a runtime.
4. Misurare la corrente reale nei tre stati: deep sleep, misura BSEC e Wi-Fi.
5. Stimare l'autonomia soltanto dopo una misura strumentale del duty cycle.
