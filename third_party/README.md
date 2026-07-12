# Vendored third-party files

Single-file dependencies copied into the tree (no submodules, no package
manager). Update by replacing the file with a newer upstream revision and
recording the new version here.

| Path | Project | Version | License | Source |
|---|---|---|---|---|
| `stb/stb_truetype.h` | stb | v1.26 | public domain / MIT (dual, see end of file) | <https://github.com/nothings/stb> |
| `GL/glcorearb.h` | OpenGL Registry | generated core-profile header, © Khronos 2013–2020 | MIT (SPDX in file) | <https://github.com/KhronosGroup/OpenGL-Registry> |
| `KHR/khrplatform.h` | Khronos EGL Registry | © Khronos 2008–2018 | MIT-style (see file header) | <https://github.com/KhronosGroup/EGL-Registry> |

Notes:

- `stb_truetype.h` is the TrueType parser/rasterizer behind `src/text/`
  (implementation TU: `src/text/stb_impl.c`, built with warnings off). It does
  not bounds-check truncated font data against the buffer length, hence the
  "trusted fonts" note in `include/copal/font.h`.
- `glcorearb.h` + `khrplatform.h` serve the OpenGL renderer
  (`src/render/gl/`); they are generated headers from the Khronos registries
  and are only compiled when `COPAL_ENABLE_OPENGL` is set.
- Local modifications: none — all files are verbatim upstream copies.
