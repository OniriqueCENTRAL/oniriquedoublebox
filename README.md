# Onirique DoubleBox

Doubler à cinq voix : la voix elle-même au centre, deux doubles de chaque côté.
Chaque voix a sa place, son retard, son désaccord et son niveau, et un
pitch / formant commun se donne aux voix qu'on veut.

VST3 (Windows, macOS, Linux) et Audio Unit (macOS). Entrée mono ou stéréo,
sortie stéréo.

---

## État du projet

| Partie | État |
|---|---|
| Moteur (cinq voix, pitch et formant par grains, manipulations, drift, EQ) | fait, testé hors ligne |
| A/B, presets stock et utilisateur, état sauvegardé | fait, testé hors ligne |
| Plugin VST3, paramètres automatisables | compile sous Windows |
| Interface | écrite, vérifiée sur capture (`doublebox_snapshot`) |
| Écoute dans un DAW | **pas encore faite** |

Le moteur a été mesuré sur des signaux de synthèse et sur une vraie prise de
voix (`doublebox_voicecheck`), pas écouté. Les réglages par défaut et les
presets stock ont été posés par raisonnement : ils restent à ajuster à
l'oreille.

---

## Construire

JUCE 9.0.3 est cherché dans `JUCE/`, puis dans `../PitchBox/JUCE` : les plugins
de la gamme partagent la même copie.

```bash
cmake -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Le plugin sort dans
`build/OniriqueDoubleBox_artefacts/Release/VST3/DoubleBox.vst3` ; à copier dans
`C:\Program Files\Common Files\VST3`.

Outils de développement, sans hôte :

```bash
cmake --build build --config Release --target doublebox_snapshot doublebox_voicecheck
build/Release/doublebox_snapshot.exe snapshot.png       # capture de l'interface
build/Release/doublebox_snapshot.exe --selftest         # A/B, presets, état, mono et stéréo
build/Release/doublebox_voicecheck.exe voix.wav dossier # neuf réglages rendus en fichiers à écouter
```

---

## Utilisation

**Partout** : le pointeur laissé immobile deux secondes sur un contrôle en fait
apparaître la description, en français. Elle reste ensuite tant que le pointeur
est sur ce contrôle, même s'il bouge, et s'en va quand il le quitte.

**En-tête — quels réglages sont chargés**

Presets, **A / B** et **BYPASS**. Un clic sur le nom du preset ouvre le
**navigateur**, repris de ModBox : à gauche les filtres — origine (ALL,
FACTORY, USER) et type de son (DOUBLER, PITCH, FORMANT, MANIPULATE, ROBOT,
plusieurs à la fois) —, à droite les presets qui restent, avec une recherche
par nom. Un clic charge un preset pour l'entendre, OK ou un double-clic le
garde, CANCEL ou Échap remet ce qu'il y avait. Le type n'est écrit nulle part :
il se lit dans les réglages, donc tes presets sont triés comme ceux d'origine.
< et > parcourent ce que les filtres laissent. Aux doubleurs s'ajoutent des
presets de transformation de voix (voix du centre seule : Deep Voice, Giant,
Helium, Robot, Alien, Demon…) et des presets où ce sont les doubles qui sont
manipulés (Android Choir, Ghost Pad, Sub Shadows…). Les presets utilisateur sont des fichiers dans
`%APPDATA%\Onirique\DoubleBox\Presets`. BYPASS garde la latence et n'est
touché ni par un preset ni par un changement de slot.

**La scène — où chaque voix s'entend, et à quel niveau**

Cinq points : **LL**, **L**, **C** (la voix elle-même), **R**, **RR**. De
gauche à droite, la place dans la stéréo ; de bas en haut, le niveau (−36 à
+6 dB). Le retard n'est pas sur la scène : il se règle dans le panneau.

- **glisser** un point : pan et niveau à la fois ;
- **molette** sur un point : désaccord, un cent par cran (`Maj` : un dixième) ;
- **double-clic** : coupe la voix (l'anneau reste, vide) ;
- **clic droit** : pan, niveau, retard et désaccord reviennent au départ ;
- **sélection multiple** : `Ctrl` ou `Maj` + clic, ou un cadre tracé dans le
  vide. Glisser, molette, double-clic et clic droit agissent alors sur toute
  la sélection : même déplacement, même nombre de dB, même nombre de cents.

Sous chaque point : son retard et son désaccord, et en rouge le pitch et le
formant quand ils lui sont donnés. Le halo suit le niveau de la voix à
l'instant.

**Le panneau sous la scène — tout d'une voix, ou de plusieurs**

La voix cliquée en dernier (ou `<` `>`) : marche/arrêt, **DELAY** (0 à 100 ms),
**DETUNE** (±50 cents), **LEVEL**, **PAN**, et **PITCH / FORMANT** pour lui
donner le pitch et le formant de la colonne. Chaque valeur en capsule se clique
pour être tapée.

Avec plusieurs voix sélectionnées, le panneau montre celle cliquée en dernier
et agit sur toutes : chaque potard décale toutes les voix de la même quantité
(en gardant leurs écarts), et les deux interrupteurs les mettent toutes dans
le même état. Il y a toujours au moins une voix sélectionnée : un clic dans le
vide revient à la dernière.

**Colonne de gauche**

- **PITCH** et **FORMANT** (±12 demi-tons chacun), indépendants : le pitch
  déplace la hauteur en gardant la voyelle, le formant déplace la voyelle en
  gardant la hauteur.
- **APPLY TO** : les cinq voix, dans l'ordre de la scène. Seules celles qui
  sont allumées reçoivent le pitch et le formant — juste à gauche, juste les
  deux extérieures, tout sauf le centre…
- **DRIFT** : les doubles dérivent, en hauteur (jusqu'à ±14 cents) et en temps
  (±3 ms), chacun à son rythme, comme une seconde prise. **RATE** : la vitesse
  de cette dérive. Le centre ne dérive jamais.
- **EQ** : deux plateaux, grave et aigu. Glisser un point : fréquence et gain
  (±12 dB) ; double-clic : gain à zéro. **ALL** traite tout, **DOUBLES**
  seulement les quatre doubles et laisse la voix du centre intacte.

**Colonne de droite — MANIPULATE**

Les commandes d'un manipulateur de voix, pour les voix allumées dans son propre
**APPLY TO** (ou par le bouton **MANIPULATE** du panneau de la voix). Une voix
qui les reçoit porte un losange rouge à côté de son nom. Le pitch et le formant
sont dans la colonne de gauche.

Le timbre :

- **HARMONICS** (−1 à +1) : fait glisser les harmoniques vers l'aigu ou le
  grave, de deux harmoniques au maximum, sans changer la hauteur. Avec
  **SNAP**, seulement par harmoniques entières.
- **FM** : chaque cycle est lu avec une oscillation à la hauteur de la voix.
  Du grain, puis un côté robot, sans changer la note.
- **ALTERNATOR** : un cycle sur deux est baissé, puis retourné. À fond, c'est
  l'octave du dessous.

La façon de suivre la voix :

- **RATIO** (−1 à 2) : ce qui reste de la mélodie. 1 la suit, 0
  tient une seule note (la moyenne lente de ce qui est chanté), au-delà elle
  est exagérée, en négatif inversée.
- **OCTAVE** (−2 à +2) : octaves entières, voyelles gardées.
- **GLIDE** (0 à 500 ms) : le temps pour rejoindre une nouvelle note.

La place :

- **SMEAR** : les cycles sont pris de plus en plus loin en arrière, au hasard
  (jusqu'à dix cycles). Les attaques et les consonnes se fondent en nappe.
- **STEREO** : un cycle penche à gauche, le suivant à droite. La somme mono
  reste la voix entière.
- **DETUNE** (0 à 50 cents) : désaccord en plus, vers le bas à gauche et vers
  le haut à droite, la paire intérieure de moitié.

**DRY / WET** : entre les voix manipulées telles qu'elles arrivent et avec tout
ce qui leur est fait. Au repos (tout à zéro, RATIO à 1), la colonne ne fait
rien. Pas d'entrée MIDI : la note tenue par RATIO vient de la voix elle-même.
**Pied**

**DOUBLES** : le niveau des quatre doubles ensemble. **WIDTH** : resserre
toutes les voix vers le centre (0 % = mono). **OUT**.

La latence (28 ms) est déclarée à l'hôte.

---

## Comment c'est construit

```
Source/dsp/                  sans dépendance, testable en ligne de commande
  PeriodTracker              la période de la voix, suivie en continu (YIN à ~12 kHz)
  Doubler                    tout ce qui décide du son
Source/PluginProcessor       paramètres, bus, état, slots A/B, presets
Source/PluginEditor          la fenêtre
Source/ui/StageDisplay       la scène, ses points et le panneau de la voix
Source/ui/EqDisplay          la petite courbe de l'EQ
Source/ui/Controls, Theme    repris de ResoBox
```

### Deux chemins par voix

Une voix qui n'est ni désaccordée ni décalée est lue directement dans une
ligne à retard : c'est l'entrée, à l'échantillon près. La voix du centre, par
défaut, est donc intacte.

Une voix qui l'est est reconstruite par grains : deux cycles de la voix sous
une fenêtre, reposés un nouveau cycle plus loin. Plus serrés, la hauteur
monte ; plus espacés, elle descend ; et comme chaque grain contient toujours
une impulsion entière, la voyelle ne bouge pas. Lire le grain plus vite ou
plus lentement déplace la voyelle sans toucher à la hauteur : c'est le formant.
Désaccord, pitch, formant et dérive passent tous par là, voix par voix.

Trois choses font que ça tient :

1. **La période** est suivie sur une copie du signal à ~12 kHz, puis le
   départ exact de chaque cycle est retrouvé à pleine résolution en comparant
   un cycle au suivant. L'erreur sur un désaccord de quelques cents est de
   l'ordre du dixième de cent.
2. **Les grains sont centrés sur l'impulsion** du cycle, là où la voix met son
   énergie. Centré entre deux impulsions, un grain en contient deux moitiés qui
   ne se recollent plus à une autre hauteur : la voyelle change de couleur et
   perd plusieurs dB.
3. **Ce qui n'a pas de hauteur** (souffle, consonnes) est reposé tel qu'il a
   été pris : les grains s'additionnent alors en le signal lui-même.

Le passage d'un chemin à l'autre se fait en fondu sur 20 ms.

### Ce que les tests vérifient

`test_doubler` (moteur) :

- la voix du centre seule rend l'entrée retardée de la latence, à
  l'échantillon près, en mono comme en stéréo ; bypass aussi ;
- un retard de 20 ms fait 20 ms à l'échantillon près ; pan, width, niveaux ;
- un désaccord de ±7 à ±50 cents est tenu à moins de 2 cents, sans changer le
  niveau ;
- pitch ±5, +7, ±12 : la hauteur bouge de ce qui est demandé, les formants
  restent ; formant ±4 : les formants bougent, la hauteur non ;
- le pitch ne va qu'aux voix auxquelles il est donné ;
- octave, ratio (0 tient une note, 2 double l'écart), detune gauche / droite,
  stereo (les deux côtés diffèrent, leur somme est la voix), dry / wet à dry =
  l'entrée à l'échantillon près, snap = harmoniques entières ;
- manipulations : harmonics déplace le poids du timbre dans le sens demandé
  sans bouger la hauteur ; alternator à fond donne l'octave du dessous ; FM et
  smear gardent la note et le niveau ; une voix à qui elles ne sont pas données
  reste intacte à l'échantillon près ;
- un bruit garde son niveau, une voix avec vibrato est suivie ;
- mêmes échantillons quel que soit le découpage en blocs (64, 333, 512, 4096) ;
- l'EQ fait ce que sa courbe montre, et n'atteint pas le centre en mode
  DOUBLES ;
- environ 70× le temps réel, cinq voix décalées, entrée stéréo.

`doublebox_snapshot --selftest` (processeur) : slots A/B, état sauvegardé,
presets, et le plugin tel qu'un hôte le pilote en mono → stéréo et en stéréo.

### Limites connues

- Le suivi de période va de 75 Hz à 1 kHz et suppose une seule voix : sur un
  accord ou un mix, les doubles désaccordés restent utilisables mais le pitch
  et le formant ne sont plus propres.
- Une octave vers le bas sur une voix seule a le grain du procédé ; mêlée aux
  autres voix, c'est son rôle.

---

## Licence

JUCE 9 est sous double licence AGPLv3 ou commerciale : une distribution payante
à code fermé demande une licence JUCE commerciale.
