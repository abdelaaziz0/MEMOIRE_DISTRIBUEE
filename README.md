# Distributed Shared Memory (DSM) en C

📢 **Un système de mémoire partagée distribuée permettant à plusieurs processus de partager une mémoire virtuelle commune sur différentes machines, en gérant dynamiquement l'accès aux pages mémoire.**

---

## 📋 Fonctionnalités
✔️ **Partage de mémoire virtuelle** : Permet à plusieurs processus distants d’accéder à la même mémoire virtuelle.
✔️ **Gestion dynamique des pages mémoire** : Transfert de propriété des pages selon les accès des processus.
✔️ **Traitement des erreurs de segmentation (SIGSEGV)** : Capture des accès non autorisés pour déclencher la mise à jour des pages.
✔️ **Allocation cyclique des pages** : Répartition équitable des pages entre les processus au lancement.
✔️ **Communication inter-processus via sockets TCP/IP** : Chaque processus communique avec les autres pour synchroniser les accès mémoire.
✔️ **Utilisation de `pthread` et `semaphore` pour la synchronisation**.
✔️ **Lancement automatisé des processus distants via SSH avec `dsmexec`**.
✔️ **Filtrage et redirection des sorties standard et erreur des processus distants**.
✔️ **Interopérabilité avec d'autres implémentations DSM via des structures de communication normalisées**.

---

## 🔧 Prérequis
📌 **Système** : Linux avec support des sockets et de la mémoire partagée.
📌 **Compilateur** : GCC
📌 **Outils** : `make`, `valgrind`, `ssh`, `netstat`, `ps`
📌 **Bibliothèques** : `pthread`, `semaphore`, `sys/mman.h`, `fcntl.h`

---

## 📦 Installation
```sh
# Cloner le dépôt
git clone https://github.com/abdelaaziz0/MEMOIRE_DISTRIBUEE.git

# Compiler le projet
make
```

Après compilation, les exécutables seront placés dans `bin/`.

---

## 🚀 Utilisation
### 1️⃣ Lancement des processus DSM
```sh
./dsmexec machine_file executable [args...]
```

Exemple :
```sh
./dsmexec machines.txt ./exemple
```
Cela exécutera `./exemple` sur chaque machine listée dans `machines.txt`, en leur assignant des numéros de processus (DSM_NODE_ID).

### 2️⃣ Exécution d’un programme utilisant DSM
Un programme doit appeler `dsm_init()` pour initialiser la mémoire partagée et `dsm_finalize()` pour libérer les ressources à la fin.

Exemple :
```c
#include "dsm.h"

int main(int argc, char **argv) {
    char *pointer = dsm_init(argc, argv);
    printf("[%d] Adresse mémoire DSM: %p\n", DSM_NODE_ID, pointer);
    dsm_finalize();
    return 0;
}
```

---

## 📝 Explication des Modules
### 📌 Gestion des processus distants
- **`dsmexec.c`** :
  - Lit la liste des machines depuis `machine_file`.
  - Attribue un identifiant (`DSM_NODE_ID`) à chaque processus.
  - Lance les processus distants via `ssh` et `dsmwrap`.
  - Gère la communication entre les processus et le maître DSM.

- **`dsmwrap.c`** :
  - S’exécute sur chaque machine distante.
  - Récupère les informations envoyées par `dsmexec` (numéro de processus, ports de communication).
  - Initialise la connexion avec le serveur DSM et exécute le programme utilisateur.

### 📌 Gestion de la mémoire partagée distribuée
- **`dsm.c`** :
  - Alloue et protège les pages mémoire en fonction du propriétaire initial.
  - Capture les fautes d’accès mémoire et déclenche un échange de pages via TCP/IP.
  - Met à jour la table des pages avec les nouveaux propriétaires.
  - Implémente un thread de communication (`dsm_comm_daemon`) pour gérer les requêtes de pages.

- **`dsm.h`** :
  - Interface de la bibliothèque DSM, contenant `dsm_init()` et `dsm_finalize()`.

- **`dsm_impl.h`** :
  - Définit les structures de gestion des pages mémoire (propriétaire, état d'accès).
  - Contient les constantes système (`PAGE_SIZE`, `PAGE_NUMBER`).

### 📌 Communication et synchronisation
- **`common.c` / `common_impl.h`** :
  - Fonctions pour gérer les connexions entre processus DSM.
  - Gestion des sockets pour l’échange des requêtes de pages.
  - Synchronisation des accès mémoire avec des sémaphores.

---

## 🔍 Débogage et Outils utiles
🛠 **Détection des fuites mémoire**
```sh
valgrind ./dsmexec machine_file executable
```

🛠 **Visualiser les processus DSM en cours**
```sh
ps aux | grep dsm
```

🛠 **Vérifier les connexions réseau**
```sh
netstat -tulnp | grep dsm
```

🛠 **Afficher les messages des processus distants en direct**
```sh
tail -f logs/dsm_output.log
```

---

## 📂 Structure du projet
```
.
├── dsmexec.c        # Lanceur de processus DSM
├── dsmwrap.c        # Programme intermédiaire exécuté via SSH
├── dsm.c            # Gestion des pages mémoire distribuées
├── dsm.h            # Interface de la bibliothèque DSM
├── dsm_impl.h       # Structures et constantes DSM
├── common.c         # Fonctions d’utilitaires
├── common_impl.h    # Structures partagées entre dsmexec et dsmwrap
├── exemple.c        # Programme d’exemple utilisant DSM
├── Makefile         # Compilation automatisée
├── README.md        # Documentation du projet
├── bin/             # Répertoire des exécutables
```

---

## 📄 Licence
Ce projet est sous licence MIT. Voir le fichier `LICENSE` pour plus de détails.

