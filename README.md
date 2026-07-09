## Overview
**dtcopy** is a command-line utility for the Windows environment.

The program allows conditional copying of entire directories based on file date and time stamps. Its main purpose is to create backup copies of one or more directories (including subdirectories), either updating existing files or copying them from scratch based on their timestamps or a specifically defined date.

It can create copies that include dependencies through a basic versioning mechanism, or generate a single compressed output file containing the main project alongside any related projects.

The comments within the source code, as well as the help screen accessible via the `-h` option, include several usage examples covering typical copy configurations. These examples can be helpful for both simple projects and more complex ones with dependencies on other directories.

When generating a single compressed output file, the code uses a modified version of the zLib, which enables the creation of a single `.gz` file containing all input files. The source code for this DLL is provided separately in the **zLibDll** repository.

## Project dependencies
Source files that are not part of the core **dtcopy** project but are used by it as external dependencies can be found in the **Include**, **Library** and **zLibDll** repositories. Therefore, to compile this project, you need to download the following components:

* [dtcopy](https://github.com/lpierge/dtcopy) — this project
* [Include](https://github.com/lpierge/Include) — Shared header (.h) files
* [Library](https://github.com/lpierge/Library) — Shared source (.c/.cpp) files
* [zLibDll](https://github.com/lpierge/zLibDll) — Modified zLib DLL project

## Implementation notes
This is a personal project. I wrote it for my own use to backup files on my laptop, and since I don't need Unicode support, the program only supports ANSI mode, not Wide (Unicode) mode.

The main reason the project targets x86 only (no x64 configuration) is that it uses an external DLL (zLibDll) for data compression, which is compiled for x86.

About the source code, even if most of the source files are named with the _.cpp_ extension, the code is mainly C, with a minimal use of basic C++ features, which is commonly labeled as _"C with classes"_ (usage of basic object-oriented concepts like classes, inheritance, polymorphism, but no modern C++ features like STL, templates, namespaces, etc.).

**Important note on projects structure:**

The Visual Studio project for **dtcopy** is hardcoded to search for dependencies using absolute paths starting from the root of a virtual L: drive. The expected directory structure is as follows:

```text
L:\
  |-- dtcopy\
  |-- Include\
  |-- Library\
  |-- zLibDll\
  |-- Lib\
```

The L:\Lib directory must be created manually. This is the directory where the output zLibDll DLL will be copied and where the dtcopy project looks for libraries and DLLs to compile and link.

Instead of changing the Visual Studio settings in the project file, I recommend mapping a local folder to a virtual L: drive with the Windows SUBST command:
- Create a directory on your local drive, for example `C:\DEV`.
- Download and extract all the repositories inside that directory.
- Open the Windows Command Prompt (press `Win + R` to open the Run dialog, type `cmd.exe` and press `Enter`) and from the Console run the following command: `SUBST L: C:\DEV`

## Windows binaries and Installer
The **Installer** directory of this repository contains a simple Installer with the compiled binaries for the Windows environment (`dtcopy.exe` and `zLibDll.dll`).

When running the Installer, if you get the _"Windows protected your PC"_ JOKE, do not worry, it's a default Microsoft Defender SmartScreen feature that blocks unrecognized apps. To bypass it, click the _"More info"_ link in the pop-up, then click _"Run anyway"_. dtcopy is a program I wrote, NOT a virus :) so you can safely say _"Yes"_ to the next Windows screen asking for authorization.

The **redist** directory of the **dtcopy** repository contains the `dtcopy.iss` script file used to create the Installer. This file is provided as a basic example to help you create an installer using the [Inno Setup](https://jrsoftware.org/isinfo.php) program.

Luca P.
