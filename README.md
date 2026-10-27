# Rubber Duck Revival

Recréation en plugin **AU / VST3** (et appli autonome) pour macOS du synthé-séquenceur
**Rubber Duck H3O+ v2.12** de *d-lusion interactive media* (1996-2000), une « virtual
bassline machine » de type TB-303 au son acid très particulier.

Le moteur sonore a été reconstitué à partir de l'exécutable d'origine et produit
**exactement les mêmes échantillons** que lui (vérifié au bit près sur les 112 patterns
fournis avec l'installeur et sur des patterns de test aléatoires). Le plugin le rend
pilotable depuis Ableton Live : automation de tous les réglages, changement de pattern,
synchro au tempo et au transport.

## État

| Étape | Statut |
|---|---|
| Analyse de l'exécutable d'origine | fait |
| Format `.h30` (v1 à v4), lecture et écriture | fait |
| Moteur sonore C++ portable | fait — identique au bit près à l'original |
| Plugin AU / VST3 / Standalone (JUCE 9) | fait — à tester dans Ableton |

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
  de l'original (enchaînement des patterns du « song » chargé).
- **Jouer au clavier ou depuis des clips MIDI** : *Source = MIDI notes*. Vélocité ≥ 100
  = accent, notes liées (legato) = slide. MIDI C1 correspond à la note « C-4 » des patterns.
- **Éditeur de pas** : glisser verticalement ou molette sur une note pour la changer,
  double-clic pour silence/note, clic droit pour « Note off », octave, effacer ;
  `AC` = accent, `SL` = slide.
- **Fichiers** : *Load .h30* charge une sauvegarde de l'original (patterns + réglages),
  *Save .h30* écrit un fichier relisible par l'original.
- **Fréquence d'échantillonnage** : exacte à 44,1 kHz. À 48 kHz ou plus, le signal est
  rééchantillonné (latence de quelques échantillons, compensée par Live).

## Organisation du dépôt

- `core/` : moteur sonore en C++ pur, sans dépendance.
- `plugin/` : plugin JUCE (processeur, interface, éditeur de pas).
- `tools/rdrender` : rendu d'un `.h30` en WAV ; `tools/rdcheck` : contrôle bit à bit.
- `tools/oracle` : banc de test qui exécute les routines de l'exécutable d'origine sous Wine.
- `tests/` : patterns synthétiques et empreintes calculées par l'original, tests du plugin.
- `docs/reverse-engineering.md` : tout ce qui a été compris du logiciel d'origine.

## Fichiers d'origine et licences

Le programme d'origine et ses patterns (`.h30`) ne sont **pas** versionnés ici (droits
d-lusion) ; pour les tests, copier le contenu de l'installeur dans `reference/` (ignoré
par git). Projet personnel, non destiné à la diffusion publique. JUCE est utilisé sous
licence AGPLv3 / JUCE 9 (gratuite pour un usage personnel).
