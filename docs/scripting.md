# Julia scripting

The docked console evaluates real Julia source in a persistent `JlMolSession` module. Variables and function definitions remain available between executions. Standard output, standard error, results, and stack traces are displayed in the console.

```julia
using JlMol
protein = active_structure()
atom_count(protein)
setrepresentation!(protein, :spacefill)
colorby!(protein, :chain)
selected_atom()
```

Supported representations are `:ball_and_stick`, `:spacefill`, `:points`, and `:lines`. Supported color schemes are `:element`, `:chain`, and `:residue`.

The embedded session executes trusted local Julia code. It is not a security sandbox. Computation is synchronous in the current milestone; avoid long-running expressions during interactive work.

