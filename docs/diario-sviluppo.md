# Diario di sviluppo GlassGlide

**Periodo ricostruito:** 17–19 novembre 2026  
**Dispositivo di prova:** LG OLED55E7N, webOS 3.9.2  
**Stato:** fixture locale WPE installata e visibile; frecce e OK del telecomando confermati.

> Questo diario è stato ricostruito dalla conversazione, dai checkpoint e dai
> timestamp dei log disponibili. I log segnano quando sono avvenuti comandi e
> build, ma non misurano il tempo di lavoro attivo: perciò descrivo le ore di
> tentativi e le loro successioni senza inventare un conteggio preciso delle
> ore-persona o presentare le pause come lavoro continuativo.

## 17 novembre — Capire il target prima di compilare

Il punto di partenza è stata la specifica: l’obiettivo non era soltanto
mostrare una pagina, ma realizzare un browser C++ autonomo per webOS, usando
WPE WebKit e integrandosi con la grafica e il telecomando della TV.

La prima connessione SSH ha usato `192.168.1.214`, un indirizzo rimasto in una
configurazione vecchia. Ho letto le istruzioni locali del progetto e trovato
l’indirizzo aggiornato, `192.168.1.204`, con la chiave SSH prevista. Da lì è
iniziata la ricognizione diretta del dispositivo.

Sono risultati subito chiari alcuni vincoli:

- la TV usa ARMv7 a 32 bit e webOS 3.9.2, con glibc 2.19;
- il compositor offre protocolli Wayland più vecchi di quelli che WPE moderno
  si aspetta;
- il driver grafico di sistema deve essere usato senza sostituire le librerie
  LG;
- la memoria libera è limitata e varia sensibilmente;
- il kernel non offre le primitive richieste dalla sandbox Bubblewrap standard.

Questo ultimo punto ha cambiato l’ambito della prima build: niente siti remoti
o pagine non fidate. Il prototipo doveva essere una fixture locale controllata,
con navigazione esterna e download bloccati, e con un controllo che rifiutasse
l’esecuzione come root.

La preparazione ha richiesto una toolchain diversa da quella disponibile sulla
TV. SDK webOS e Buildroot sono stati scaricati e verificati localmente; una
prima compilazione di prova ARMv7, seguita da una prova C++, ha confermato che
era possibile produrre binari compatibili con il target. Non dimostrava ancora
che un motore web potesse funzionare.

## 17 novembre sera — Sbloccare Buildroot e l’ABI

Il primo tentativo di compilare WPE si è fermato prima di arrivare al motore:
una regola della toolchain generava una riga di shell non valida (`; ;`).
Ho confrontato la ricetta Buildroot con la funzione di verifica degli header
del kernel, mancante nell’albero usato, e ho corretto la preparazione in modo
riproducibile, invece di saltare il controllo.

La configurazione iniziale selezionava anche il floating point `soft`,
mentre SDK e TV richiedevano EABI `softfp` con NEON. Ho corretto e ricontrollato
le opzioni ARM prima di proseguire: una build che termina non sarebbe bastata
se avesse generato codice con ABI incompatibile.

## Notte tra il 17 e il 18 — Portare WPE sui protocolli della TV

La TV non espone `xdg_wm_base`, che WPE moderno usa comunemente, ma espone
`wl_shell` e l’estensione `wl_webos_shell`. Ho quindi scritto un adattatore
nativo che assegna alla superficie WPE il ruolo legacy richiesto da webOS,
gestisce fullscreen, ridimensionamento, stato e chiusura. Un compositor fittizio
e test dedicati hanno permesso di esercitare il protocollo senza intervenire
sulla TV.

Sono emerse anche incompatibilità più piccole, ma bloccanti:

- `wl_output` v1 non invia l’evento `done` che WPE si aspettava; è stata aggiunta
  una patch per rendere utilizzabili le dimensioni ricevute con quella versione;
- alcune richieste di rilascio di puntatore, tastiera e touch non esistono nei
  seat v1; la pulizia è stata resa compatibile senza inviare richieste moderne;
- il backend Wayland non usa `wl_egl_window`, ma il linker SDK aggiungeva
  ugualmente lo stub EGL webOS, non adatto al runtime; il collegamento superfluo
  è stato rimosso e il packaging è stato configurato per rifiutare quello stub.

Ho poi creato l’app C++ con una pagina locale incorporata e lanciata tramite un
piccolo launcher ARM statico. La pagina verifica HTML, CSS, JavaScript e riceve
in teoria i tasti freccia; non include barra indirizzi né navigazione web.

## 18 novembre, notte e mattina — Le build che non entravano in memoria

La difficoltà più lunga sulla macchina di sviluppo è stata compilare WebKit
senza mettere a rischio un host condiviso. Alcune unità C++ unificate
superavano il budget disponibile; il problema non era un errore del codice,
ma il picco di memoria del compilatore.

Ho provato prima a ridurre parallelismo e consumo di GCC. Questo non è bastato:
`Document.cpp`, file molto grande del DOM, continuava a richiedere troppa
memoria, anche separandolo e provando diverse ottimizzazioni e configurazioni
di header precompilati.

La soluzione trovata è stata circoscritta a quel file:

1. escludere `Document.cpp` dalle unità unificate;
2. generare un precompiled header dedicato per il suo preambolo;
3. compilarlo con Clang 19 per ARM, mantenendo target, sysroot e ABI dell’SDK;
4. lasciare GCC per il resto di WPE e impedire che il PCH venga aggiunto per
   errore al link finale.

La compilazione mirata di `Document.cpp` è riuscita entro un limite virtuale
prudenziale. Ho integrato questo percorso negli script e aggiunto test per
verificare comando, flag, oggetto prodotto ed esclusione del PCH dal link.

La build completa è stata sorvegliata con un supervisore che controllava RAM
e swap. Una successiva unità HTML ha provocato un calo troppo rapido dello swap:
il supervisore ha fermato la build prima che la situazione peggiorasse. Ho
ridotto il costo di alcune unità e ripreso incrementalmente, inizialmente con
un solo job e priorità ridotta. Quando hai messo a disposizione più risorse,
ho proseguito con un massimo di due job e supervisione della memoria. Questi
arresti hanno allungato il lavoro, ma hanno evitato di trattare una macchina
condivisa come se fosse dedicata.

## 18 novembre, durante il giorno — Completare il motore e preparare il pacchetto

Sono state affrontate altre incompatibilità scoperte soltanto durante la
compilazione:

- Buildroot provava a collegare il backend legacy WPE FDO, non usato dal
  prototipo moderno, e una versione di Wayland incompatibile con un header
  `memfd` mancante nell’SDK; ho rimosso quel backend e dichiarato le dipendenze
  necessarie direttamente;
- le opzioni multilinea passate a Make finivano interpretate male dalla shell;
  sono state rese monoriga;
- CMake riattivava ccache e cambiava i comandi di compilazione; è stato
  disabilitato esplicitamente per rendere la build incrementale prevedibile;
- la compilazione di `libgcrypt` includeva istruzioni ARMv8 anche disattivando
  le estensioni crypto; è stata ricostruita senza assembler e verificata come
  ARMv7, accettando il possibile costo prestazionale;
- l’SDK inseriva codice `glibc-polyfills` senza licenza dichiarata. Il primo
  link di WPE conteneva il relativo marker: ho rigenerato il motore senza
  quel polyfill e aggiunto al packaging il rifiuto esplicito del marker.

WPE 2.54.2 è stato compilato e installato nell’albero target locale; anche
l’app è stata collegata. L’ABI e le dipendenze sono state controllate. Il link
WPE è stato inoltre verificato senza lo stub EGL inutilizzabile. Restano
comunque avvertenze di `legal-info` sul sorgente Buildroot e su componenti
esterni: il pacchetto è per test locale, non una release redistribuibile.

Il packaging ha richiesto prove separate perché l’installer della TV è più
vecchio degli strumenti desktop:

- BusyBox `ar` rifiutava header privi del tipo file regolare;
- il tar legacy non interpretava i percorsi estesi PAX e troncava file
  indispensabili;
- i timestamp Unix zero generavano avvisi sul tar della TV;
- includere directory condivise nell’archivio ne avrebbe alterato i permessi.

Ho corretto il formato IPK usando header `ar` compatibili, tar USTAR,
timestamp fissi e omissione delle directory condivise. Le modalità cambiate
dalla prima installazione sono state ripristinate; non era stato però annotato
il timestamp originale di due directory.

## 18 novembre sera — Installazione e diagnosi dello schermo bianco

Con la TV disponibile, l’IPK è stato installato tramite il servizio Luna di
webOS e avviato dall’application manager. Il processo dell’app è stato
osservato dentro la jail con UID 5786 e non root; anche i processi WebKit
usavano quell’identità e i gruppi previsti per accedere al compositor.

La prima esecuzione visibile mostrava una pagina bianca. Invece di attribuire
subito il problema al caricamento HTML, ho raccolto log della fixture e
ispezionato i buffer Wayland. La pagina risultava caricata, il DOM era corretto,
JavaScript era attivo e la vista era visibile e mappata. Il buffer consegnato
al compositor era però bianco.

Ho provato separatamente due alternative:

- abilitare il painting CPU non ha corretto il frame bianco;
- selezionare TextureMapper invece della composizione Skia ha prodotto un
  buffer con il gradiente, il testo e gli aggiornamenti JavaScript della
  fixture.

Il secondo risultato è stato confermato sia nella variante diagnostica sia
nell’IPK installato: ho letto e salvato il buffer condiviso dell’app e
verificato che contenesse i pixel attesi. Ho quindi impostato la selezione di
TextureMapper nel launcher di GlassGlide, senza cambiare configurazioni di
sistema o librerie del driver.

È stato necessario anche adattare WPE al driver EGL della TV, che non espone
`EGL_MESA_platform_surfaceless`. Una patch usa il display EGL predefinito solo
se è disponibile `EGL_KHR_surfaceless_context`; nella jail sono riusciti il
contesto senza superficie e un probe pbuffer.

## 18 novembre sera — Il telecomando non risponde

Tu hai confermato che la UI è visibile, ma che non compare «Tasto ricevuto».
Hai poi chiesto di poter usare la TV, autorizzando solo letture e vietando
modifiche all’applicazione in primo piano. Durante quel periodo le verifiche
sulla TV sono state in sola lettura: non ho rilanciato GlassGlide, non ho
cambiato focus e non ho simulato input. Ho consultato i metadati dei dispositivi
Wayland, senza osservare i tasti che stavi premendo.

Le letture Wayland hanno trovato tre seat attivi. Uno è identificato come
`LGE M-RCU - Builtin [0]`; gli altri metadati non sono tutti disponibili.
Un *seat* rappresenta un insieme di dispositivi di input per il compositor.
Nel codice WPE originale, ogni nuovo seat annunciato sostituiva il precedente:
il motore finiva quindi per ascoltarne uno solo. Questo rendeva plausibile
che i tasti del telecomando arrivassero da un seat non ascoltato, ma non
dimostrava ancora quale dispositivo li inviasse.

Già durante la diagnosi precedente avevo trovato anche un problema distinto:
la mappa dei tasti webOS contiene un codice `0x5030`, oltre il limite di
libxkbcommon. Senza correggere quel limite l’intera mappa veniva rifiutata.
Ho alzato il limite fino al valore necessario e verificato che un codice
superiore restasse rifiutato. Questa correzione permetteva di compilare la
mappa, ma da sola non aveva fatto funzionare le frecce.

## 19 novembre — Più tastiere e verifica sulla TV

Dopo la tua autorizzazione ho aggiunto la patch
`buildroot/patches/wpewebkit/0011-wayland-multiple-keyboards.patch`. Invece di
scegliere un seat in base a un numero o a un nome che potrebbero cambiare,
WPE continua a usare un seat principale e ascolta anche le tastiere degli altri.
La patch non estende ancora puntatore e touch ai seat aggiuntivi.

Ascoltare più tastiere ha richiesto una correzione del focus: se due seat
hanno il focus sulla stessa finestra, l’uscita di uno non deve togliere il
focus anche all’altro. La modifica tiene conto dei seat che lo conservano e
interrompe la ripetizione dei tasti quando una tastiera perde il focus o
scompare. Gestisce anche l’arrivo di nuove tastiere e la rimozione di quelle
secondarie. Se dopo l’avvio scompare il seat principale, invece di continuare
in uno stato incoerente, l’app segnala la perdita e si chiude.

Ho ricompilato WPE, preparato un nuovo IPK e registrato la patch anche nei
materiali `legal-info`. Dopo aver liberato memoria sulla TV, il pacchetto è
stato installato e avviato monitorando le risorse. **Hai confermato che frecce
e OK aggiornano la fixture**: è il primo riscontro diretto che quei tasti
funzionano in GlassGlide.

Per verificare anche casi difficili da riprodurre col telecomando ho aggiunto
un test separato: avvia WPE come utente non root su un compositor Wayland
fittizio, isolato da quello che gestisce la schermata della TV. Il test invia
tasti da più tastiere, controlla il focus condiviso e simula aggiunta,
rimozione e variazione delle capacità dei seat, inclusa la perdita del
principale. Questa verifica non sostituisce le prove con i dispositivi fisici:
non è stato registrato quale seat del telecomando abbia trasmesso i tasti.

L’IPK locale è `dist/org.glassglide.browser_0.1.0_arm.ipk`, SHA-256
`9640b4ea00f7535a5f6be6a6d91bac69a951f30a4483f0884aff742a4dbe49f9`.

## Risultato, limiti e lavoro ancora necessario

**Ottenuto**

- WPE WebKit 2.54.2 e app C++ compilati per ARMv7;
- IPK webOS installato e avviato sulla TV;
- pagina locale con HTML, CSS e JavaScript visibile dopo la correzione del
  compositore;
- frecce e OK del telecomando confermati sulla fixture;
- processi verificati non root;
- controlli di build, ABI, dipendenze e packaging, con test locali passati.

**Non ancora ottenuto**

- verifica di puntatore, altri tasti del Magic Remote e tastiera fisica;
- dimostrazione dell’isolamento effettivo della jail;
- browser con URL HTTP/HTTPS, barra di navigazione, schede e dati persistenti;
- validazione di siti, TLS, download, multimedia o prestazioni.

Fino a quando l’isolamento non sarà dimostrato, la fixture resta confinata al
contenuto locale fidato e GlassGlide non va usato per navigare il web.
