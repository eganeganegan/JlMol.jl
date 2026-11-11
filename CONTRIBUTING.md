# Contributing to JlMol

Read the [build guide](docs/building.md) to configure the desktop app. The [architecture guide](docs/architecture.md) describes how Qt, the native scene, and embedded Julia share state.

## Reproduce and verify a change

- For a bug report, include the JlMol version, operating system, steps to reproduce, and the smallest structure or trajectory you can share. Include the topology with a trajectory and say whether it contains periodic cell data.
- Run CTest and the Julia unit suite before submitting a change. Use the native graphics and trajectory checks for changes to rendering, input, playback, or the Julia bridge. See the [test commands](docs/building.md#tests).
- Keep numerical tests independent of the display. Use analytic cases or independently computed values when changing measurements, alignment, contacts, or trajectory analysis. Record units and tolerances with the case. See the [validation fixtures](docs/scientific_methods.md#numerical-validation).
- Changes to a desktop package should be checked by launching the installed or extracted package from a fresh environment. See the [packaging guide](docs/packaging.md).

Include the behavior changed and the checks you ran in the pull request description. Contributions are covered by the [MIT license](LICENSE).
