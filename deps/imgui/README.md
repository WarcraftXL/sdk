# Dear ImGui

[Dear ImGui](https://github.com/ocornut/imgui) release **v1.91.9** (commit 97428e8a), MIT (see
`LICENSE.txt`). The library's own sources as they ship, without the demo, the examples and the
docs, and the backends the runtime draws with: `backends/imgui_impl_dx9` and
`backends/imgui_impl_win32` (the client's Direct3D 9 device and its window), and
`backends/imgui_impl_vulkan` (the runtime's Vulkan renderer, which compiles it over its own loader). `imconfig.h` is untouched: the runtime and every mod compile the
same configuration, which is what lets them share one context.
