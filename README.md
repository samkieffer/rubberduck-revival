# Rubber Duck Revival

Recréation en plugin **AU / VST3** (et appli autonome) pour macOS du synthé-séquenceur
**Rubber Duck H3O+ v2.12** de *d-lusion interactive media* (1996-2000), une « virtual
bassline machine » de type TB-303 au son acid très particulier.

Objectif : retrouver **le même son** que le logiciel d'origine (et non une 303 générique),
en reproduisant fidèlement son moteur sonore à partir de l'analyse de l'exécutable, puis
le rendre pilotable depuis Ableton Live : automation de tous les réglages (squelch, drive,
tweak, tweet, delay, distorsion…), changement de pattern, synchro au tempo.

## État d'avancement

| Étape | Statut |
|---|---|
| Analyse de l'exécutable d'origine (décompilation) | fait |
| Décodage du format de fichier `.h30` (v1 à v4) | fait |
| Banc de comparaison avec l'original (Wine) | fait |
| Moteur sonore C++ portable + rendu WAV | fait — **identique au bit près à l'original** sur les 112 patterns fournis |
| Plugin AU / VST3 (JUCE) | en cours |

## Organisation du dépôt

- `docs/` : notes d'analyse du logiciel d'origine (format `.h30`, moteur sonore).
- `core/` : moteur sonore en C++ pur, sans dépendance.
- `tools/rdrender` : rendu d'un `.h30` en WAV.
- `tools/rdcheck` : vérifie qu'une compilation reproduit l'original au bit près.
- `tools/oracle` : banc de test qui exécute les routines de l'exécutable d'origine sous Wine.
- `plugin/` : enveloppe JUCE pour AU / VST3 / Standalone (à venir).

## Compiler et vérifier (Mac)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRD_BUILD_PLUGIN=OFF \
      -DRD_CONTENT_DIR=/chemin/vers/RubberDuck_H3O_2.12/content
cmake --build build -j
./build/rdrender "/chemin/vers/content/Acid_was_my_first_love.h30" acid.wav --bars 8
(cd build && ctest --output-on-failure)   # contrôle bit à bit
```

## Fichiers d'origine

Le programme d'origine et ses patterns (`.h30`) ne sont **pas** versionnés ici (droits
d-lusion). Pour les tests, copier le contenu de l'installeur `Setup_RD_2.12.exe` dans
`reference/` (ignoré par git).

## Usage

Projet personnel, non destiné à la diffusion publique : le nom, les graphismes et le
logiciel d'origine appartiennent à leurs ayants droit.
