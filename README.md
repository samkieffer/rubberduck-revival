# Rubber Duck Revival

Recréation en plugin **AU / VST3** (et appli autonome) pour macOS du synthé-séquenceur
**Rubber Duck H3O+** de *d-lusion interactive media* (1996-2000), une « virtual
bassline machine » de type TB-303 au son acid très particulier.

Le plugin contient **deux moteurs**, au choix (réglage *Engine*) :

- **v1.02 (1997)**, celui de `RUBDUCK.EXE` : blocs de 64 échantillons, filtre à deux
  étages dont la coupure et la résonance suivent l'enveloppe, distorsion à courbe
  logarithmique, 14 patterns ;
- **v2.12 (2000)**, celui de `rdh3o.exe` : filtre à deux branches avec Squelch, Drive,
  Tweak et Tweet, 16 patterns et « song ».

Chaque moteur a été reconstitué à partir de l'exécutable d'origine et produit
**exactement les mêmes échantillons** que lui (vérifié au bit près sur les patterns
fournis avec l'installeur et sur des patterns de test aléatoires). Le plugin les rend
pilotables depuis Ableton Live : automation de tous les réglages, changement de pattern,
synchro au tempo et au transport.

## État

| Étape | Statut |
|---|---|
| Analyse des exécutables d'origine (v1.02 et v2.12) | fait |
| Format `.h30` (v1 à v4), lecture et écriture | fait |
| Moteurs sonores C++ portables v1.02 et v2.12 | fait — identiques au bit près aux originaux |
| Plugin AU / VST3 / Standalone (JUCE 9) | fait — testé dans Ableton Live 12 (v2.12) |

## Compiler et installer sur Mac

Prérequis : outils Xcode (`xcode-select --install`) et CMake (`brew install cmake`).

```sh
git clone https://github.com/samkieffer/rubberduck-revival
cd rubberduck-revival
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

La compilation télécharge JUCE puis copie automatiquement le plugin dans
`~/Library/Audio/Plug-Ins/Components` (AU) et `~/Library/Audio/Plug-Ins/VST3` (VST3).
L'appli autonome se trouve dans `build/plugin/RubberDuckRevival_artefacts/Release/Standalone/`.

Sans compiler : chaque build de la CI GitHub (onglet *Actions*, artefact
`RubberDuckRevival-macOS`) contient l'AU, le VST3 et l'appli. Après décompression,
retirer la quarantaine de macOS avant de copier les plugins :
`xattr -dr com.apple.quarantine "Rubber Duck Revival.component" "Rubber Duck Revival.vst3"`.

Dans Ableton Live : Réglages → Plug-ins → activer « Use Audio Units v2 » et/ou
« Use VST3 Plug-in System Folders », puis « Rescan ». Le plugin apparaît sous
*Rubber Duck Revival* comme instrument.

Vérifier que la compilation reproduit l'original au bit près :

```sh
(cd build && ctest --output-on-failure)
# avec les patterns de l'installeur en plus :
cmake -S . -B build -DRD_CONTENT_DIR="/chemin/vers/RubberDuck_H3O_2.12/content" && (cd build && ctest)
```

## Utilisation dans Ableton Live

- **Moteur** (*Engine*) : v1.02 (par défaut pour une nouvelle instance) ou v2.12. Les
  réglages sont communs ; ceux qui n'existent pas en v1.02 (Squelch, Drive, m, Tweak,
  Tweet, onde triangle, patterns E et F) sont grisés. En v1.02, *Dist Gain* règle la
  part de distorsion et *Dist Drive* la courbe. Les projets enregistrés avant l'ajout du
  moteur v1.02 se rouvrent en v2.12.
- **Séquenceur** : les patterns tournent avec le transport de Live, au tempo de Live
  (un pas = une double-croche, 16 pas par mesure).
- **Automation** : tous les boutons (Cut Off, Resonance, Squelch, Drive, Tweak, Tweet,
  delay, distorsion…) sont des paramètres automatisables, avec les mêmes plages 0–255
  que l'original. Comme dans l'original, ils sont pris en compte toutes les 2,9 ms.
- **Changer de pattern** :
  - paramètre automatisable *Pattern* (1 à 16) ;
  - ou notes MIDI **C2 à D#3** dans un clip (C2 = pattern 0, … D#3 = pattern F) ;
  - ou Program Change 0–15.
  Le changement se fait à la mesure suivante, au pas suivant ou immédiatement
  (réglage *Pattern change*). *Play mode* reprend les modes Single / Sequence / Shuffle
  de l'original : en v2.12, enchaînement des patterns du « song » chargé ; en v1.02,
  patterns de *Range from* à *Range to*, dans l'ordre ou au hasard.
- **Jouer au clavier ou depuis des clips MIDI** : *Source = MIDI notes*. Vélocité ≥ 100
  = accent, notes liées (legato) = slide. MIDI C1 correspond à la note « C-4 » des patterns.
- **Éditeur de pas** : glisser verticalement ou molette sur une note pour la changer,
  double-clic pour silence/note, clic droit pour « Note off », octave, effacer ;
  `AC` = accent, `SL` = slide.
- **Saisie au clavier** (façon tracker) : cliquer une note pour y placer le curseur, puis
  taper. Les touches sont prises à leur *position* (même place en AZERTY et en QWERTY) :
  rangée du bas (Z S X D C V G B N H J M en QWERTY, W S X D C V G B N H J , en AZERTY)
  = octave choisie par *Keys Oct*, rangée du haut (Q 2 W 3 E… / A é Z " E…) = octave
  au-dessus. Touche 1 (`&`) = note off, Retour arrière = silence, A (`Q` en AZERTY) =
  accent, F = slide, flèches = déplacer le curseur. L'infobulle de la grille rappelle les
  touches de la disposition active. Dans Live, désactiver le clavier MIDI de l'ordinateur
  (touche M) si des touches n'arrivent pas au plugin.
- **Outils de séquence** (pattern affiché, ou les deux en 32 pas) : *Shift* ← → décale
  tous les pas d'un cran en boucle (⇧← ⇧→), *Transpose* −12 −1 +1 +12 (⇧↑ ⇧↓ pour le
  demi-ton, ⌥↑ ⌥↓ pour l'octave ; refusé si une note sortait de la tessiture),
  *Undo* / *Redo* (⌘Z, ⇧⌘Z), *Copy* / *Paste* (⌘C, ⌘V, aussi d'une instance à l'autre).
- **32 pas** : en mode *Single*, le pattern choisi est joué avec son voisin (0+1, 2+3…),
  un par mesure, calé sur les mesures impaires/paires de Live ; les deux sont affichés
  l'un sous l'autre. Les fichiers `.h30` restent au format d'origine (deux patterns) et
  le son est identique à celui de l'original jouant ces deux patterns à la suite.
- **Fichiers** : *Load .h30* charge une sauvegarde de l'original (patterns + réglages)
  et choisit le moteur qui l'a écrite (fichiers de version 1–2 : v1.02, versions 3–4 :
  v2.12). Pendant la lecture, le plugin passe aussitôt au pattern de départ du fichier,
  sans attendre la mesure. *Save .h30* écrit un fichier relisible par l'original du
  moteur choisi.
- **Version** : affichée sous le titre (`v0.2.0`…), pour savoir quelle compilation Live a chargée.
- **Fréquence d'échantillonnage** : exacte à 44,1 kHz. À 48 kHz ou plus, le signal est
  rééchantillonné (latence de quelques échantillons, compensée par Live).

## Organisation du dépôt

- `core/` : moteur sonore en C++ pur, sans dépendance.
- `plugin/` : plugin JUCE (processeur, interface, éditeur de pas).
- `tools/rdrender` : rendu d'un `.h30` en WAV ; `tools/rdcheck` : contrôle bit à bit.
- `tools/oracle` : bancs de test qui exécutent les routines des exécutables d'origine sous Wine.
- `tests/` : patterns synthétiques et empreintes calculées par l'original, tests du plugin.
- `docs/reverse-engineering.md` : tout ce qui a été compris du logiciel d'origine.

## Fichiers d'origine et licences

Les programmes d'origine et leurs patterns (`.h30`) ne sont **pas** versionnés ici (droits
d-lusion) ; pour les tests, copier le contenu de l'installeur dans `reference/` (ignoré
par git). Projet personnel, non destiné à la diffusion publique. JUCE est utilisé sous
licence AGPLv3 / JUCE 9 (gratuite pour un usage personnel).
