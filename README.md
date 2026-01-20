# PolyglotCompiler

Multi-language compiler scaffold with frontends, shared IR, backends, runtime, and tools.

Status: skeleton only. See text.txt for the original requirements.

## Third-party dependencies

Dependencies are fetched automatically during CMake configure using `FetchContent` and stored in the `third_party/` folder:

- fmt (formatting)
- nlohmann/json (JSON)
- Catch2 (testing)
- mimalloc (allocator)
- ANTLR4 C++ runtime

You don't need to vendor the sources manually. Just configure the project (example for an out-of-source build):

```pwsh
cmake -S . -B build -G "Ninja"
cmake --build build
```

If you prefer to pre-populate `third_party/`, you can clone any of the above projects there (e.g. `third_party/fmt`) and CMake will reuse them instead of downloading.

Targets exposed for linking:

- `fmt::fmt-header-only`
- `nlohmann_json::nlohmann_json`
- `Catch2::Catch2WithMain` / `Catch2::Catch2`
- `mimalloc-static` / `mimalloc`
- `antlr4_shared` / `antlr4_static`

Example of linking in your own target:

```cmake
target_link_libraries(my_target PRIVATE fmt::fmt-header-only nlohmann_json::nlohmann_json)
```
