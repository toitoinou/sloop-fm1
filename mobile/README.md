# SLOOP Touch (mobile)

Une appli de groovebox tactile pour Android (puis iOS) construite sur le moteur audio de SLOOP : les mêmes
moteurs de synthèse, kits, effets et séquenceur que le FM-1, avec une interface pensée pour le doigt.

État : **squelette**. Le moteur tourne, la Session (clips et scènes), la grille de batterie / le piano roll,
les pads et les potards fonctionnent ; il manque encore beaucoup de choses (voir la feuille de route).

## Ce qu'il y a dans ce dossier

| Dossier | Contenu |
| --- | --- |
| `mockup/` | La maquette HTML jouable qui sert de référence pour l'interface (gestes, disposition, styles). Ouvre `mockup/sloop-touch.html` dans un navigateur ; on la fait évoluer pour essayer des idées avant de les coder en C++. |
| `engine/` | Le moteur de SLOOP sous forme de bibliothèque C (`sloop_engine.h`), autour de `firmware/host/preview.c`, l'adaptateur que l'aperçu du web editor utilise déjà. Aucun fichier du firmware n'est modifié. |
| `tools/gen_engine_tables.sh` | Génère les tables du moteur (formes d'onde, échantillons, kits, patchs FM6) avec les générateurs du firmware, dans `engine/gen/`. |
| `app/` | L'appli JUCE 8 (C++17) : `CMakeLists.txt` pour le bureau, `SloopTouch.jucer` pour Android. |

## Architecture

```
UI JUCE (thread des messages)                 thread audio
  Session / Grille / Pads / Potards
        │ gestes                                 AudioEngine (AudioIODeviceCallback)
        ▼                                          │ applique les commandes entre deux blocs
  Controller ──► file sans verrou (commandes) ──► │ clips en attente de la mesure, chaînes,
        ▲                                          │ probabilités et conditions des pas
        └──── atomics (clip joué, position) ◄──── │ sloop_engine (C) → 44,1 kHz → rééchantillonné
```

- Le moteur de SLOOP joue chaque piste en boucle sur ses 64 pas. La Session vit dans l'appli : le thread audio
  écrit les quelques pas juste devant la tête de lecture, pris dans le clip qui doit jouer là (clip lancé à la
  mesure, chaîne, arrêt), avec ses probabilités et ses conditions (1:2, 2:2, 1:4, Fill, Sans fill) tirées pour ce
  tour. Un clip peut donc avoir n'importe quelle longueur (1 à 64 pas), une chaîne peut mélanger les longueurs,
  et une modification s'entend tout de suite.
- L'interface ne touche jamais au moteur : elle poste des commandes, le thread audio les applique.
- Pistes de l'appli : DR (la piste batterie de SLOOP, n° 3) puis T1–T3 (ses parties synthé 0–2).

## Compiler et lancer sur ordinateur (Linux, macOS, Windows)

```
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git mobile/app/JUCE
cmake -S mobile/app -B build-mobile -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-mobile
```

Sans `mobile/app/JUCE`, CMake télécharge JUCE 8.0.4 tout seul. Le moteur se compile avec clang (les tests du
firmware notent que GCC ne termine pas la compilation de l'adaptateur). Sous Linux il faut les paquets de
développement d'ALSA, X11, Freetype et Fontconfig (voir `.github/workflows/mobile.yml`).

L'appli a deux modes en ligne de commande, utilisés par la CI :

- `--selftest` : fait tourner le moteur sans carte son et vérifie le séquencement (lancement à la mesure,
  chaînes, arrêt, son non nul) ;
- `--snapshot sortie.png 844x390 [session|grid|pads|soundN|trackN]` : dessine l'écran dans une image, pour
  regarder la disposition sans appareil.

## Les projets

Le bouton **Projet** (à côté des onglets) ouvre le panneau des projets :

- **Enregistrer** garde la session sous le nom tapé, dans la mémoire de l'appli ; la liste montre les projets
  enregistrés, le plus récent en haut (touche une fois pour choisir, une deuxième fois pour ouvrir ; ✕ deux fois
  pour supprimer) ;
- **Nouveau** vide la session (deuxième tap pour confirmer) ;
- **Exporter…** et **Importer…** passent par le sélecteur de fichiers du système (sur Android : Téléchargements,
  Drive, une clé USB…) : un projet est un fichier `.sloop`, du JSON lisible ;
- **DAW…** exporte la session en [DAWproject](https://github.com/bitwig/dawproject) pour la finir dans Bitwig
  Studio, Studio One, Cubase… : une piste MIDI par piste, les scènes dans le lanceur de clips (s'il y en a un) et
  aussi à la suite dans l'arrangement. Les notes telles qu'écrites (les probabilités et les conditions 1:2, 1:4
  jouent toutes, les pas « Fill » seuls sont laissés de côté), la batterie sur les notes General MIDI (kick 36,
  snare 38…). Pas de son : ce sont les instruments du DAW qui jouent. Le fichier suit le schéma officiel (vérifié
  avec `xmllint --schema Project.xsd`) ;
- **Réglages** : la tonalité et la gamme, le swing, le métronome, le décompte, la quantification de
  l'enregistrement, le tampon audio ;
- la session en cours est **gardée toute seule** (quand l'appli passe en arrière-plan, en la quittant, et toutes
  les 30 secondes) et rouverte au prochain lancement.

Un projet garde la session (clips, scènes, pas, conditions, p-locks), le tempo, le swing, la tonalité, le son de
chaque piste et tous ses paramètres. Les fichiers sont dans `userApplicationDataDirectory/SLOOP Touch/` (le dossier privé de l'appli
sur Android, `~/.config/SLOOP Touch/` sous Linux).

## Le son et la latence

L'appli prend la sortie **Oboe** de JUCE (AAudio, ou OpenSL ES sur les vieux téléphones), le chemin à faible
latence d'Android, avec un petit tampon. La ligne grise à droite des onglets montre la sortie, la taille du tampon
et la latence estimée (`Oboe · 192 · ≈ 8,0 ms`) : **touche-la pour essayer la taille suivante**, et garde la plus
petite qui ne craque pas sur ton téléphone. L'APK de la CI est une version Release (optimisée) : une version Debug
calcule bien plus lentement et oblige à de plus gros tampons.

## Android

JUCE ne sait pas faire d'Android avec CMake : on passe par Projucer, qui génère un projet Gradle.

```
mobile/tools/gen_engine_tables.sh
Projucer --resave mobile/app/SloopTouch.jucer       # -> mobile/app/Builds/Android
cd mobile/app/Builds/Android && ./gradlew assembleRelease     # (signée avec la clé de debug d'Android)
```

Il faut le SDK Android avec le NDK `26.2.11394342` et CMake `3.22.1` (les versions que JUCE 8.0.4 écrit dans le
projet). On peut aussi ouvrir `mobile/app/Builds/Android` dans Android Studio.

**La CI fait tout ça** : à chaque push qui touche `mobile/`, le workflow *Mobile app* construit l'APK (Release)
et des captures d'écran, à télécharger dans l'onglet *Actions* de GitHub (section *Artifacts* du run).

## Feuille de route

- [ ] Mesurer la latence et la charge CPU sur de vrais téléphones (Oboe, tampon réglable à l'écran).
- [x] Choisir le son de chaque piste : touche le nom du son en haut des potards (les 12 moteurs et leurs presets, les kits).
- [x] Tous les paramètres du son, page par page : Mix, Macro (les quatre principaux du moteur), ses deux pages
  d'édition (OSC, FLT…), l'enveloppe, les envois d'effets. Un pas maintenu verrouille n'importe lequel (p-lock).
- [x] Enregistrer en jouant sur les pads (Rec pendant la lecture, au pas le plus proche).
- [x] Longueur de chaque clip (1 à 64 pas, bouton « 16 pas » de la grille), doubler un clip, boucles décalées.
- [x] Annuler / refaire (les flèches), copier / coller un clip (appui long dans la Session).
- [x] Métronome (Clic), décompte d'une mesure quand on enregistre à l'arrêt, swing, quantification de
  l'enregistrement (sur la grille, ou à sa place entre les pas), « Quantifier » un clip.
- [x] Tonalité et gamme (11 gammes) ; les Pads d'une piste synthé deviennent un clavier dans la gamme, en quartes,
  les toniques allumées, avec Oct -/+ et un mode Accords (la triade de la gamme sur chaque pad).
- [x] Export DAWproject (Bitwig, Studio One, Cubase…).
- [x] Les moteurs ont jusqu'à 16 réglages (4 pages d'édition, P_E0..P_E15) : le moteur ne suit plus le FM-1
  (son firmware n'est plus la référence, seulement la base de départ).
- [x] **ANALOG** refait façon NoiseMaker : deux oscillateurs chacun sa forme, OCT2, SYNC, sous-oscillateur,
  bruit ; filtre LP24 (échelle à la Moog, saturée) ou LP12 / BP / HP ; sa propre enveloppe de filtre (F.ATK
  F.DEC F.SUS F.AMT). 29 presets (les anciens refaits, plus SAW+SUB BS, SYNC LEAD, JUNO PAD, POLY BRASS, SQR SYNC
  BS, RESO SWEEP, HOOVER). Les projets enregistrés avec l'ancien ANALOG sonnent autrement.
- [x] Un 14ᵉ moteur à l'essai, **OBXD** : la voix d'Obxd (l'émulation d'Oberheim OB-X de Vadim Filatov), ses deux
  oscillateurs (scie, pulse ; sync, cross-mod), son filtre multimode 12 dB / 24 dB et son enveloppe de filtre ;
  12 presets. Licence à éclaircir avant une publication (`third_party/obxd/README.md`).
- [x] Une page **Mod** par piste synthé, stéréo, après le filtre de piste : les chorus du Juno-60 (I, II, I+II) et
  un vibrato façon Boss VB-2 (RATE, DEPTH : jusqu'où va la hauteur, RISE : sa montée après une note).
- [x] La fiche Son par catégorie (Basses, Leads, Nappes, Claviers, Cloches, Plucks, Stabs, Percus, Textures), de
  tous les moteurs, avant les moteurs eux-mêmes.
- [x] Un 13ᵉ moteur, **PLAITS** : le Plaits de Mutable Instruments (MIT, `third_party/plaits`), ses 24 modèles
  (analogique, FM, wavetable, accords, voix, cordes, percussions…) réglés par MODEL, HARM, TIMB, MORPH, plus son
  low-pass gate et son enveloppe ; 27 presets. Quatre voix par piste au plus (le processeur).
- [ ] L'écran FX (surface XY, les 27 effets punch-in de SLOOP).
- [x] Enregistrer ses sessions : projets dans l'appli, sauvegarde automatique, export / import `.sloop`.
- [x] Notes tenues dans la vue Notes : glisser une note l'allonge (elle tient, au lieu d'être rejouée à chaque pas).
- [x] Huit pistes (DR et T1..T7 : le moteur prend son nombre de parties à la compilation, 32 voix ensemble).
- [x] Les mouvements de boutons enregistrés (REC : un bouton tourné s'écrit sur les pas qu'il traverse).
- [x] Note repeat sur les pads (1/4 à 1/32, triolets), calé sur le tempo, enregistré.
- [x] Un mixeur (volume, pan, envois, mute, solo, vumètres ; le master : volume, filtre DJ, DUST, DUCK).
- [x] Enregistrer la sortie en WAV (REC du master dans le mixeur), puis la partager.
- [x] MIDI : les entrées (USB, Bluetooth après appairage) jouent la piste choisie et s'enregistrent, CC 21-24 /
  70-73 sur les boutons, START / STOP ; l'horloge MIDI en sortie (Réglages).
- [x] Ableton Link (Réglages) : le tempo, la mesure et Play / Stop partagés avec les apps du même Wi-Fi (Live, Bitwig,
  Koala, G-Stomper…) ; Play attend la mesure suivante de la session. `mobile/tools/get_deps.sh` récupère Link (GPL-2.0+)
  dans `mobile/third_party/link`.
- [x] Une piste audio (AU, la dernière colonne de la session) : un son importé (WAV, AIFF, FLAC, Ogg, MP3, ou un
  enregistrement de l'app) par ligne, son début et sa fin (le fichier n'est pas touché), son tempo à la main ou au TAP
  en l'écoutant ; il joue autant de temps du morceau, étiré (Signalsmith Stretch, la hauteur gardée) ou accéléré comme
  un disque, lancé à la mesure comme les autres clips ; sa tranche dans le mixer ; dans l'export DAWproject.
- [ ] L'export audio piste par piste.
- [ ] MIDI (USB, Bluetooth) et lien avec le FM-1.
- [ ] Une piste guitare (NAM / TONE3000).

## Licence

GPL-3.0-only, comme SLOOP et Felucca. JUCE est utilisé sous sa licence AGPLv3, compatible avec le GPL-3.0.
