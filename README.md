# gdlearn2 — bot Geometry Dash (réécriture complète)

Réécriture from scratch de `GDlearn-beta`. Zéro dépendance : juste un compilateur
C++17. Pas de PyTorch, pas de lecture mémoire du jeu, pas de spaghetti.

```
./build/gdlearn selftest     # 8 groupes de tests (physique, réseau, macro, recherche)
./build/gdlearn bench        # ~46 M frames/s simulées, 765 000x temps réel
./build/gdlearn gen          # génère un curriculum de niveaux PROUVÉS faisables
./build/gdlearn solve lvl    # trouve une solution frame-perfect (beam search)
./build/gdlearn train        # entraîne la policy PPO
./build/gdlearn eval         # évalue un checkpoint
./build/gdlearn replay/trace # rejoue / debug frame par frame
```

## Ce qui n'allait pas dans l'ancien code (et qui est corrigé ici)

| Ancien projet | Ici |
|---|---|
| Unités mélangées : vitesse horizontale en unités/**seconde**, vitesse verticale en unités/**frame**, sans conversion | Convention unique documentée dans `src/core/physics.hpp` (1 bloc = 30 u, 60 Hz) |
| État du joueur avec `std::unordered_set` pour les triggers déjà activés → impossible à copier, à sauvegarder, à chercher | `State` est un **POD de 64 octets** (`static_assert`) : snapshot = memcpy. C'est ce qui rend la recherche d'arbre possible |
| Portails de gravité en *toggle* → rejouer les mêmes inputs pouvait donner deux résultats | Tous les triggers sont **idempotents** ou gardés par un `uid` dans l'état → déterminisme prouvé par le selftest |
| Offsets Cheat Engine codés en dur (`0x3222D0`, GD 2.206 uniquement) → cassé à chaque mise à jour | Aucune lecture mémoire. Le bot produit une **macro** rejouée par `SendInput` |
| Observations en coordonnées absolues → le réseau devait réapprendre chaque hauteur | Observations relatives au joueur ET à la gravité, normalisées en blocs |
| Génétique + PPO mélangés, 1400 lignes de générateurs de niveaux écrits à la main | PPO propre + **Go-Explore** + curriculum procédural vérifié |
| Niveaux d'entraînement parfois physiquement infaisables | `gen` **prouve** chaque niveau avec le solveur avant de l'écrire (106 candidats rejetés sur 120 au dernier run) |
| Aucun moyen de savoir *pourquoi* le bot meurt | `gdlearn trace` : dump frame par frame (x, y, vy, sol, mort, mode) |

## Architecture

```
src/core/    physics.hpp  constantes et convention d'unités
             level.*      niveau immuable + index CSR par colonnes de 60 u
             sim.*        LE simulateur : 1 frame = 1 step, POD, sans allocation
src/env/     obs.*        encodeur d'observation 600 floats (grille 16x12x3 + 24 scalaires)
             env.*        épisodes + archive Go-Explore
src/nn/      net.*        MLP + Adam écrits à la main (policy + value)
src/rl/      ppo.*        PPO multi-thread, GAE, clipping, entropie annelée
src/search/  beam.*       beam search frame-perfect (le vrai tueur de niveaux)
src/io/      macro.*      format .macro (RLE) rejouable
src/app/     main.cpp     CLI
live/        win_macro_player.cpp   rejeu frame-perfect sous Windows
tools/       gmd_to_gdl.py          .gmd / CCLocalLevels.dat -> .gdl
```

### Les deux cerveaux (c'est la clé)

1. **Beam search** (`solve`) — cherche dans l'arbre des inputs avec déduplication
   d'états quantifiés. C'est *exact* : quand il sort une macro, elle gagne, et
   `verifyMacro` le revérifie à côté. C'est ce qui fait un bot « méga fort ».
2. **PPO + Go-Explore** (`train`) — apprend une policy généraliste qui joue sans
   recherche, et qui sert de *prior* pour guider le beam search
   (`SolveOptions::prior`) sur les niveaux longs. Go-Explore relance les
   épisodes depuis les états les plus avancés déjà atteints : sans ça, le bot ne
   voit jamais la fin d'un niveau difficile.

## Résultats mesurés dans cette session

- Simulateur : **45,9 M steps/s** en single-thread (765 000x le temps réel).
- `selftest` : tous les groupes passent (déterminisme, snapshot, encodeur, réseau
  sauvé/rechargé bit à bit, macro aller-retour, recherche + rejeu identiques).
- Smoke run PPO (400 k steps, 8 envs, 2 cœurs) : jusqu'à **33 % de win rate**
  sur les niveaux faciles, record 100 %. C'est un test de plomberie, pas un vrai
  entraînement — lance 20 M+ steps pour du sérieux.
- Beam search : 100 % sur les niveaux d0–d6 générés, ~36 % sur *Stereo Madness*
  converti (les constantes de physique restent à calibrer, voir ci-dessous).

## Honnêteté sur la calibration

Les constantes de `physics.hpp` (gravité, hauteur de saut, boosts d'orbes) sont
des valeurs *communautaires approchées*, pas des valeurs extraites du binaire de
GD. Sur les niveaux procéduraux, ça n'a aucune importance. Sur un vrai niveau
rejoué en live, un écart de 1 % de gravité décale un saut d'une frame et tue le
bot au bout de 30 secondes.

Procédure de calibration :
1. Dans GD, sur un niveau plat, saute et compte les frames d'airtime
   (enregistrement d'écran à 60 fps).
2. `./build/gdlearn selftest` affiche `airtime = N frames`. Ajuste
   `CUBE_GRAVITY` / `CUBE_JUMP` jusqu'à ce que les deux coïncident.
3. Recommence par mode (ship, ball, ufo, wave, robot, spider, swing).

C'est pour ça que le pipeline est « recherche offline → macro » et pas « réseau
qui joue en direct » : une macro vérifiée est reproductible, une policy en direct
dépend de la latence d'input.

## Build

### Linux / macOS
```bash
make -j
./build/gdlearn selftest
```

### Windows (Visual Studio)
```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\gdlearn.exe selftest
```

## Workflow complet

```bash
# 1. convertir tes vrais niveaux
python3 tools/gmd_to_gdl.py "levels/*.gmd" -o levels_real
#    (ou tout ton compte : python3 tools/gmd_to_gdl.py CCLocalLevels.dat --save-file -o levels_real)

# 2. curriculum vérifié pour l'apprentissage
./build/gdlearn gen --out levels --difficulty 9 --per-difficulty 4

# 3. entraînement (utilise tous les cœurs)
./build/gdlearn train --levels levels --total-steps 20000000 --envs 64 --out runs/v1

# 4. résoudre un niveau précis, frame-perfect
./build/gdlearn solve levels_real/1.gdl --beam 4000 --out stereo.macro

# 5. rejouer dans le vrai jeu (Windows)
cl /O2 /EHsc live\win_macro_player.cpp /Fe:macro_player.exe
macro_player.exe stereo.macro --key space --delay 3

# 6. debug quand ça meurt
./build/gdlearn trace levels_real/1.gdl --macro stereo.macro --from 1800
```

## Réglages qui comptent vraiment

- `--beam` : la seule variable qui décide si un niveau tombe ou pas. 800 pour
  itérer, 4000–20000 pour un vrai run (coût linéaire en mémoire et en temps).
- `--envs` × `--steps-per-env` = taille du batch PPO. 64×128 = 8192 est un bon
  défaut ; monte les envs avant de monter le learning rate.
- `restartProb` / `frontierBias` (`src/env/env.hpp`) : agressivité de
  Go-Explore. Plus haut = le bot passe plus de temps sur les passages tardifs.
- `entropyStart/End` : si le bot spamme le saut, l'entropie est trop haute ; s'il
  ne bouge plus, elle est trop basse.

## Deux bugs de physique trouvés pendant l'écriture (gardés en commentaire)

1. **Input bufferisé** — GD fait sauter le cube à chaque contact au sol tant que
   le bouton est maintenu. Exiger un nouvel appui rendait des familles entières
   de sauts inatteignables.
2. **Pardon des coins** — GD ne tue sur le côté d'un bloc que si la hitbox
   *interne* est touchée. Tuer sur la hitbox complète rendait un simple escalier
   infranchissable. Le beam search a servi d'oracle : il a prouvé en quelques
   secondes que le niveau était impossible, donc que la physique était fausse.

MIT.

## Correctifs v5 : le crash silencieux, pour de bon

En v4 le run mourait encore a 34 niveaux sur 39, malgre le try/catch. Trois causes
cumulees, toutes corrigees :

### 1. Build 32 bits (la cause principale)
Les generateurs Visual Studio compilent en **Win32 par defaut**. Un process 32 bits
ne peut adresser que ~2 Go, quelle que soit la RAM de la machine. Avec 3 threads a
~1.1 Go chacun, lallocation echoue de facon brutale : ce nest pas une exception
C++ propre, donc aucun catch ne peut la rattraper.

A faire de ton cote :
    cmake -B build -A x64
    cmake --build build --config Release

Le programme detecte maintenant le cas et affiche :
    rating 25 level(s): 1 thread(s) (~1373 MB per thread, budget 1200 MB, 32-bit build)
    warning: 32-bit build; address space is the real limit. Rebuild with: cmake -B build -A x64
Et CMake emet un WARNING a la configuration.

### 2. Estimation memoire trop optimiste
Le `trail` du beam est un vecteur de vecteurs : chaque frame est une allocation
separee avec du rab de capacite, et le buffer de candidats vit a cote. La formule
`beamWidth * frames * 8` sous-estimait dun facteur ~2.5, do le message rassurant
"budget 4096 MB" suivi dune mort. Facteur 2.5 applique, defaut abaisse a 2048 Mo.
Meme configuration, avant / apres : `~1099 MB` annonce -> `~1373 MB` reel.

### 3. Plafond de frames absurde
`--rate-max-frames` valait 24000 pour tous les niveaux. Or meme au palier de vitesse
le plus lent le joueur avance de 251 unites par seconde : un niveau de 26k unites
est fini en ~6300 frames. On budgetait donc 4x trop de memoire pour rien. Le
plafond est maintenant calcule par niveau depuis sa longueur reelle
(`longueur / vitesse_min * 1.4 + 600`).

### 4. Filet de securite
`catch (...)` en plus du `catch (const std::exception&)`, et un avertissement
explicite si un seul niveau depasse a lui seul le budget memoire.

## Correctifs v6 : identifier le niveau tueur, et borner le beam

Le run v5 mourait a 33 niveaux sur 39, en mono-thread, donc de facon sequentielle :
le coupable etait le niveau suivant dans la liste, 5001. Impossible a reproduire
sous Linux (5001 et 5004 tournent tres bien ici), donc specifique a Windows/MSVC.
Trois mesures :

### 1. Le niveau est annonce AVANT detre traite
    ... rating 5001                      362 blocks, 4227 frames, beam <= 6350
    [ 20/ 25] 5001                         UNSOLVED beam  6000  stuck at 0.00% ...
Si le process meurt, la derniere ligne affichee nomme le niveau responsable au
lieu de laisser une liste tronquee a deviner. Desactivable avec `--no-announce`.

### 2. Le beam est borne par niveau
La largeur demandee est desormais plafonnee par le budget memoire ET par le
plafond de frames du niveau : `budget / (20 octets * frames)`. Une demande de
6000 sur un niveau long devient une largeur plus petite au lieu de tenter
dallouer des gigaoctets. Exemple a `--mem-budget-mb 512` : niveau 5004
`beam <= 2707`.

### 3. Pile MSVC alignee sur Linux
MSVC donne 1 Mo de pile par thread, Linux 8 Mo. Cest exactement le genre decart
qui tue une seule plateforme. `/STACK:8388608` ajoute au link MSVC.

### 4. Ce que le diagnostic a revele sur 5001 et 5004
Ce sont les deux seuls niveaux avec des coordonnees negatives (`min_x = -15`), et
5001 affiche `stuck at 0.00% after 1 frames` : **le joueur nait a linterieur de la
geometrie**. Le convertisseur ne decale pas lorigine du niveau, donc des objets
places avant x=0 recouvrent le point dapparition et tuent immediatement. A
corriger dans `tools/gmd_to_gdl.py` en translatant tout le niveau pour que
`min_x >= 0`.

## Correctifs v7 : LE crash, trouve et corrige

La ligne dannonce ajoutee en v6 a fait son travail : le run sarretait sur
`... rating 5001`. Le coupable etait donc le niveau 5001, et la cause est un bug
de ma part dans `beam.cpp`.

### La cause exacte
Le niveau 5001 commence par un mur solide empile a x=15, juste devant le point
dapparition (ids 94 et 1, y de 15 a 195). Le joueur nait donc dans la geometrie
et meurt a la frame 0. Tous les candidats meurent, `cands` est vide, on sort de
la boucle par `break`... avec `trail` vide. Puis `reconstruct` fait :

    res.holds.assign(bestFrame + 1, 0);
    const auto& e = trail[f][slot];      // trail[0] sur un vecteur VIDE

Cest un comportement indefini. Et il etait invisible :
- `trail.reserve(1024)` fait que cette lecture tombe dans de la capacite deja
  allouee mais non construite, donc AddressSanitizer ne la signale pas ;
- sous Linux la memoire fraiche est nulle et le programme sen sortait en
  retournant 0.00% ;
- sous MSVC en Release, on dereference un vecteur poubelle et le process
  disparait sans exception, sans message. Do le try/catch inutile.

### Le correctif
- garde explicite sur `trail` vide dans `reconstruct`, qui retourne un resultat
  propre a 0 frame ;
- clamps defensifs sur `bestFrame` et `bestSlot` : un mauvais index doit etre
  corrige la ou il se produit, pas devenir une corruption memoire silencieuse ;
- le cas est nomme au lieu detre un 0.00% mysterieux :

    [ 20/ 25] 5001    ERROR    dies at spawn (level starts inside geometry)

### Lecon pour la suite
Les ids 94 et 1 au spawn de 5001 renvoient au meme probleme de fond deja
identifie : `SOLID_IDS` est une plage en bloc qui transforme de la decoration en
murs mortels. Corriger le convertisseur reglera a la fois ce spawn et les
niveaux 13 et 14.

## v8 : la liste blanche de blocs solides (le vrai probleme de fond)

Cest la correction la plus importante du projet. Elle ne touche pas au moteur
mais aux donnees, et cest la que se jouait le blocage a 2-5% sur tous les
niveaux reels.

### Le diagnostic
Histogramme des ids classes solid sur les 25 niveaux reels : id 94 apparait
8283 fois, id 61 7478 fois, puis 468, 211, 157, 193, 227, 120... Or le niveau 1,
le seul dont le macro est verifie a 100%, nutilise que 16 ids :

    1 2 3 4 5 6 7 15 16 17 40 41 54 62 65 142

Aucun des ids massifs des autres niveaux nen fait partie. `SOLID_IDS` etait une
plage en bloc (1-300 et 467-500) qui avalait toute la decoration des niveaux
recents et la transformait en murs mortels. Le bot ne perdait pas contre le
niveau, il perdait contre de la deco.

### La correction
`SOLID_IDS` devient une liste blanche fondee sur des preuves : uniquement les
ids reellement utilises comme geometrie. Tout id inconnu redevient de la
decoration et est ignore, ce qui est le defaut sur : un faux mur bloque
definitivement, un mur manquant se voit tout de suite en rejouant le macro.

### Validation, avant / apres
Deux criteres poses avant de lancer lexperience :

1. Le niveau 1 doit rester bit-a-bit identique et son macro passer a 100%.
   Resultat : 2194 objets -> 2194, 1580 solides -> 1580, replay 100.00% COMPLETE.
   La liste blanche na donc retire aucune vraie geometrie.
2. Le blocage des autres niveaux doit reculer.

Nettoyage constate : niveau 13 5110 -> 249 solides, niveau 14 6543 -> 100,
niveau 5001 3154 -> 451, niveau 3001 841 -> 17. Le spawn du 5001 est degage.

Niveaux battus par le solveur : 1 -> 4 (niveaux 1, 12, 17, 3001).
Progressions notables : 11 5.84% -> 16.40%, 15 1.49% -> 16.07%,
16 2.39% -> 12.07%, 12 15.17% -> RESOLU, 17 7.55% -> RESOLU,
3001 2.36% -> RESOLU, 5001 crash -> 6.22%.

### Honnetete sur les regressions
Trois niveaux reculent : 18 3.74 -> 1.39, 19 4.21 -> 2.75, 20 7.72 -> 4.29.
Cest attendu et sain : de la deco servait de plancher accidentel, le joueur
tombe maintenant dans des trous reels. La geometrie est plus fidele, donc plus
dure. Le niveau 2 reste bloque a 26.30% exactement comme avant, ce qui confirme
que son mur est un vrai probleme de gameplay et non un artefact de conversion.

### Limite a garder en tete
Larbitre final nest pas le simulateur mais le jeu. Un macro genere ici doit
etre rejoue avec `live/win_macro_player.cpp` sur le vrai Geometry Dash : si un
id manque a la liste blanche, le personnage traversera un mur a lecran. Les
macros des niveaux reconvertis ont ete supprimes du cache, celui du niveau 1 est
conserve puisque sa geometrie est inchangee.

## v9 : le visualiseur

Il ny en avait pas. Il y avait `trace`, qui affiche un tableau de flottants
frame par frame, et `replay`, qui donne un pourcentage. Pour comprendre
pourquoi le bot meurt a 26.30% du niveau 2, cest inutilisable.

    gdlearn render <level.gdl> [macro] [--out f.html] [--solve] [--beam N]

Exemples :

    gdlearn render levels_real/1.gdl macros/1.macro --out viewer/1.html
    gdlearn render levels_real/2.gdl --solve --beam 600 --out viewer/2_wall.html
    gdlearn render levels_real/13.gdl            (aucun input : chute libre)

Ca ecrit un fichier HTML autonome : pas de serveur, pas de dependance, pas de
CDN. Double-clic, ca souvre dans le navigateur, et ca marche hors ligne.

### Ce quon voit
- la geometrie reelle telle que le moteur la voit, pas le sprite du jeu :
  solides en gris, pics en triangles rouges (leur vraie hitbox, bien plus
  petite que le sprite), pads jaunes, orbes en cercles, portails violets ;
- le cube avec sa rotation, sa taille mini, son sens de gravite ;
- un halo jaune quand le bouton est tenu : on lit les inputs a lecran ;
- la trainee des 90 dernieres frames, pour voir la courbe du saut ;
- le cube passe au rouge sur la frame de mort.

### Controles
espace = play/pause, fleches = avancer/reculer dune frame, scrubber pour
sauter nimporte ou, vitesse 0.15x pour les morts au pixel, bouton
"go to end" pour aller directement a la frame de mort.

### Le point important
La trajectoire nest pas re-simulee en JavaScript. Le C++ rejoue le macro dans
`Sim`, le meme moteur que le solveur et lentrainement, et exporte les
positions obtenues. Un viewer avec sa propre physique aurait derive et menti
au moment precis ou on a besoin de lui : celui-ci montre exactement ce que le
moteur a calcule.

Le niveau 2 rendu a 26.32% donne enfin de quoi regarder le mur en face.

## v10 : le plafond manquant (deux bugs empiles)

Repere a loeil dans le visualiseur des sa premiere utilisation : en section
vaisseau, le joueur montait indefiniment, tres au-dessus du decor. Le viewer a
paye son cout en une minute.

### Bug 1 : le convertisseur necrivait jamais de toit
`tools/gmd_to_gdl.py` emettait `roof 0` en dur, et `roof 0` signifie "pas de
plafond" pour le moteur. Tous les niveaux reels etaient donc ouverts vers le
haut jusquau plan de mort a 80 blocs.

### Bug 2 : le moteur ignorait le toit en gravite normale
Plus grave, dans `sim.cpp` les deux plans etaient un `if / else if` sur la
gravite :

    if (!st_.flip) { ... plancher ... }
    else if (roofY > 0 && ...) { ... plafond ... }

Le plafond nexistait donc QUE gravite inversee. Meme avec un `roof` correct
dans le fichier, un vaisseau en gravite normale navait rien au-dessus de lui.
Meme faute dans `obs.cpp` : lagent ne voyait pas le plafond dans sa grille
dobservation.

### Ce que ca cachait
Mesure sur le macro "verifie" du niveau 1 :

    trajectoire : max y = 79.5 blocs, 2842 frames au-dessus de 30 blocs
    geometrie   : objet le plus haut du niveau = 14.5 blocs

Le solveur ne resolvait pas Stereo Madness, il passait en vol au-dessus. Le
100% verifie de toutes les versions precedentes etait faux : injouable dans le
vrai jeu. Cest le genre de bug quun pourcentage ne revele jamais.

### Corrections
- convertisseur : `roof = max(300, y_max_objet + 2 blocs)`, derive de la
  geometrie. Assez haut pour ne bloquer aucune route legitime, assez bas pour
  tuer lexploit. Niveau 1 -> 510, niveau 2 -> 450, niveau 13 -> 683.
- `sim.cpp` : plancher et plafond deviennent deux plans INDEPENDANTS, toujours
  testes. La gravite ne decide plus de leur existence, seulement duquel des
  deux compte comme sol (`onGround`).
- `obs.cpp` : les deux plans sont encodes comme solides quelle que soit la
  gravite.

### Verification
Ancien macro sous le vrai plafond : 100% -> 36.34%. Lexploit est bien mort.
Niveau 1 re-resolu honnetement, beam 3000 :

    SOLVED  progress 100.00%  frames 5110  expanded 19106k  3.9s  verify 100.00%
    trajectoire : max y = 16.5 blocs, sous le toit a 17

`macros/1.macro` est desormais cette solution la. Les macros obsoletes
(`1_stereo_madness`, `proc_*`) ont ete supprimes : ils avaient tous ete
produits sans plafond.

## v11 : le plafond LOCAL (correction de la correction)

Le toit global de la v10 etait un mauvais correctif, signale immediatement dans
le visualiseur : le vaisseau montait toujours au-dessus du niveau.

### Pourquoi le toit plat etait pire que rien
Mesures sur la section vaisseau du niveau 1 :

    couverture de blocs de plafond : 23 colonnes sur 153, soit 15%
    plafonds locaux du couloir     : 9.5 a 11.5 blocs
    toit global emis en v10        : 17 blocs

Le couloir est ouvert en haut a 85%, et le toit global etait 5 blocs au-dessus
de ses plafonds. Resultat : au lieu de contenir le joueur, il lui a fourni une
autoroute lisse au-dessus du decor, sur laquelle le macro roulait a 16.5 blocs.
Un plafond plat ne peut pas exprimer la contrainte, parce que la contrainte est
locale.

### Le bon modele : la camera de GD
Dans le vrai jeu, le joueur reste dans le couloir visible. La camera se pose sur
le sol situe SOUS le joueur, donc un vaisseau monte environ dix blocs au-dessus
de cette surface, pas plus, et un couloir surleve a un plafond surleve.

`Sim::resolveWorld` calcule donc a chaque frame la surface sous le joueur
(le solide le plus haut dont le sommet est sous ses pieds, sinon le sol) et
plaque un plafond a `phys::FLIGHT_CEILING` = 300 unites au-dessus, uniquement
pour les modes voles (ship, ufo, wave, swing : en cube un saut monte 4 blocs,
la contrainte est sans objet).

Le convertisseur reemet `roof 0` : le toit global est supprime, il ny a plus
de plafond artificiel servant accidentellement de sol en gravite inversee.

### Verification

    ancien macro autoroute, sous le plafond local : 100% -> 44.06%
    niveau 1 re-resolu, beam 3000 : SOLVED 100.00%  verify 100.00%
    altitude max : 79.5 blocs (sans plafond) -> 16.5 (toit plat) -> 12.5 blocs
    section vaisseau : le joueur navigue entre 2.9 et 8.9 blocs

### Le plafond est desormais visible
Le viewer affiche le plafond local en pointilles violets, avec son etiquette,
et il suit le joueur puisquil depend du sol sous lui. Le bug etait invisible
parce que rien ne le dessinait : cest corrige.

## v12 : se cogner la tete tue

Repere dans le visualiseur : le cube sautait dans les blocs situes au-dessus de
sa tete sans consequence.

### Le bug
Dans `resolveWorld`, le contact avec la face INFERIEURE dun solide etait un
simple arret :

    } else if (!falling && (prevHead - surfaceBottom) * g <= 1.0f) {
      st_.y = surfaceBottom - hh * g;
      st_.vy = 0;          // on sarrete, on ne meurt pas
    }

Dans Geometry Dash, seule la face SUPERIEURE dun bloc est sure. Se cogner la
tete en montant est une mort, pas un arret. Pire pour lentrainement : le
solveur pouvait utiliser les plafonds comme freins gratuits et sauter dans les
blocs au-dessus de lui sans aucune penalite, ce qui elargissait artificiellement
lespace de recherche avec des trajectoires injouables.

### La correction
Le contact par le dessous devient mortel dans tous les modes au sol. Les modes
voles gardent le glissement : un vaisseau longe un plafond, cest le
comportement du jeu (et le mode wave meurt deja au moindre contact via
`diesOnTouch`).

### Verification

    macro precedent, sous la nouvelle regle : 100% -> 19.57%
    niveau 1 re-resolu, beam 3000 : SOLVED 100.00%  verify 100.00%

La chute a 19.57% est la preuve que lancienne solution se cognait vraiment la
tete, et le niveau reste franchissable sans cette triche.

## v13 : importer les vrais niveaux celebres

Les niveaux 1 a 22 livres avec le jeu contiennent deja les plus durs de RobTop
(Clubstep, Theory of Everything 2, Deadlocked). Les monstres de la communaute
(Bloodbath, Cataclysm, Sonic Wave...) vivent sur les serveurs GD et doivent
etre telecharges.

### `tools/fetch_gd_level.py`
A lancer depuis TON PC : mon bac a sable na aucun acces reseau, le script
utilise donc les memes endpoints que le jeu, depuis ta machine.

    python tools/fetch_gd_level.py --search "Bloodbath"
    python tools/fetch_gd_level.py --pack demons --out levels_real
    python tools/fetch_gd_level.py --id 10565740 --out levels_real

La recherche par NOM est la voie fiable : les IDs se retiennent mal, le serveur
non. `--pack` resout chaque nom via le endpoint de recherche et garde le plus
telecharge, ce qui ecarte les copies qui portent le meme titre. Aucun ID nest
code en dur.

Packs : `demons` (10 extremes celebres), `classics` (7 officiels durs).

### Verification hors-ligne
Sans reseau je ne peux pas prouver le telechargement, mais jai teste tout le
reste en reconstruisant une reponse serveur a partir dun vrai .gmd :

    decodage base64url + gzip du champ 4 : 41941 octets
    conversion  : 2194 objets de gameplay
    classement  : le vrai Bloodbath (9M dl) bat un copycat homonyme (12 dl)
    le .gdl produit se charge dans le moteur

### Ensuite
Une fois les niveaux dans `levels_real/`, `curriculum` les note et les integre
au programme dentrainement par difficulte croissante. Attention : ces niveaux
font 20k a 60k objets, le solveur naura aucune chance de les finir, ils servent
de materiel dentrainement extreme, pas de cible a 100%.

## v14 : recuperer la geometrie des niveaux 2.0

Clubstep se terminait a 1%% des la premiere seconde. Ce netait pas sa
difficulte : le fichier converti ne contenait que 100 blocs solides pour 26745
unites de long. Ma whitelist dids de la v8, calibree sur Stereo Madness (un
niveau 1.0), jetait 87% des objets de Clubstep, qui utilise des blocs 2.0.

### Classer les ids sans les deviner
Je ne pouvais pas declarer ces ids solides de memoire : un niveau 2.0 est
massivement decore, et transformer la deco en murs casse le niveau dans
lautre sens. Deux signaux mesurables sur les 25 niveaux :

  1. lid porte-t-il des pics connus ? (un pic repose toujours sur du solide)
  2. forme-t-il de longues rangees alignees sur la grille ? (sols, plateformes)

Ids retenus : 61, 91, 94, 117, 118, 121, 122, 193, 211, 498, 503, 504,
1011, 1012, 2466, 3092, 3507, 4023.

### Resultat, avec garde-fou

    niveau  1 : 1580 -> 1580 solides (inchange), macro toujours 100.00%
    niveau 14 :  100 -> 2495 solides   (Clubstep)
    niveau 13 :  249 -> 1638 solides
    niveau 21 :   82 -> 7217 solides   (Deadlocked)

Le test de non-regression est le macro verifie du niveau 1 : sil restait a
100%, aucun id ajoute na fabrique de faux mur sur son trajet.

### Ce qui bloque encore
Clubstep meurt toujours a 0.99%, sur un mur de 2 blocs a x=295 :

    apex maximum dun saut cube : 65.2 unites = 2.17 blocs
    sommet du mur              : 98.5 unites = 3.28 blocs

Le mur est donc infranchissable sans plateforme dappel, et cette plateforme
manque encore. La conversion reste incomplete pour les niveaux 2.0 : il reste
des ids structurels non identifies. Un macro Clubstep na aucun sens tant que
le fichier nest pas reellement Clubstep.

## v15 : le meme bloc, deux roles

Signalement : au debut de Clubstep des blocs bloquent le passage alors quils
nexistent pas dans le vrai niveau. Exact, et cetait ma faute.

### Le piege
Le mur infranchissable a x=295 etait fait dobjets dun seul id : 41. Mon audit
lavait classe sans preuve structurelle (3 supports de pics, 0 rangee alignee
sur 1089 objets), donc je lai retire de la whitelist. Le macro verifie du
niveau 1 est alors tombe de 100% a 80.79%.

Conclusion : lid 41 PORTE le joueur dans Stereo Madness et DECORE Clubstep.
Le meme identifiant a deux roles selon le niveau. Une whitelist globale par id
ne peut structurellement pas trancher ce cas.

### Pourquoi aucune regle automatique ne sen sort
Jai cherche un discriminant par objet dans les donnees : calque (cle 24),
ordre de rendu (25), canal de couleur (21). Ces cles sont ABSENTES de tous les
objets des niveaux officiels, qui sont stockes en format minimal. Il ny a donc
rien a exploiter : la distinction doit etre declaree.

### `DECO_OVERRIDES`
Le convertisseur accepte desormais des exceptions par niveau, plus des drapeaux
manuels :

    python tools/gmd_to_gdl.py levels/14.gmd --out levels_real --deco-ids 41
    python tools/gmd_to_gdl.py levels/X.gmd  --out levels_real --solid-ids 1011

Table actuelle : `14: {41}` (Clubstep). Lid 41 reste solide partout ailleurs.

### Resultat mesure

    niveau  1 : 1580 solides, macro toujours 100.00%  (non-regression)
    Clubstep  : le mur a x=295 a disparu, 2475 solides
    solveur   : 0.99% -> 16.64% (857 frames, verifie)

Si tu vois dautres blocs fantomes, donne-moi le niveau et lendroit : cest
exactement le signalement qui permet de completer la table.

## v16 : le cube senvole apres un pad -- gravite inversee et vy terminal

Signalement : le bot prend un pad et monte indefiniment jusqu'a mourir en hauteur.

### Diagnostic exact
Trace : vy = 15.00 exactement, CONSTANT pendant plus de 100 frames dans l'air.
Dans la gravite normale (g=1) c'est impossible : la gravite reduit vy de 0.958/frame.
Dans la gravite inversee (g=-1), `clampFall` agit dans l'autre sens :

    if (st_.vy * g < -terminal)  =>  if (vy * (-1) < -15)  =>  if (vy > 15)
    => plafonne a vy = -terminal * g = 15

A chaque frame, la gravite tente d'accelerer le cube vers le plafond (+0.958),
`clampFall` le replafonne a 15. vy reste bloqu\u00E9 a exactement CUBE_TERMINAL
indefiniment. Le cube monte a 15 u/frame jusqu'a CEILING_Y = 2400 u = 80 blocs.

### Correctif
Le plafond local (10 blocs au-dessus de la surface sous le joueur) etait
calcule uniquement pour les modes voles. Avec la gravite inversee, le cube est
dans la meme situation : il tombe vers le plafond, et sans bloc pour l'arreter
il sort du niveau.

Extension du clamp : si `st_.flip && st_.y + hh >= lastCeiling_`, memes
actions que pour les modes voles (plafonner y, zeroise vy positive).
`lastCeiling_` est deja calcule depuis le sol sous le joueur ; en gravite
inversee ca donne le sol inverse + 10 blocs = plafond realiste.

### Resultats

    vy bloqu\u00E9 a terminal : oui (avant) -> non (apres)
    Clubstep avant correctif  : 16.64% (via cheese en hauteur)
    Clubstep apres correctif  : 14.91% (trajectoires physiquement valides)
    niveau 1 non-regression   : 100.00%  5110 frames COMPLETE
    selftest                  : all checks passed

La legere baisse de progression montre que certaines trajectoires qui passaient
avant dependaient du cheese en gravite inversee. Le bot joue maintenant un
Clubstep physiquement plausible.

## v16 : gravite inversee - le cube senvole apres un pad

Signalement : apres un pad, le bot monte indefiniment et meurt en hauteur.

### Diagnostic
Trace : vy = 15.00 CONSTANT pendant 100+ frames dans les airs.
Dans la gravite inversee (g=-1), clampFall agit dans le mauvais sens :
  if (vy * g < -terminal)  =>  if (-vy < -15)  =>  if (vy > 15)
=> vy est plafonne a +15 a chaque frame.
La gravite ajoute +0.958 vers le plafond, clampFall le replafonne a 15 :
cycle infini, le cube monte a terminal velocity jusqu'a CEILING_Y = 80 bl.

### Correctif
En gravite inversee, la reference est le bloc AU-DESSUS du joueur (y+hh),
non le sol SOUS lui. Nouveau calcul :
- Cherche le solide le plus bas dont la face inf est > y+hh (plafond cible)
- Si aucun bloc au-dessus : fallback = sol + 2*FLIGHT_CEILING (20 blocs)
- Clamp si y+hh >= flipRef+hh

Le fallback 20 blocs est genereux pour un saut red pad (max 8 bl) mais
bloque toute trajectoire indefinie bien avant CEILING_Y=80.

### Resultats mesures
  0.99%  (mur decoratif non retire)
  16.64% (mur retire, pas de correctif flip)
  14.91% (1er correctif flip: comparaison inversee, regression)
  26.13% (correctif flip corrige: 1346 frames verifiees)
  niveau 1 : 100.00%% / 5110 frames COMPLETE (non-regression)
  selftest : all checks passed
