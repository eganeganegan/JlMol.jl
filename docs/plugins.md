# Julia plugins

Open a structure, then choose **Plugins > Load Local Plugin** and select [StructureTools.jl](../examples/plugins/StructureTools.jl). The chooser starts in the per-user JlMol `plugins` folder when available. You can also select a `.jl` file elsewhere on your computer.

- **Plugins > Commands > Show Space Filling** changes the active structure's representation.
- **Analysis > Plugins > Structure Summary** shows atom count, bond count, and radius of gyration in the Analysis Workspace.
- **Color > Plugins > Distance from Center** colors atoms by distance from the structure's center. The palette follows the same atoms during playback. Choose a built-in color scheme to replace it.

Use **Plugins > Enabled Plugins** to see plugin names, versions, and descriptions and to toggle their actions. Disabling a plugin keeps any representation or colors it already applied. Plugins start enabled when loaded. Loading and toggling apply to the current session. To use a modified version of a loaded file, restart JlMol and load it again.

Load plugins only from people and sources you trust. A plugin executes as Julia code with the same access as the app. JlMol executes a plugin file only when you select it or call `loadplugin!`. Compatibility checks run after the module loads and before its initializer runs.

## Write a plugin

Save a Julia file whose last expression is the plugin's module definition. Define `PLUGIN` and `initialize_plugin!` in the module. Plugin IDs must be unique within a session. Command IDs must be unique within their plugin. IDs start with an ASCII letter and may also contain digits, underscores, and hyphens.

```julia
module MyProteinTools
using JlMol

const PLUGIN = PluginMetadata(:my_protein_tools, "My Protein Tools", v"0.1.0";
                              api_version=v"1.0.0", description="Protein display and analysis tools")

function initialize_plugin!(registration)
    register_command!(registration, :show_cartoon, "Show Cartoon", () -> setrepresentation!(:cartoon))
    register_analysis!(registration, :count_atoms, "Count Atoms", atom_count)
end

end
```

The API version states the minimum version your plugin needs within the same major version. Menu items are published together when the initializer succeeds. Keep initialization focused on registration so failures leave the molecular scene intact.

Command callbacks take no arguments. A returned string appears in the status bar. Analysis callbacks take an `ActiveStructure` and return text or a value to display in the Analysis Workspace. Coloring callbacks also take an `ActiveStructure` and return a `3 × N` matrix of red, green, and blue components in topology atom order, each in `[0, 1]`. `snapshot(structure)` provides coordinates and atom IDs in that order. Colors are copied into the scene when you run the action.

The Julia Console can use the same registry as the menus.

```julia
using JlMol
loadplugin!(joinpath(pkgdir(JlMol), "examples", "plugins", "StructureTools.jl"))
plugins()
run_plugin_command!(:structure_tools, :summary)
enableplugin!(:structure_tools, false)
```

## Verify the example

From the repository root after building on macOS, run the native plugin integration check.

```sh
JULIA_DEPOT_PATH="$PWD/build/julia-depot" ./build/jlmol.app/Contents/MacOS/jlmol \
  resources/1CRN.pdb --plugin-test examples/plugins/StructureTools.jl
```
