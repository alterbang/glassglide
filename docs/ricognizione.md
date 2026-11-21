# Ricognizione iniziale

Data: 17 novembre 2026  
Dispositivo: LG OLED55E7N, connesso via SSH all'indirizzo `192.168.1.204`.

## Risultati

- La TV risponde come `LGwebOSTV`; il modello letto dal dispositivo è `LG
  Electronics, DTV SoC LG1313 (AArch32)`.
- `/etc/starfish-release` riporta `Rockhopper release 3.9.2-6270908
  (dreadlocks2-dudhwa)`, `/etc/version` riporta `202211181357` e `uname -m`
  restituisce `armv7l`.
- Il socket Wayland `/tmp/xdg/wayland-0` esiste. Sulla TV sono presenti le
  librerie EGL, GLES e Wayland, incluse quelle webOS/Starfish. Il socket è
  `root:composit` con permessi `0770`: l'app installata ha aperto il socket
  dal proprio contesto non root.
- La partizione applicazioni ha circa 3 GiB liberi. La root overlay è piena;
  non usarla per installare o copiare dipendenze.
- Non risultano WPE WebKit, compilatore C/C++ o `ares-package` disponibili
  sulla TV.
- La macchina di sviluppo è Debian 13 ARM64, con pagine da 16 KiB. I tool
  vengono installati nel progetto, senza modificare il sistema condiviso.
- La pagina ufficiale WPE WebKit indica 2.54.2 come release stabile, datata
  23 ottobre 2026. È il candidato della spec, non una conferma di compatibilità
  con questa TV. L'indice degli advisory consultato il 17 novembre 2026
  riporta come ultimo [WSA-2026-0006](https://wpewebkit.org/security/WSA-2026-0006.html);
  la versione stabile non elimina la necessità di aggiornamenti e isolamento.
- `../stremio-origin-webos` è stato consultato solo come riferimento in
  lettura. I suoi script descrivono un SDK e Zig sotto `$HOME/origin-build`,
  ma quei componenti non sono presenti su questa macchina.

## Toolchain e compositor

- L'SDK `openlgtv/buildroot-nc4` è stato scaricato e verificato; include GCC
  16.2 e una glibc di target 2.12.2. Una prova ARMv7 C e una prova C++ con
  `libstdc++` privata sono state eseguite sulla TV. Non dimostrano ancora che
  WPE o l'app installata funzionino.
- Gli script selezionano l'SDK e il Clang privato per host Linux ARM64 o
  x86_64, mantenendo ARMv7 come target. La build completa è stata provata su
  ARM64; il percorso nativo x86_64 è predisposto ma non ancora verificato su
  un host x86_64.
- La configurazione Buildroot usa ARM EABI `softfp` e NEON-FP16, coerenti con
  la toolchain e le istruzioni rilevate sulla TV. Le dipendenze compilate
  restano sotto `.deps/`; non vengono installate sul sistema della TV.
- La build Wayland non sostituisce il wrapper EGL previsto dall'SDK webOS.
  Quest'ultimo è uno stub di linking: il pacchetto rifiuta di includerlo e
  richiede una verifica sulla TV se risulta effettivamente necessario.
  Il backend Wayland WPE non usa `wl_egl_window`: una patch elimina il suo
  collegamento superfluo allo stub, da verificare sul binario finale.
- La fixture usa la piattaforma WPE moderna, senza il backend legacy FDO;
  GStreamer è disabilitato esplicitamente; le compilazioni Ninja usano un job
  per default, oppure due su richiesta. `WK_USE_CCACHE=NO` evita che
  una rigenerazione CMake cambi il launcher e ricompili l'intero motore.
  `Document.cpp` è compilato separatamente con Clang 19, `-O0` e un header
  precompilato dedicato: le prove con GCC superavano il limite prudenziale
  dell'host condiviso. Il resto di WPE usa GCC 16; l'ABI ARM dell'oggetto
  prodotto è coerente con l'SDK; la fixture gira sulla TV, ma la toolchain
  mista e le prestazioni non sono validate per un browser completo.
  Gli spec del GCC privato impostano una raccolta della memoria più frequente
  senza cambiare il codice generato: una prova del bundle HTML interrotto è
  riuscita con il PCH GCC e un limite di 2,5 GiB per processo. I margini
  dell'host condiviso devono comunque essere monitorati durante la build.
  Tre sorgenti HTML con costo elevato sono compilati separatamente per
  ridurre la dimensione delle unità unificate successive.
  Una patch usa le costanti `madvise` dei kernel header quando mancano nella
  glibc meno recente dell'SDK.
- WPE 2.54.2 e il prototipo C++ sono stati compilati per ARMv7. Una patch
  esclude dal codice degli snapshot DMA-BUF il riferimento a DRM quando
  libdrm non è abilitata.
- Libgcrypt è stato ricompilato senza assembler perché la sola disattivazione
  delle ARMv8 Crypto Extension non eliminava il codice ARMv8 dal binario.
  La verifica ELF ora rileva ARMv7. La variante portabile può essere più lenta.
- Il link condiviso di WPE esclude i polyfill dell'SDK, la cui licenza non è
  dichiarata. Il packaging rifiuta il loro marker nei binari. `legal-info`
  segnala ancora avvisi sulle licenze di componenti dell'SDK e sul sorgente
  Buildroot: il pacchetto è una prova locale, non una release redistribuibile.
- Da SSH il compositor espone, fra gli altri, `wl_compositor` v2, `wl_shm`
  v1, `wl_output` v1, `wl_shell` v1 e `wl_webos_shell` v1; non espone
  `xdg_wm_base` o `linux_dmabuf`. È stata predisposta una patch per pubblicare
  le dimensioni dello schermo anche con `wl_output` v1, che non invia
  l'evento `done`, e una per rimuovere dispositivi di input senza richieste
  introdotte dopo `wl_seat` v1. WPE originale selezionava un solo seat;
  la patch aggiunta ascolta anche le tastiere degli altri seat e mantiene il
  focus finché almeno una di esse lo conserva. Un'ulteriore patch estende
  l'ascolto dei puntatori agli altri seat; touch, clipboard e input method
  restano sul seat principale. Se quest'ultimo scompare dopo l'avvio, l'app
  segnala la perdita e si chiude. Una patch a libxkbcommon alza il
  limite dei keycode fino a `0x5030`, necessario per le mappe webOS: le tre
  mappe ricevute vengono compilate e un valore superiore al limite resta
  rifiutato.
- L'adattatore previsto assegna il ruolo `wl_shell` alla superficie WPE e
  associa l'estensione `wl_webos_shell`; inoltra a WPE ridimensionamento,
  stato della finestra e focus. La superficie dell'app è stata mappata e ha
  ricevuto focus e notifica di frame visualizzato. L'utente ha confermato
  frecce e OK sulla fixture; puntatore, altri tasti e tastiera fisica restano
  da verificare.
- Il driver EGL Mali della TV non espone `EGL_MESA_platform_surfaceless`.
  Una patch WPE usa il display EGL predefinito solo se supporta
  `EGL_KHR_surfaceless_context`; nella jail sono stati verificati un contesto
  GLES senza superficie e un pbuffer. Il compositore Skia produceva un frame
  bianco anche con il painting CPU; selezionando TextureMapper tramite
  `WEBKIT_USE_SKIA_FOR_COMPOSITION=0`, il buffer Wayland contiene la fixture
  HTML/CSS e il testo aggiornato da JavaScript. Questo seleziona il
  compositore alternativo, non disabilita tutta l'accelerazione grafica.
- Il caricatore glibc 2.19 richiede attenzione alla disposizione delle
  relocation ARM. La preparazione del pacchetto controlla ogni ELF privato,
  elimina gli RPATH di build dalle copie e rifiuta dipendenze mancanti o
  simboli glibc più recenti o versioni C++ mancanti. Il tool IPK ufficiale
  produceva directory scrivibili da tutti: il pacchetto viene normalizzato a
  proprietà `root` e permessi non scrivibili da gruppo o altri.
- L'installer webOS usa il comando `ar` di BusyBox 1.22.1, che rifiuta i
  membri creati da `ar rcD` senza i bit di tipo file regolare. Il packaging
  conserva metadati deterministici e inserisce tali bit negli header `ar`.
  GNU tar 1.17 sulla TV segnala anche i timestamp Unix zero: i file negli
  archivi sono ora datati al 1° gennaio 2000.
- Il primo IPK installato usava nomi estesi PAX, che l'opkg della TV non
  interpreta: tra i file mancanti c'erano il bundle WPE e un file di licenza.
  Il nuovo IPK usa USTAR e rifiuta i percorsi non rappresentabili. Le directory
  condivise `usr`, `usr/palm`, `applications` e `packages` non sono più voci
  dell'archivio: la prima installazione ne aveva modificato i permessi, che
  sono stati ripristinati ai valori osservati prima della prova.

## Prototipo locale successivo

- È stata preparata una nuova interfaccia browser offline con quattro pagine
  incorporate, sei schede al massimo, navigazione, preferiti e cronologia.
  La pagina della scheda attiva viene ricaricata a ogni cambio di scheda.
- Una superficie nativa Wayland, collegata tramite il gruppo di superfici
  webOS, ospita i controlli. Il compositor della TV non espone
  `wl_subcompositor`; la sovrapposizione e la pagina iniziale sono state
  osservate insieme sul display. Le prove successive su puntatore, BACK e
  prestazioni hanno rilevato i problemi descritti sotto; EXIT e lifecycle
  restano da verificare.
- I soli identificativi delle pagine locali, le schede normali, preferiti e
  cronologia sono salvati in `/media/developer/.glassglide-data` con directory
  privata e scrittura atomica. Il genitore è condiviso: questi controlli non
  dimostrano la riservatezza della jail. La directory è stata creata sulla TV
  con UID 5786 e permessi `0700`; la persistenza attraverso i riavvii resta da
  verificare. Le schede private non sono salvate e non aggiungono cronologia,
  ma tutte le schede condividono una sessione WebKit effimera.
- Il 19 novembre la nuova build è stata installata e avviata sulla TV; il
  binario installato corrisponde a quello del pacchetto. Il blocco degli
  indirizzi esterni e dei download rimane attivo; la nuova interfaccia non
  abilita siti web.
- Dopo una prova in cui il puntatore non attivava i pulsanti, WPE è stato
  modificato per ascoltare i puntatori anche sui seat secondari. L'utente ha
  confermato che il puntatore ora funziona sulla TV.
- BACK apre ancora la dashboard webOS invece di riportare alla barra in
  modalità pagina, anche dopo aver richiesto BACK ed EXIT al compositor.
  Non è ancora noto se l'evento raggiunga l'app. La build più recente consente
  di cliccare di nuovo **Pagina** per tornare ai controlli; l'utente ha
  confermato che questo ripiego funziona sulla TV.
- L'utente ha riferito ritardi di 4–5 secondi tra la pressione e l'aggiornamento
  «Tasto ricevuto». Durante la prova il processo WPEWebProcess ha utilizzato
  circa un core per decine di secondi, a fronte di un uso molto minore del
  processo dei controlli. È un indizio di lavoro nella pagina, non una misura
  della latenza né una prova che la lettura e copia dei frame sia la causa.
- Dopo il riavvio della TV, la cartella dati e il file `state` sono stati
  trovati con permessi `0777`, invece dei `0700` e `0600` iniziali. L'app
  ha rifiutato l'archivio e ha segnalato che i dati non venivano salvati.
  La causa della modifica e una soluzione durevole restano da verificare.

## Sicurezza e stato

- Il kernel della TV non abilita `USER_NS`, `NET_NS`, `SECCOMP` o `SECURITY`:
  la sandbox Bubblewrap standard di WPE non è utilizzabile in questa forma.
  L'isolamento effettivo della jail webOS non è ancora dimostrato.
- La configurazione WPE in sviluppo disabilita esplicitamente Bubblewrap
  **solo per contenuti locali fidati**. Il prototipo rifiuta l'esecuzione
  come root, non offre navigazione esterna e non deve caricare contenuti non
  attendibili. Processi separati o sessioni effimere non sostituiscono una
  sandbox.
- Il 18 novembre il servizio webOS ha installato e avviato l'IPK USTAR
  `dist/org.glassglide.browser_0.1.0_arm.ipk`. Il processo dell'app è stato
  osservato con UID 5786 e GID 5000, non root. Il primo IPK, precedente alla
  correzione USTAR, non si avviava per i percorsi PAX troncati.
- La fixture è stata verificata nella jail con una variante diagnostica e
  con l'IPK installato: DOM e JavaScript caricati e contenuto corretto nel
  buffer Wayland con TextureMapper. Questa prova non dimostra l'isolamento.
  Durante le prove monitorare RAM e swap, senza chiudere altre app né
  modificare la configurazione della TV.
- Prima della correzione l'utente riferiva che le frecce non aggiornavano la
  fixture. Una lettura dei soli metadati Wayland ha rilevato tre seat: il 19
  si identificava come `LGE M-RCU - Builtin [0]`, mentre il 20 non aveva
  fornito un nome; gli identificativi non sono stabili. Dopo l'installazione
  della patch multi-seat l'utente ha confermato frecce e OK, senza tracciare
  quale seat fisico abbia inviato i tasti.
- Un test con compositor fittizio e processo WPE non root, isolato dal
  compositor della TV, verifica più tastiere e puntatori, focus condiviso,
  aggiunta e rimozione dei seat, variazioni delle capacità e perdita del seat
  principale. Non sostituisce la verifica dei dispositivi fisici non ancora
  provati.
- Prima del fallback EGL, l'avvio fallito ha mostrato anche un segfault del
  processo UI dopo la terminazione del processo web. Il percorso di errore
  non è ancora validato.

## Fonte versione WPE

- https://wpewebkit.org/release/
