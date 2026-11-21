# GlassGlide — specifica iniziale

Stato: fixture locale WPE avviata sulla TV, con frecce e OK del telecomando
confermati; il browser completo e l'isolamento non sono ancora validati.
I risultati e i limiti correnti sono in
[docs/ricognizione.md](docs/ricognizione.md).
Ultimo aggiornamento: 19 novembre 2026.

## Obiettivo e decisioni

Realizzare un browser completo, autonomo e ottimizzato per TV LG webOS con
hardware datato. Il browser deve funzionare sulla TV senza un server esterno;
un computer può essere utilizzato per compilazione, installazione e test.

- Linguaggio dell'applicazione: **C++**.
- Motore candidato: **WPE WebKit**, da integrare direttamente tramite la sua
  API nativa. Non usare il Chromium 38 di sistema per visualizzare i siti.
- Interfaccia: controlli nativi C++ e integrazione Wayland/EGL; nessun secondo
  motore web per disegnare la UI. La scelta dei componenti grafici precisi
  dipende dalla prova di integrazione.
- Dispositivo primario: **LG OLED55E7N**, webOS 3.9.2. Altri modelli webOS
  sono obiettivi successivi, non compatibilità già garantita.
- Puntare a un motore mantenuto e aggiornabile. La versione stabile pubblicata
  al 17 novembre 2026 è WPE WebKit 2.54.2: è il primo candidato da valutare,
  non una garanzia di compatibilità con questa TV. Ricontrollare la versione
  e gli aggiornamenti di sicurezza all'inizio dello sviluppo.
- Il nome GlassGlide è stato scelto consapevolmente nonostante l'omonimia con
  un [gioco web chiamato Glass Glide](https://github.com/Santoshrt999/glass-glide).

## Hardware e sistema rilevati

Rilevazioni dirette sulla TV il 17 novembre 2026, salvo il modello comunicato
dall'utente:

| Componente | Dato |
| --- | --- |
| SoC e CPU | LG1313, quattro core Cortex-A53, frequenza massima rilevata circa 1,008 GHz |
| Sistema e ABI | ARMv7 a 32 bit, ARM EABI; verificare ABI e istruzioni di ogni binario |
| GPU | Mali-T82x MP2 r1p0; librerie EGL e OpenGL ES presenti |
| Sistema operativo | webOS 3.9.2, kernel Linux 4.4.3, glibc 2.19 |
| RAM | 2.031.864 KiB totali; `MemAvailable` variava fra circa 190 e 247 MiB durante le letture |
| Swap | circa 600 MiB totali; in una lettura ne restavano circa 27 MiB liberi |
| Storage | circa 3 GiB liberi nella partizione applicazioni al momento della lettura |
| Compositor | socket Wayland `/tmp/xdg/wayland-0`; librerie Wayland webOS e Starfish presenti |
| Motore LG | Chromium 38, confermato dall'endpoint di debug del sistema |

RAM, swap e storage sono **istantanee, non budget garantiti**. La presenza di
Wayland, EGL e GLES non dimostra che WPE possa usarli. Occorre verificare
accesso al socket nel contesto dell'app, protocolli del compositor, driver,
buffer grafici e accelerazione reale.

## Architettura prevista

```text
Telecomando / Magic Remote / tastiera / mouse
                    |
          Browser e controlli C++
                    |
        WPE WebKit + WPEPlatform
                    |
      Wayland / EGL / compositor LG
```

- WebKit gestisce pagine, DOM, JavaScript, rete e processi del motore; la UI
  gestisce navigazione, schede, preferiti, cronologia e integrazione con webOS.
- Provare prima il backend Wayland di WPEPlatform. Da WPE 2.54 questa è l'API
  raccomandata; il vecchio backend `libwpe` resta un'alternativa tecnica da
  valutare se disponibile nella versione mantenuta scelta.
- Verificare come comporre e sincronizzare pagina e controlli nativi, gestire
  focus, ridimensionamento e input. Non assumere che superfici WPE e UI si
  sovrappongano correttamente sul compositor LG.
- Preferire buffer GPU senza copie non necessarie; misurare eventuali fallback
  in memoria condivisa. La presenza di EGL non prova l'accelerazione.
- Se il percorso Wayland standard non funziona, valutare un'integrazione
  specifica per i protocolli LG. Non sostituire il compositor né prendere il
  controllo diretto dello schermo tramite DRM/KMS come scorciatoia.
- SDL2/GLES2, come in Stremio, è una possibile **alternativa**, non parte
  dell'architettura scelta: discutere un cambio di approccio se necessario.

## Requisiti del browser

### Navigazione e interfaccia

- Barra indirizzi e ricerca, apertura URL HTTP/HTTPS, avanti, indietro,
  ricarica, interruzione, pagina iniziale ed errori comprensibili.
- Schede, con una sola pagina attiva inizialmente; le altre devono poter
  essere sospese o scaricate sotto pressione di memoria. Il ripristino deve
  essere esplicito quando non è possibile conservare lo stato della pagina.
- Preferiti, cronologia, riapertura delle schede e sessioni persistenti.
- Zoom, navigazione con puntatore o tasti direzionali, focus visibile,
  selezione e scorrimento. UI leggibile a distanza e adattabile allo schermo.
- Magic Remote, tasti Back/Exit, tastiera e mouse: verificare gli eventi
  effettivamente ricevuti sulla TV. Prevedere una tastiera a schermo per
  indirizzi e campi web quando non è disponibile una tastiera fisica.
- Gestire correttamente passaggio in background, ritorno in primo piano e
  chiusura dell'app senza interferire con il launcher o altre app.
- Popup, nuove finestre, permessi, download e dialoghi dei siti devono avere
  una UI esplicita e utilizzabile dal telecomando.

### Dati e privacy

- Conservare preferiti, cronologia, cookie e impostazioni in una directory
  dati dell'app persistente e accessibile nel suo contesto di esecuzione.
  Non scrivere nel firmware o nelle directory di altre app.
- Consentire la cancellazione dei dati di navigazione e, dove possibile,
  per singolo sito. La navigazione privata usa un profilo temporaneo, non
  conserva cronologia o cookie alla chiusura e non promette di cancellare
  eventuali tracce gestite dal sistema operativo.
- I download richiedono conferma, mostrano avanzamento ed errori e restano in
  una posizione accessibile all'utente. Impedire che nomi non attendibili
  escano dalla directory di destinazione.
- Non registrare password, cookie, token o URL sensibili nei log. Non inviare
  telemetria di navigazione.
- Salvataggio password: solo dopo aver verificato un archivio protetto
  disponibile; non memorizzarle in chiaro come ripiego.

### Compatibilità web e multimedia

- HTML, CSS e JavaScript sono forniti dalla versione WPE selezionata: verificare
  i siti reali e non promettere compatibilità identica a Chrome.
- Puntare a video HTML5 e audio, fullscreen e comandi di riproduzione.
  Verificare separatamente codec, pipeline GStreamer e decodifica hardware
  LG; il funzionamento di Stremio non dimostra l'integrazione con WPE.
- DRM/EME e servizi protetti dipendono da integrazione, licenze e CDM:
  non sono garantiti. WebRTC, PDF ed estensioni richiedono verifiche dedicate;
  la compatibilità con le estensioni Chrome non è prevista automaticamente.

## Sicurezza

- I contenuti web non devono essere eseguiti come root. Verificare utente,
  permessi e isolamento effettivi dell'app e dei processi WPE nel pacchetto
  installato, non solo in una sessione SSH.
- Verificare la sandbox del motore e i vincoli del kernel 4.4 e della jail
  webOS. Processi separati da soli non equivalgono a una sandbox. Se
  l'isolamento non è realizzabile, documentare il blocco prima di navigare su
  contenuti non attendibili; non disabilitarlo silenziosamente.
- Validare certificati TLS e hostname con CA aggiornabili; un certificato
  non valido deve produrre un errore, non un caricamento silenzioso.
  Evitare di dipendere implicitamente dal vecchio stack TLS di sistema.
- Non esporre API privilegiate webOS, credenziali o accesso arbitrario al
  filesystem alle pagine. Richiedere solo i permessi indispensabili.
- Nessun endpoint di debug aperto sulla rete in una build distribuita.
  Aggiornare motore e dipendenze in risposta agli advisory di sicurezza.

## Build, installazione e riferimenti locali

- Predisporre una cross-compilazione riproducibile per ARMv7 EABI compatibile
  con il kernel e la glibc 2.19 della TV. Verificare `readelf`, ABI, versione
  dei simboli, dipendenze dinamiche e avvio dei processi sul dispositivo.
- Valutare SDK Buildroot webOS e Zig/C++ come riferimento: nel progetto
  Stremio la build ARM usa `arm-linux-gnueabi` con `-mfloat-abi=softfp`.
  Non presumere che questa configurazione basti a compilare WPE moderno.
- Le dipendenze più recenti necessarie a WPE vanno distribuite con l'app o
  altrimenti isolate, senza sostituire librerie di sistema. Verificare
  attentamente loader, ricerca delle librerie, conflitti ABI/SONAME e uso delle
  librerie grafiche LG: copiare librerie in una directory privata non basta
  di per sé a garantire compatibilità.
- Distribuire come app webOS nativa in un IPK indipendente, con ciclo di vita
  e directory dati propri. Testare installazione, aggiornamento, rimozione e
  persistenza; non richiedere modifiche permanenti al firmware.
- Il progetto `../stremio-origin-webos` è un riferimento **in sola lettura**:
  contiene esempi di SDL/Wayland, input, lifecycle, packaging e una correzione
  delle relocation ELF per il loader glibc 2.19 della stessa TV. La correzione
  va valutata per ogni eseguibile e libreria del nuovo progetto, non assunta
  sufficiente per WPE.
- Non copiare codice o asset da Stremio senza verificarne la licenza GPL-3.0
  e i relativi obblighi. La licenza del nuovo progetto è ancora da scegliere;
  rispettare anche le licenze di WebKit e delle altre dipendenze distribuite.

## Prestazioni e protezione della TV

- Misurare baseline e picco di tutti i processi del browser, non soltanto
  quello della UI: PSS da `/proc/<pid>/smaps` dove disponibile, altrimenti
  RSS dichiarando il metodo; rilevare anche `MemAvailable`, swap e, se
  accessibile, memoria GPU. Non sommare RSS come se fossero memoria univoca.
- Misurare avvio, primo contenuto visibile, fine caricamento, scorrimento,
  cambio scheda e uso CPU su pagine semplici e JavaScript rappresentative.
  Registrare condizioni, URL o fixture, versione e numero di ripetizioni.
- Definire limiti di memoria e tempi **dopo** la baseline sul dispositivo;
  non presentare stime come risultati. Gestire la pressione di memoria
  sospendendo o scaricando schede e mostrando un errore se necessario.
- Non terminare altre app, alterare swap, riavviare servizi o modificare
  configurazioni di sistema per far spazio. Prima dei test pesanti verificare
  che la TV non sia in uso e concordare come evitare saturazione e OOM.

## Fasi e criteri di verifica

1. **Ricognizione:** identificare toolchain, ABI, protocolli e permessi reali;
   verificare che una versione WPE mantenuta sia costruibile per il target.
   Registrare incompatibilità, dipendenze e possibili soluzioni.
2. **Prototipo locale:** compilare e avviare un'app nativa e WPE sulla TV.
   Una fixture locale deve mostrare HTML/CSS e modificare la pagina tramite
   JavaScript. Verificare avvio ripetuto e comportamento dei processi.
3. **Grafica e input:** mostrare la pagina tramite il compositor LG,
   identificare percorso GPU o fallback, usare puntatore e telecomando e
   misurare risorse. Verificare anche focus, background e ritorno alla UI.
4. **Sicurezza e rete:** dimostrare utente non root e isolamento, poi provare
   HTTPS con certificato valido e non valido e siti rappresentativi.
   Se un requisito di sicurezza blocca il porting, fermarsi e riportarlo.
5. **Browser utilizzabile:** implementare barra, navigazione, schede,
   preferiti, cronologia, persistenza e modalità privata; verificare che i
   dati normali sopravvivano al riavvio e quelli privati no.
6. **Completamento:** download, permessi, multimedia e ottimizzazione; testare
   un IPK installato, aggiornato e rimosso e documentare funzioni non supportate.

Per ogni fase registrare versione dei componenti, comandi, risultati e
limitazioni. Sul dispositivo eseguire almeno tre avvii e una sessione
interattiva di 15 minuti senza crash per il prototipo grafico; misurare anche
un sito semplice, uno con JavaScript e una sequenza di almeno tre schede,
senza assumere che il numero di schede attive sia illimitato. I test su
contenuti web non attendibili sono subordinati alla fase di sicurezza.

## Fonti

- [WPE WebKit: architettura e WPEPlatform](https://wpewebkit.org/about/architecture.html)
- [WPE WebKit: release](https://wpewebkit.org/release/)
- [WPE WebKit: security advisory](https://wpewebkit.org/security/)
- [LG: motori web per versione webOS TV](https://webostv.developer.lge.com/develop/specifications/web-api-and-web-engine)
- [Stremio Origin: compatibilità OLED55E7N](../stremio-origin-webos/docs/compatibility.md), solo lettura.
