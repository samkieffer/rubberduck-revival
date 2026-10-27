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
| Analyse de l'exécutable d'origine (décompilation) | en cours |
| Décodage du format de fichier `.h30` | à faire |
| Banc de comparaison avec l'original (Wine) | à faire |
| Moteur sonore C++ portable + rendu WAV | à faire |
| Plugin AU / VST3 (JUCE) | à faire |

## Organisation du dépôt

- `docs/` : notes d'analyse du logiciel d'origine (format `.h30`, moteur sonore).
- `core/` : moteur sonore en C++ pur, sans dépendance (à venir).
- `tools/` : outils en ligne de commande (rendu d'un `.h30` en WAV, comparaison A/B) (à venir).
- `plugin/` : enveloppe JUCE pour AU / VST3 / Standalone (à venir).

## Fichiers d'origine

Le programme d'origine et ses patterns (`.h30`) ne sont **pas** versionnés ici (droits
d-lusion). Pour les tests, copier le contenu de l'installeur `Setup_RD_2.12.exe` dans
`reference/` (ignoré par git).

## Usage

Projet personnel, non destiné à la diffusion publique : le nom, les graphismes et le
logiciel d'origine appartiennent à leurs ayants droit.
