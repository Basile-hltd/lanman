# lanman

Un mini serveur de fichiers pour échanger rapidement des fichiers entre tes appareils (ordinateur, téléphone, tablette…) sur ton réseau local, via un simple navigateur.

```
$ lanman
lanman partage /Users/moi/Partage
  -> http://192.168.1.42:53817
  -> http://localhost:53817
```

Ouvre l'adresse affichée sur n'importe quel appareil du même réseau, et c'est tout.

- Un seul exécutable natif (~300 Ko), aucune dépendance, macOS / Linux / Windows
- Liste les fichiers et sous-dossiers du dossier partagé, navigation dans l'arborescence
- Téléchargement des fichiers (lecture vidéo/audio directement dans le navigateur)
- Dépôt de fichiers depuis la page : bouton ou glisser-déposer, plusieurs fichiers à la fois
- Impossible de sortir du dossier partagé (`../`, liens symboliques, noms piégés…)
- Un fichier déposé n'écrase jamais un fichier existant (`photo (1).jpg`)
- Choisit un port libre au hasard et affiche l'adresse IP locale à utiliser

## ⚠️ Précautions d'usage

**lanman est prévu pour un usage personnel, ponctuel, sur un réseau privé de confiance** (ta maison, ton propre Wi-Fi).

- **Aucun chiffrement** : tout passe en HTTP clair. Quelqu'un sur le même réseau peut voir les fichiers qui transitent.
- **Aucune authentification** : toute personne connectée au même réseau qui connaît (ou devine) l'adresse et le port peut **lire tous les fichiers** du dossier partagé **et y déposer des fichiers**.
- **Ne l'utilise pas** sur un Wi-Fi public (café, gare, hôtel, coworking…) ni sur un réseau d'entreprise.
- **Ne l'expose jamais sur Internet** (pas de redirection de port sur ta box, pas de tunnel).
- **Partage un dossier dédié**, pas ton dossier personnel : les fichiers cachés (`.ssh`, `.env`…) sont aussi visibles. Par défaut, c'est le dossier courant qui est partagé.
- **Arrête-le (Ctrl+C) dès que tu as fini** ton échange.

L'outil est fourni tel quel, sans aucune garantie.

## Utilisation

```
lanman [options]

  -p, --port <n>     port d'écoute (défaut : un port libre au hasard)
  -H, --host <addr>  adresse d'écoute (défaut 0.0.0.0 = tout le réseau local)
  -d, --dir <path>   dossier à partager (défaut : dossier courant)
  -v, --version      affiche la version
  -h, --help         affiche l'aide
```

Exemples :

```sh
lanman                        # partage le dossier courant
lanman -d ~/Partage -p 8080   # partage ~/Partage sur le port 8080
lanman -H 127.0.0.1           # accessible uniquement depuis cette machine
```

Au premier lancement, le pare-feu de ton système peut demander d'autoriser les connexions entrantes : il faut accepter pour que les autres appareils puissent se connecter.

## Installation

Télécharge l'archive correspondant à ton système depuis la page **[Releases](../../releases/latest)** :

| Système | Fichier |
|---|---|
| macOS (Apple Silicon et Intel) | `lanman-macos-universal.tar.gz` |
| Linux x64 (toutes distributions) | `lanman-linux-x64.tar.gz` |
| Windows x64 | `lanman-windows-x64.zip` |

Puis ajoute-le au `PATH` pour pouvoir taper `lanman` depuis n'importe quel dossier.

### macOS

```sh
cd ~/Downloads
tar -xzf lanman-macos-universal.tar.gz
# Le binaire n'est pas signé : retire l'attribut de quarantaine ajouté par le navigateur
xattr -d com.apple.quarantine lanman 2>/dev/null
sudo mkdir -p /usr/local/bin
sudo mv lanman /usr/local/bin/
lanman --version
```

`/usr/local/bin` est déjà dans le `PATH` par défaut sur macOS.

### Linux

```sh
cd ~/Downloads
tar -xzf lanman-linux-x64.tar.gz
mkdir -p ~/.local/bin
mv lanman ~/.local/bin/
lanman --version
```

Si la commande n'est pas trouvée, ajoute `~/.local/bin` au `PATH` (dans `~/.bashrc` ou `~/.zshrc`) puis ouvre un nouveau terminal :

```sh
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
```

(Ou `sudo mv lanman /usr/local/bin/` pour l'installer pour tous les utilisateurs.)

### Windows

1. Extrais `lanman-windows-x64.zip` (clic droit → *Extraire tout*).
2. Crée un dossier, par exemple `C:\Users\<toi>\bin`, et places-y `lanman.exe`.
3. Ajoute ce dossier au `PATH` : menu Démarrer → tape *variables d'environnement* → *Modifier les variables d'environnement pour votre compte* → sélectionne `Path` → *Modifier* → *Nouveau* → colle le chemin du dossier → *OK*.

   Ou en une commande dans PowerShell :

   ```powershell
   [Environment]::SetEnvironmentVariable("Path", [Environment]::GetEnvironmentVariable("Path", "User") + ";$env:USERPROFILE\bin", "User")
   ```

4. Ouvre un **nouveau** terminal et tape `lanman --version`.

Windows SmartScreen peut avertir que l'exécutable n'est pas signé : *Informations complémentaires* → *Exécuter quand même*.

## Compiler depuis les sources

Prérequis : CMake ≥ 3.20 et un compilateur C++17 (clang, gcc ou MSVC). La bibliothèque [cpp-httplib](https://github.com/yhirose/cpp-httplib) est téléchargée automatiquement.

```sh
cmake -S . -B build
cmake --build build --config MinSizeRel
```

Les binaires des releases sont produits par GitHub Actions (`.github/workflows/build.yml`) à chaque tag `v*`.
