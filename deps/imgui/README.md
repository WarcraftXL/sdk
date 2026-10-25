# Dear ImGui

[Dear ImGui](https://github.com/ocornut/imgui) release **v1.91.9** (commit 97428e8a), MIT (see
`LICENSE.txt`). The library's own sources as they ship, without the demo, the examples and the
docs, and the two backends the runtime draws with: `backends/imgui_impl_dx9` and
`backends/imgui_impl_win32`. `imconfig.h` is untouched: the runtime and every mod compile the
same configuration, which is what lets them share one context.
