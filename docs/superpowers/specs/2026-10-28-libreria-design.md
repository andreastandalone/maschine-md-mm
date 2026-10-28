# Libreria pack per Maschine MD-MM

Data: 2026-10-28. Branch: `dev`. Riguarda solo il prodotto combinato Maschine MD-MM
(plugin VST3/AU e standalone).

## Perché

Andrea vuole esplorare velocemente i sound pack per Machinedrum e Monomachine
(kit, pattern, campioni) senza passare dai menu della macchina e senza perdere
quello che c'è già dentro.

Oggi, se carica un `.syx`, ogni kit, pattern e song va nello slot scritto nel file.
Tutti i pack scrivono a partire dallo slot 0, quindi l'ultimo caricato sovrascrive
i precedenti e i dati di fabbrica. Esempio reale: MD909UW (13 kit negli slot 0-44
e tutti i 128 pattern) ha coperto MD808 e Deep Rhythms. In più, dentro il plugin
non esiste un modo per caricare sysex: il menu File c'è solo nella standalone.

## Obiettivi

- Un pulsante **LIBRERIA** nella UI del plugin apre e chiude una finestra Libreria.
- La Libreria elenca i pack presenti in una cartella fissa e mostra il contenuto di
  ognuno: kit e pattern con il loro nome reale, letto dai file.
- **Suona kit**: carica il kit scelto sul pattern che la macchina sta suonando.
- **Suona pattern**: carica il pattern del pack insieme al suo kit.
- **Importa**: copia kit e pattern nei primi slot liberi, così restano nella macchina.
- I pack con **campioni** (MD UW) funzionano fin dalla prima versione: sono la parte
  che interessa di più ad Andrea.
- Ascoltare non deve mai far perdere nulla. Se Andrea chiude la Libreria senza
  importare, la macchina torna esattamente com'era, campioni compresi.

## Fuori dallo scopo (per ora)

- Navigare la Libreria dalla MK3: per ora si usano mouse e frecce della tastiera.
- Spostare i campioni importati in slot campione liberi: all'importazione i campioni
  vanno negli slot pensati dal pack, con un avviso.
- Caricare per intero progetti completi (global, song, 128 kit): non si fa mai.
- Salvare sysex e lo stato di partenza: andranno sotto lo stesso pulsante in un lavoro
  successivo, ma non fanno parte di questo.

## Cose da non sbagliare

- Sovrascrivere senza avviso dati che Andrea non ha scelto di sostituire.
- Lasciare la macchina in uno stato diverso da prima dopo un ascolto annullato.
- Far finire nella lista di una macchina dati destinati all'altra.

## Decisioni prese con Andrea

**Cartella.** Una cartella fissa, `Documenti/Gearmulator Preview/Library`, accanto
alle cartelle `Machinedrum` e `Monomachine` che contengono i firmware. Il plugin la
crea se non esiste. Andrea ci copia i pack così come li scarica.

**Finestra.** Si apre solo premendo il pulsante LIBRERIA, che la chiude anche.
Impaginazione a due colonne (opzione B del mockup):

- in alto si sceglie la macchina, **MD** o **MM**;
- colonna sinistra: i pack;
- colonna destra: il contenuto del pack scelto (kit e pattern, con il numero di
  campioni che ogni kit usa);
- in basso: **SUONA KIT**, **SUONA PATTERN**, **IMPORTA** e una riga che dice quali
  slot campione verranno sostituiti.

Stile minimale, come un display Elektron: fondo scuro, testo chiaro maiuscolo in
monospace, selezione in negativo. Non va sacrificato spazio: la finestra può essere
grande e coprire buona parte del pannello. Il mockup approvato è in
`.superpowers/brainstorm/` (opzione B, `library-layout.html`).

**Chiusura senza importare.** La macchina torna al kit e al pattern di prima.

**Cosa copia un IMPORTA.** La voce selezionata: un kit, oppure un pattern con il suo
kit e i suoi campioni. Nella colonna destra si possono selezionare più voci con
Cmd/Shift+clic, come nel Finder, e Cmd+A seleziona tutto il pack. IMPORTA copia tutte
le voci selezionate in un colpo.

**Quando gli slot liberi non bastano.** Prima si usano gli slot vuoti. Se non
bastano, la Libreria apre un selettore di slot nello stesso stile, con l'elenco degli
slot e il nome di cosa contengono (es. `KIT 12 · TR909 HEAVY`). Andrea sceglie lo slot
di partenza; più voci vanno negli slot successivi. Prima della conferma una riga dice
cosa verrà sostituito (es. `SOSTITUISCE KIT 12-14`). Il plugin non decide mai da solo
cosa sacrificare, nemmeno tra contenuti di fabbrica.

## Decisioni mie (da verificare durante la costruzione)

**Una cartella per tutte e due le macchine.** Il plugin legge l'intestazione di ogni
messaggio sysex (byte modello `0x02` = MD, `0x03` = MM; i messaggi SDS sono solo MD
UW) e mette ogni contenuto sotto la macchina giusta, indipendentemente dalla
cartella. Esempio reale: i tre file in `MD/Booster-Pack` sono dati MM.

**Pack = cartella o file `.zip`.** Il plugin legge sia cartelle estratte sia zip così
come vengono scaricati. Un pack è una cartella di primo livello o uno zip dentro
`Library`. Nella colonna destra compaiono i kit e i pattern di tutti i `.syx` del
pack. I file che non si riescono a interpretare si saltano senza bloccare la lista.

**Ascolto non distruttivo con una "foto".** All'apertura della Libreria il plugin
salva lo stato completo della macchina (lo stesso che salva nel progetto della DAW:
`getStateInformation` del processor figlio). Alla chiusura senza importazione lo
ripristina. Dopo un'importazione la foto viene scartata. Così l'ascolto può usare
qualsiasi slot, anche quelli dei campioni, senza rischi.
Da verificare: quanto dura il ripristino e se produce un breve silenzio. Se il
ripristino fa ripartire la macchina, è accettabile, ma il tempo va misurato e
riportato ad Andrea.

**Importare dopo aver ascoltato.** IMPORTA prima ripristina la foto, poi scrive solo
le voci importate. Così nessun avanzo dell'ascolto (slot di prova, campioni di altri
pack) resta nella macchina senza avviso. Il ripristino può far ripartire la
macchina; la Libreria resta aperta e scatta una nuova foto dopo l'importazione.

**Stato salvato dalla DAW mentre la Libreria è aperta.** Finché la Libreria è aperta,
`getStateInformation` del plugin restituisce la foto, non lo stato di ascolto. Un
salvataggio o autosalvataggio di Ableton non registra mai dati di prova. Chiudere
l'editor del plugin equivale a chiudere la Libreria senza importare. Le modifiche
fatte sulla macchina durante l'ascolto si perdono al ripristino: è coerente con
"se non importo niente torna a quello di prima".

**Suona e sequencer fermo.** SUONA KIT e SUONA PATTERN caricano e, se il sequencer
della macchina è fermo, lo avviano, così il risultato si sente. Alla chiusura senza
importare la foto ripristina anche lo stato di play.

**Pack con sottocartelle.** Ogni cartella che contiene direttamente dei `.syx` è un
pack a sé nella colonna sinistra, con il percorso come nome (es.
`DEEP RHYTHMS / DUB1`). La regola "un kit usa i campioni del suo pack" vale quindi
per sottocartella: i campioni di `dub1` non si mescolano con quelli di `dub2`.

**Progetti completi.** I dump di progetti completi (global, song, 128 kit) compaiono
come elenco dei loro kit e pattern, da ascoltare e importare uno alla volta. Global e
song non si mandano mai.

**Slot di prova.** Per ascoltare, kit e pattern vanno in uno slot di prova fisso:
l'ultimo slot kit e l'ultimo slot pattern della macchina. Poi il plugin manda i
comandi sysex "load kit" (`$58`) o "load pattern" (`$57`), documentati in entrambi i
manuali, così il risultato suona subito. Grazie alla foto, lo slot di prova non deve
essere vuoto.

**Riscrivere lo slot nei messaggi.** Per mandare un kit o un pattern in uno slot
diverso da quello del file, il plugin riscrive nel messaggio lo slot di origine e
ricalcola checksum e lunghezza. Se sposta un kit, aggiorna anche il riferimento al
kit nei pattern che manda insieme.

**Campioni.** Per un kit che usa campioni, il plugin manda prima i campioni del pack
(messaggi SDS e, se ci sono, i messaggi Elektron `0x73` con il nome del campione),
negli slot originali, poi il kit. Dopo la chiusura della Libreria la foto li
rimette com'erano. Un campione già presente e identico non viene rimandato. Il
trasferimento dura alcuni secondi: la Libreria mostra l'avanzamento.
Da capire durante la costruzione: come collegare un kit ai campioni che usa. I pack
reali sono organizzati in modi diversi: Deep Rhythms ha un file per campione
(`01_DUB1_BD01.syx`) accanto al file con kit e pattern; MD909 ha file campione
separati per slot (`09CH-SLOT1.syx`) e un file con kit e pattern. La regola semplice
di partenza: un kit di un pack usa tutti i campioni dello stesso pack.

**Monomachine.** L'MM accetta kit e pattern via sysex solo quando è nella schermata
di ricezione (GLOBAL > FILE > SYSEX RECV, "WAITING"); il plugin oggi lo verifica già
prima di inviare un file. La Libreria porta da sola l'MM in quella schermata
premendo i tasti virtuali del pannello, manda i dati, e torna indietro. Se non ci
riesce, chiede ad Andrea di aprire la schermata a mano. I comandi load kit e load
pattern dell'MM vanno verificati sul firmware emulato.

**Slot libero.** Un kit o un pattern vuoto, cioè cancellato. Il modo per leggere
quali slot sono vuoti e i nomi dei kit va scelto durante la costruzione (richiesta
sysex alla macchina o lettura dello stato emulato).

**Infrastruttura esistente da riusare.** Il progetto sa già leggere, validare e
inviare file sysex alla macchina emulata, campioni SDS e TurboMIDI compresi
(`source/elektron/md/mdLib/mdsysexfile.h`, `mdsysextransfer.h`, i metodi
`*UserSysex*` di `md::Device` e di `mdJucePlugin::Editor`). Nota: il validatore di
oggi rifiuta i messaggi di comando come `$57`/`$58`, perché accetta solo dati; i
comandi di caricamento vanno mandati per un'altra strada.

**Dove sta il pulsante.** Nel `CombinedEditor`
(`source/elektron/md/mdJucePlugin/mdCombinedEditor.cpp`), che contiene le UI delle
due macchine. Nel plugin oggi non ci sono menu: la standalone ha un menu File nella
barra dei menu di macOS, che nel plugin non esiste.

## Ordine di lavoro

1. Libreria per il Machinedrum: elenco, ascolto di kit e pattern, campioni, foto e
   ripristino.
2. Estensione al Monomachine.
3. Importazione negli slot liberi.

Ogni passo si chiude con una prova di Andrea in Ableton e un commit su `dev`.

## Lasciato a chi costruisce

- Struttura del codice e posizione dei nuovi file.
- Come rendere la finestra (RmlUi come il resto della UI o componenti JUCE), purché
  rispetti lo stile approvato.
- Formato esatto dei messaggi kit e pattern (posizione del nome, dello slot, del
  riferimento al kit), da ricavare dai manuali in `_handoff/` (cartella locale non
  versionata) e dai file reali in `_handoff/Kits`.

## Pack reali per le prove

In `_handoff/Kits` (locale, non versionato):

| File | Contenuto |
|---|---|
| `MD/MD909/MD909UW.syx` | 13 kit MD (slot 0-44), 128 pattern |
| `MD/MD909/09CH-SLOT1.syx` ... `09CC-SLOT4.syx` | campioni SDS |
| `MD/MD808/MD808_01.syx` | 1 kit, 16 pattern |
| `MD/Deep-Rhythms/SysEx/dub1/DUB1_SONG_PAT_KIT.syx` | 2 kit, 4 pattern, 1 song |
| `MD/Deep-Rhythms/SysEx/dub1/01_DUB1_BD01.syx` ... | 1 nome campione `0x73` + campione SDS ciascuno |
| `MD/Booster-Pack/SysEx/*.syx` | progetti MM completi |
| `MM/Nicholas-Lem-Collection/.../*.syx` | 16 kit MM, 16 pattern |
| `MM/Immortal-Waves`, `MM/Oxford-Overdrive` | progetti MM completi |
