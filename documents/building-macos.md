<!--
SPDX-FileCopyrightText: 2024 shadPS4 Emulator Project
SPDX-License-Identifier: GPL-2.0-or-later
-->

## Build shadPS4QtLauncher for macOS

### Install the necessary tools and dependencies to build shadPS4QtLauncher:

First, make sure you have **Xcode 26.0 or newer** installed.

For installing other tools and library dependencies we will be using [Homebrew](https://brew.sh/).

First, install Homebrew:
```
# Installs Homebrew to /opt/homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
# Adds Homebrew to your path
echo 'eval $(/opt/homebrew/bin/brew shellenv)' >> ~/.zprofile
eval $(/opt/homebrew/bin/brew shellenv)
```

Then, use Homebrew to install the required build tools and dependencies:
```
brew install clang-format cmake qt@6
```

### Cloning and compiling:

Clone the repository recursively:
```
git clone --recursive https://github.com/shadps4-emu/shadPS4-qtlauncher.git
cd shadPS4-qtlauncher
```

Generate the build directory in the shadPS4-qtlauncher directory:
```
cmake -S . -B build/
```

Enter the directory:
```
cd build/
```

Use cmake to build the project:
```
cmake --build . --parallel$(sysctl -n hw.ncpu)
```

Now run the launcher:

```
./shadPS4QtLauncher.app/Contents/MacOS/shadPS4QtLauncher
```
