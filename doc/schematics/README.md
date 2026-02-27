# Libtpa Schematics

This directory contains auto-generated Graphviz `.dot` schematics that show
interconnections among library-level functions and structures.

## Files

- `function_call_graph.dot`: directed edges `function -> function` for calls
  found inside `src/**/*.c` function bodies.
- `struct_relationship_graph.dot`: directed edges `struct -> struct` when one
  structure type is referenced inside another structure declaration.
- `function_struct_usage_graph.dot`: directed edges `function -> struct` when a
  function body references a structure type.

## Regenerate

From repository root:

```bash
python3 tools/schematics/generate_schematics.py
```

The script reads definitions from:

- `src/**/*.c` (function definitions)
- `include/**/*.h` and `src/**/*.c` (structure definitions)

## Viewers

These files are standard Graphviz DOT files and can be opened by software such
as Graphviz, xdot, OmniGraffle plugins, and online DOT viewers.
