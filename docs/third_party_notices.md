# Third-party notices

JlMol's source is distributed under the MIT license. Desktop bundles also contain separately licensed libraries and data. Keep their notices with redistributed binaries.

| Component | Distribution materials |
| --- | --- |
| Qt | JlMol links dynamically to Qt Core, Gui, Widgets, OpenGL, and OpenGLWidgets. Qt deployment may include additional libraries and plugins. Keep the matching Qt license texts, third-party notices, and corresponding source available with each public binary release. [Qt licensing requirements](https://www.qt.io/development/open-source-lgpl-obligations) · [Qt source archives](https://download.qt.io/official_releases/qt/) |
| Julia | The complete Julia runtime is copied into the package, including its `LICENSE.md` and accompanying materials. Its bundled dependencies retain their own licenses. [Julia license and dependency notices](https://github.com/JuliaLang/julia/blob/master/LICENSE.md) |
| Chemfiles 0.10.4 | The native library uses the source pinned at commit `843e5949db829dcf76bac1ca3521cc8e10f9ea4e` with JlMol's rotated-cell patch. Its license is installed as `licenses/Chemfiles.txt` in the bundled JlMol project. [Pinned source and license](https://github.com/chemfiles/chemfiles/tree/843e5949db829dcf76bac1ca3521cc8e10f9ea4e) |
| Chemfiles dependencies | The bundled source contains fmt, toml11, TNG, molfile plugins, mmtf-cpp, pugixml, and Gemmi headers, plus codecs when built internally. Available license and copyright files from the configured dependency build are copied into `licenses/`. Individual headers can carry additional notices. [Pinned dependency archives](https://github.com/chemfiles/chemfiles/tree/843e5949db829dcf76bac1ca3521cc8e10f9ea4e/external) |
| System and compiler libraries | Packages may include libraries selected by Qt deployment or the native compiler's redistributable rules. Record the libraries and their matching redistribution materials for each release build. [Qt deployment](https://doc.qt.io/qt-6/deployment.html) · [Microsoft Visual C++ redistribution](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files) |
| Crambin coordinates | `resources/1CRN.pdb` comes from the RCSB PDB. Documentation images were rendered from this structure in JlMol. [1CRN entry](https://www.rcsb.org/structure/1CRN) · [RCSB usage and citation policy](https://www.rcsb.org/pages/policies) |
| Windows CI software OpenGL | Package verification downloads a pinned Mesa llvmpipe build as the runner's graphics driver. It is used during CI verification. [Build source](https://github.com/mmozeiko/build-mesa) · [Mesa licensing](https://docs.mesa3d.org/license.html) |

## Public binary releases

Keep the exact JlMol commit, dependency versions, build configuration, source links, and package hashes with each release. Include the matching Qt license texts and corresponding source materials, compiler redistribution materials, and notices for the libraries actually carried by the artifact. The current packaging steps copy the JlMol license, Julia runtime materials, and available Chemfiles dependency notices. The dependency inventory for a public binary should be checked against that binary's deployed libraries.
