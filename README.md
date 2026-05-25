**dtcopy** is a command-line utility for the Windows environment.

The program allows conditional copying of entire directories based on file date and time stamps. Its main purpose is to create backup copies of one or more directories (including subdirectories), either updating existing files or copying them from scratch based on their timestamps or a specifically defined date.

It can create copies that include dependencies through a basic versioning mechanism, or generate a single compressed output file containing the main project alongside any related projects.

The comments within the source code, as well as the help screen accessible via the `-h` option, include several usage examples covering typical copy configurations. These examples can be helpful for both simple projects and more complex ones with dependencies on other directories.

When generating a single compressed output file, the code uses a modified version of the zLib, which enables the creation of a single `.gz` file containing all input files. The source code for this DLL is provided separately in the **zLibDll** repository.

Source files that are not part of the core `dtcopy` project but are used by it as external dependencies can be found in the **Include** and **Library** repositories.

Therefore, to compile this project, you need to download the following components:
* [Include](https://github.com/lpierge/Include) — Shared header (.h) files
* [Library](https://github.com/lpierge/Library) — Shared source (.c/.cpp) files
* [zLibDll](https://github.com/lpierge/zLibDll) — Modified zLib DLL project

Regarding the the source code, most of it is specific to the _Windows platform_. Even if most of the source files are _.cpp_ files, the source code is mainly C code with a minimal use of basic C++ features, which is commonly labeled as _"C with classes"_.

**Important note on project structure:**
The Visual Studio projects for dtcopy and zLibDll are hardcoded to search for dependencies using absolute paths starting from the root of a virtual L: drive. The expected directory structure is as follows:

```text
L:\
  |-- Include\
  |-- Library\
  |-- dtcopy\
  |-- zLibDll\
```
   
If you want to compile the projects without reconfiguring the Visual Studio settings, you can map a local folder to a virtual L: drive using the Windows SUBST command:
Create a directory on your local drive (for example, C:\DEV).
Download and extract all four repositories inside that directory.
Open the Windows Command Prompt (cmd) and run the following command:
`SUBST L: C:\DEV`
Note: to remove the virtual drive you can run `SUBST L: /d`.

Luca P.
