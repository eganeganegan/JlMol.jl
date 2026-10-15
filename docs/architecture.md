# Architecture

JlMol is split into a Qt desktop shell, an OpenGL viewport, a native molecular core, a C ABI, and a Julia package/runtime.

## Authoritative state

`jlmol::MolecularScene` is the sole authority for the active molecule, representation, color scheme, and selected atom. It owns a shared molecular object and stores only stable atom identifiers in selections. The GUI's command dispatcher and the Julia C wrappers both mutate that same scene. The Julia module deliberately exposes `ActiveStructure`, not a native pointer.

The bridge registers a scene-change callback. Julia commands call the C ABI, the C ABI validates and mutates the scene, and the callback queues viewport and panel refreshes on Qt's GUI thread. Julia evaluation currently runs synchronously on the GUI thread; this is safe for the runtime and scene, but long-running work will temporarily block interaction. A later worker design must preserve Julia's thread-registration and Qt affinity requirements.

## Rendering

The initial renderer targets OpenGL 4.1 core, the newest OpenGL baseline available on macOS. Atoms are batched point-sprite sphere impostors and bonds are batched line geometry. GPU resources use Qt RAII wrappers and are destroyed while the widget context is current. Molecular data contains no OpenGL types, leaving room for a future Qt RHI, Metal, or Vulkan device implementation. OpenGL is deprecated by Apple, so it is a compatibility backend rather than the permanent macOS strategy.

## Coordinates and formats

The PDB loader treats coordinates and radii as ångströms. It keeps the first model, accepts blank or `A` alternate conformations, and infers bonds with covalent radii using a spatial grid. Stable IDs are assigned in accepted-file order. mmCIF and trajectory support are not claimed in the current milestone.

## Native ABI

`native/bridge/JlMolAPI.h` defines ABI version 1. Strings are UTF-8 and returned string pointers are thread-local and valid until the next string-returning call on the same thread. The application owns the scene handle; Julia never destroys it. Bulk coordinate access is intentionally deferred until trajectory support needs it.

