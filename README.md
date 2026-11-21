# GlassGlide

Prototipo di browser nativo C++/WPE WebKit per la TV webOS descritta in
[spec.md](spec.md). La [ricognizione](docs/ricognizione.md) riporta i vincoli
osservati sul dispositivo; il [diario](docs/diario-sviluppo.md) documenta le
prove di sviluppo.

## Stato e sicurezza

La build è una **anteprima offline**: usa soltanto pagine incorporate fidate e
blocca siti esterni e download. La sandbox Bubblewrap di WPE non è utilizzabile
sul kernel della TV e l'isolamento della jail webOS non è dimostrato. Non
caricare contenuti non attendibili. L'app rifiuta l'esecuzione come root.

Frecce, OK e puntatore sono stati provati sulla TV. **Pagina** passa i tasti
alla pagina; un secondo clic su **Pagina** riporta ai controlli. BACK apre
ancora la dashboard webOS e la pagina presenta ritardi da diagnosticare.
La persistenza resta da correggere: dopo un riavvio l'archivio ha acquisito
permessi non sicuri e l'app lo ha rifiutato.

## Host e target

Il risultato è sempre un IPK **ARMv7 a 32 bit**, destinato alla TV; ARM64 e
x64 indicano l'architettura dell'host di compilazione, non quella dell'app.

| Host Linux | `uname -m` | Stato |
| --- | --- | --- |
| ARM64 | `aarch64` | Build e IPK provati su Debian 13 ARM64 |
| x64 | `x86_64` | Supporto nativo predisposto; build completa ancora da verificare su host x64 |

Gli script selezionano l'SDK webOS e Clang 19 per l'host rilevato: non serve
emulare ARM64 su x64. Altre architetture e altri sistemi non sono supportati
dagli script. Debian 13 è l'ambiente di riferimento: le dipendenze di sistema
e le versioni possono differire su altre distribuzioni.

## Prerequisiti host

Servono una connessione Internet, Git, una shell POSIX e i seguenti strumenti:

- GCC e G++ nativi, GNU Make, binutils (`readelf`), `patch`, `awk`, Perl e
  utilità standard GNU (`tar`, `gzip`, `bzip2`, `xz`, `unzip`, `cpio`,
  `rsync`, `find`, `sed`, `grep` e `sha256sum`).
- `curl`, `wget`, `file`, `bc` e `dpkg-deb`; Python 3.9 o successivo con la
  libreria standard.
- Node.js 20 o successivo e Corepack. Il progetto fissa pnpm 10.15.1 e
  `@webos-tools/cli` 3.2.6 nel lockfile.
- Le librerie runtime richieste dal Clang 19 privato, tra cui libffi, libedit,
  libz3, zlib, zstd, libxml2 e libtinfo. `prepare-deps.sh` verifica che Clang
  si avvii e segnala eventuali librerie mancanti.

Su **Debian 13 ARM64 o x64**, installare i prerequisiti con:

```sh
sudo apt update
sudo apt install -y \
  bash bc binutils build-essential bzip2 ca-certificates coreutils cpio curl \
  diffutils dpkg file findutils gawk git grep gzip patch perl python3 rsync \
  sed tar unzip wget xz-utils libedit2 libffi8 libtinfo6 libxml2 libz3-4 \
  libzstd1 zlib1g nodejs node-corepack
```

Per test e installazione sulla TV servono anche `openssh-client`, accesso SSH
configurato separatamente e spazio sul dispositivo; le chiavi private non
vanno inserite nel repository.

La build di WPE è onerosa. La directory `.deps` occupava circa 13 GiB nella
build ARM64 già completata: prevedere almeno 20 GiB liberi e margine ulteriore
per aggiornamenti e file temporanei. Non è una soglia garantita. Alcune unità
C++ richiedono diversi GiB di memoria; su un host condiviso sorvegliare RAM e
swap e usare un solo job. `nice` riduce la priorità, non limita la memoria.

## Build su ARM64 e x64

Eseguire gli stessi comandi su un host **Linux ARM64 o x64 nativo**, come
utente normale e da una directory senza spazi:

```sh
git clone https://github.com/alterbang/glassglide.git
cd glassglide
uname -m

tools/prepare-deps.sh
nice -n 10 tools/build-wpe.sh
nice -n 10 tools/build-app.sh
nice -n 10 tools/build-wpe.sh legal-info

COREPACK_HOME="$PWD/.deps/corepack" corepack prepare pnpm@10.15.1 --activate
COREPACK_HOME="$PWD/.deps/corepack" corepack pnpm install --frozen-lockfile \
  --ignore-scripts --store-dir "$PWD/.deps/pnpm-store"
python3 -m tools.package
```

Il primo script scarica SDK, Buildroot, WPE e Clang in `.deps/`, verificandone
i checksum. Buildroot scarica altre dipendenze durante la compilazione. Il
compilatore dell'SDK genera ARMv7 EABI `softfp`; Clang viene usato per una
sola unità WPE particolarmente costosa. La toolchain, le librerie e i binari
generati non vengono installati sul sistema della TV.

Per default WPE viene compilato con un job. Su una macchina dedicata con
memoria sufficiente si possono usare due job:

```sh
GLASSGLIDE_BUILD_JOBS=2 nice -n 10 tools/build-wpe.sh
```

Il pacchetto risultante è
`dist/org.glassglide.browser_0.1.0_arm.ipk`. Si può verificare l'architettura
del binario dell'app con:

```sh
readelf -h build/glassglide | grep -E 'Class:|Machine:'
```

L'output atteso contiene `ELF32` e `ARM`, anche su host x64. Il packaging
controlla ABI, dipendenze e librerie private senza sostituire quelle grafiche
LG. Prima di distribuire l'IPK, esaminare `legal-info`: restano avvertenze
sulle licenze dell'SDK, quindi la build è una prova locale.

Non condividere o spostare `.deps/` e `build/` tra host, architetture o
percorsi: l'SDK viene adattato al percorso locale e Buildroot genera strumenti
per l'host. Usare un clone e una build puliti per ogni macchina. Gli script
rifiutano una directory di dipendenze riconosciuta come appartenente a
un'altra architettura.

## Test

I test locali usano il compilatore nativo; alcuni richiedono l'SDK e le
dipendenze già preparati:

```sh
python3 -m unittest discover -s tests -v
```

Il test opzionale multi-seat usa un compositor fittizio sulla TV senza cambiare
l'app in primo piano. Richiede IPK installato, SSH root configurato e memoria
disponibile. `TV_SSH_ALIAS` deve essere configurato in SSH con la chiave:

```sh
GLASSGLIDE_WPE_TEST_TARGET=root@TV_SSH_ALIAS \
  python3 -m unittest discover -s tests -p test_wpe_multiseat.py -v
```

La build non installa né avvia l'app sulla TV. Prima di farlo verificare che
la TV sia libera e monitorare memoria e swap; non chiudere altre app né
modificare firmware, servizi o swap per farle spazio.
