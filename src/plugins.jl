const PLUGIN_API_VERSION = v"1.0.0"

struct PluginMetadata
    id::Symbol
    name::String
    version::VersionNumber
    api_version::VersionNumber
    description::String
end

function PluginMetadata(id::Symbol, name::AbstractString, version::VersionNumber;
                        api_version::VersionNumber=PLUGIN_API_VERSION, description::AbstractString="")
    return PluginMetadata(id, String(name), version, api_version, String(description))
end

struct _PluginCommand
    label::String
    menu::Symbol
    callback::Function
end

struct _PluginRegistration
    commands::Dict{Symbol,_PluginCommand}
end

mutable struct _Plugin
    metadata::PluginMetadata
    path::String
    enabled::Bool
    commands::Dict{Symbol,_PluginCommand}
end

const _plugin_lock = ReentrantLock()
const _plugins = Dict{Symbol,_Plugin}()
const _loading_plugin_paths = Set{String}()

function _register_plugin_item!(registration::_PluginRegistration, id::Symbol, label::AbstractString,
                                callback::Function, menu::Symbol)
    occursin(r"^[a-zA-Z][a-zA-Z0-9_-]*$", String(id)) || throw(ArgumentError("Invalid plugin command ID: $id"))
    isempty(strip(label)) && throw(ArgumentError("A plugin menu label is required"))
    haskey(registration.commands, id) && throw(ArgumentError("Duplicate plugin command ID: $id"))
    registration.commands[id] = _PluginCommand(String(label), menu, callback)
    return id
end

"Register a zero-argument callback in the Plugins menu."
register_command!(registration, id::Symbol, label::AbstractString, callback::Function) =
    _register_plugin_item!(registration, id, label, callback, :command)

"Register an analysis callback taking the active structure and returning its result."
register_analysis!(registration, id::Symbol, label::AbstractString, callback::Function) =
    _register_plugin_item!(registration, id, label, callback, :analysis)

"Register a coloring callback taking the active structure and returning a 3 × N RGB matrix."
register_coloring!(registration, id::Symbol, label::AbstractString, callback::Function) =
    _register_plugin_item!(registration, id, label, callback, :color)

"""
    loadplugin!(path) -> PluginMetadata

Execute a trusted local Julia file that evaluates to a module with `PLUGIN`
metadata and `initialize_plugin!(registration)`. Menu registrations become
visible only after initialization succeeds. The plugin starts enabled.
"""
function loadplugin!(path::AbstractString)
    fullpath = realpath(path)
    isfile(fullpath) && endswith(lowercase(fullpath), ".jl") || throw(ArgumentError("Choose a Julia plugin file (.jl)"))
    lock(_plugin_lock) do
        already_loaded = any(plugin -> plugin.path == fullpath, values(_plugins))
        (already_loaded || fullpath in _loading_plugin_paths) && throw(ArgumentError("Plugin file is already loaded or loading"))
        push!(_loading_plugin_paths, fullpath)
    end
    try
        namespace = Module(gensym(:JlMolPlugin))
        implementation = Base.include(namespace, fullpath)
        implementation isa Module && parentmodule(implementation) === namespace ||
            throw(ArgumentError("Plugin file must end with its module definition"))
        return Base.invokelatest(_initialize_plugin!, implementation, fullpath)
    finally
        lock(_plugin_lock) do
            delete!(_loading_plugin_paths, fullpath)
        end
    end
end

function _initialize_plugin!(implementation::Module, path::String)
    isdefined(implementation, :PLUGIN) || throw(ArgumentError("Plugin module must define PLUGIN metadata"))
    metadata = implementation.PLUGIN
    metadata isa PluginMetadata || throw(ArgumentError("PLUGIN must be a PluginMetadata value"))
    occursin(r"^[a-zA-Z][a-zA-Z0-9_-]*$", String(metadata.id)) || throw(ArgumentError("Invalid plugin ID: $(metadata.id)"))
    isempty(strip(metadata.name)) && throw(ArgumentError("A plugin name is required"))
    required = metadata.api_version
    required.major == PLUGIN_API_VERSION.major && required <= PLUGIN_API_VERSION ||
        throw(ArgumentError("Plugin requires API $required, but this app provides $PLUGIN_API_VERSION"))
    lock(_plugin_lock) do
        haskey(_plugins, metadata.id) && throw(ArgumentError("Plugin ID $(metadata.id) is already loaded"))
    end
    isdefined(implementation, :initialize_plugin!) || throw(ArgumentError("Plugin module must define initialize_plugin!"))
    registration = _PluginRegistration(Dict{Symbol,_PluginCommand}())
    implementation.initialize_plugin!(registration)
    lock(_plugin_lock) do
        haskey(_plugins, metadata.id) && throw(ArgumentError("Plugin ID $(metadata.id) is already loaded"))
        _plugins[metadata.id] = _Plugin(metadata, path, true, copy(registration.commands))
    end
    return metadata
end

"Return the loaded plugins and their enabled state for this Julia session."
function plugins()
    return lock(_plugin_lock) do
        [ (metadata=plugin.metadata, path=plugin.path, enabled=plugin.enabled)
          for (_, plugin) in sort!(collect(_plugins); by=first) ]
    end
end

function _loaded_plugin(id::Symbol)
    plugin = get(_plugins, id, nothing)
    plugin === nothing && throw(ArgumentError("Plugin $id is not loaded"))
    return plugin
end

"Enable or disable a plugin's commands for this session."
function enableplugin!(id::Symbol, enabled::Bool)
    lock(_plugin_lock) do
        _loaded_plugin(id).enabled = enabled
    end
    return enabled
end

"Invoke a command by plugin and command ID, using the active structure for analyses and coloring."
function run_plugin_command!(plugin_id::Symbol, command_id::Symbol)
    command = lock(_plugin_lock) do
        plugin = _loaded_plugin(plugin_id)
        plugin.enabled || throw(ArgumentError("Plugin $plugin_id is disabled"))
        registered = get(plugin.commands, command_id, nothing)
        registered === nothing && throw(ArgumentError("Plugin $plugin_id has no command $command_id"))
        registered
    end
    if command.menu == :command
        return Base.invokelatest(command.callback)
    end
    structure = active_structure()
    structure.id != 0 || throw(ArgumentError("Open a structure before running this plugin tool"))
    result = Base.invokelatest(command.callback, structure)
    command.menu == :color && return setatomcolors!(structure, result)
    return result
end

# The desktop transfers plain strings as typed data, including literal filenames and labels.
function _plugin_request(operation::String, argument::String, item::String)
    if operation == "load"
        metadata = loadplugin!(argument)
        return [metadata.name, string(metadata.version)]
    elseif operation == "plugins"
        fields = String[]
        for entry in plugins()
            metadata = entry.metadata
            append!(fields, [String(metadata.id), metadata.name, string(metadata.version), metadata.description,
                             entry.enabled ? "1" : "0"])
        end
        return fields
    elseif operation == "enable"
        enableplugin!(Symbol(argument), item == "1")
        return String[]
    elseif operation == "items"
        return lock(_plugin_lock) do
            fields = String[]
            for (id, plugin) in sort!(collect(_plugins); by=first)
                plugin.enabled || continue
                for (command_id, command) in sort!(collect(plugin.commands); by=first)
                    String(command.menu) == argument || continue
                    append!(fields, [String(id), String(command_id), command.label, plugin.metadata.name])
                end
            end
            fields
        end
    elseif operation == "run"
        result = run_plugin_command!(Symbol(argument), Symbol(item))
        output = result === nothing ? "" : result isa AbstractString ? String(result) :
                 sprint(show, MIME"text/plain"(), result; context=:limit => true)
        return [output]
    end
    throw(ArgumentError("Unknown plugin request: $operation"))
end
