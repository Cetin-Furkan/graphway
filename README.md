# graphway — High-Performance Interactive 2D/3D Graphing Library

**C23 + Wayland + Vulkan 1.4** • Low Power • Modern Design • Zero Bloat • Python Bindings (planned)

A specialized, non-bloated replacement for matplotlib focused on **interactive scientific visualization** on modern Linux Wayland compositors. Built from the ground up for efficiency, beauty, and full hardware utilization using the latest kernel, Wayland protocols, and Vulkan 1.4 specifications.

---

## Table of Contents
- [Project Goals & Philosophy](#project-goals--philosophy)
- [Why This Stack? (Core Understanding)](#why-this-stack-core-understanding)
- [Low-Power & Efficiency Model](#low-power--efficiency-model)
- [Error Handling Philosophy (Critical Foundation)](#error-handling-philosophy-critical-foundation)
- [Current Status & Roadmap](#current-status--roadmap)
- [Development Environment](#development-environment)
- [Project Structure & Backbone](#project-structure--backbone)
- [Building](#building)
- [Version Control](#version-control)
- [Contributing / Collaboration](#contributing--collaboration)
- [License](#license)

---

## Project Goals & Philosophy

We are building **the** modern interactive plotting library for Linux that:

- Looks beautiful and professional by default (crisp HiDPI, clean typography, modern color palettes, smooth interaction).
- Runs at **near-zero CPU** when idle/static (true blocking event loop + smart dirty flags + FIFO present mode).
- Uses **Vulkan 1.4 + latest Wayland protocols** for explicit control, minimal overhead, and kernel-level efficiency.
- Is written in clean, optimized **C23** (no C++, minimal dependencies).
- Exposes a stable, easy-to-use C API that we will later wrap in Python (ctypes first, then proper extension).
- Replaces matplotlib for interactive 2D/3D graphs (line, scatter, bar, surface, etc.) while keeping the familiar "feel" but with 10-100x better performance and modern visuals.
- Remains specialized and non-bloated — every line of code serves graphing + interaction.

**Non-goals**: General UI toolkit, game engine, cross-platform (Linux/Wayland first), web export (future?).

---

## Why This Stack? (Core Understanding)

### Wayland (Display Server Protocol)
- Modern replacement for X11. Better security, fractional scaling, explicit sync (linux-drm-syncobj-v1), presentation feedback, viewporter, idle notifications.
- Your system has **wayland-1.25 + wayland-protocols-1.49** — excellent, recent versions.
- We use `xdg-shell`, `presentation-time`, `fractional-scale-v1`, `viewporter`, and `linux-drm-syncobj-v1` (explicit sync) for maximum efficiency and power savings.
- **Nuance**: Different compositors (Hyprland, Sway, GNOME, KDE) implement protocols slightly differently. We design defensively and test on multiple.

### Vulkan 1.4 (Graphics & Compute API)
- Your system has **vulkan-headers 1.4.350.1** — perfect. We target Vulkan 1.4 core + relevant extensions.
- Explicit control over memory, synchronization, pipelines, and presentation → we can achieve true low-power idle and maximum efficiency.
- WSI (Window System Integration) via `VK_KHR_wayland_surface` lets us create a `VkSurfaceKHR` directly from a Wayland `wl_surface`.
- Key modern features we will use:
  - Dynamic rendering (`VK_KHR_dynamic_rendering`)
  - Timeline semaphores
  - Present-wait / present-timing feedback
  - Explicit sync with Wayland drm syncobj (huge for power & correctness in 2025-2026 kernels)
- **Implication**: Much lower CPU overhead than OpenGL or matplotlib backends. GPU sleeps properly between frames.

### C23
- Modern C with `constexpr`, improved type inference hints, `[[nodiscard]]`, `static_assert`, better enums, etc.
- We write code the compiler loves: `restrict`, hot/cold attributes, branch prediction hints, no undefined behavior.
- Result: highly optimized binaries with `-O3 -flto -march=native`.

### Why Not Existing Libraries?
- matplotlib: slow Python, dated look, high CPU even when idle, poor 3D interaction.
- pyqtgraph / vispy / plotly: still Python-heavy or use older OpenGL.
- Dear ImGui + Vulkan examples: great for UIs but not specialized for scientific graphs + data handling.
- We own the entire stack → zero bloat, perfect integration between Wayland events ↔ Vulkan rendering.

---

## Low-Power & Efficiency Model (Detailed)

This is one of your core requirements. The design guarantees minimal resource use:

1. **Event-Driven Blocking Loop**
   - `wl_display_dispatch()` (or equivalent with poll) blocks the thread in the kernel when there are no events.
   - When the graph is static (no interaction, no live data), CPU usage drops to **~0%**.

2. **Dirty-Flag Driven Rendering**
   - We only record and submit Vulkan command buffers when something actually changed (data update, view pan/zoom/rotate, resize, hover tooltip).
   - Static frames = zero GPU work after the first present.

3. **Present Mode Strategy**
   - Default: `VK_PRESENT_MODE_FIFO_KHR` (vsync) — compositor can power-gate the display pipeline. Tear-free and power-efficient.
   - Optional (user flag): `VK_PRESENT_MODE_MAILBOX_KHR` for lower latency during heavy interaction.
   - We never use `IMMEDIATE` unless explicitly requested (tearing + higher power).

4. **Synchronization & Kernel Integration**
   - Proper semaphore + fence + timeline semaphore usage.
   - On modern kernels (your **linux-cachyos-rc** is bleeding-edge) + explicit sync via `linux-drm-syncobj-v1`, the driver can sleep the GPU between presents.
   - We will expose Wayland presentation feedback to further throttle if desired.

5. **GPU Selection**
   - Auto-prefer integrated GPU (Intel in your case) for power efficiency when it has the required features.
   - User can override via environment variable or API.

6. **Additional Techniques**
   - No busy-waiting anywhere.
   - Command buffer reuse where possible.
   - Minimal allocations in hot paths (arenas or pre-allocated pools).
   - When window is minimized or occluded, we can further reduce work (future).

**Result**: A static interactive graph window should be indistinguishable from idle in `htop` / `powertop`, while panning/zooming feels instant and smooth.

---

## Error Handling Philosophy (Critical Foundation — Do This Before Any Real Code)

We treat error handling as a **first-class citizen**. No silent failures, no crashes on bad input, excellent diagnostics.

### Core Principles
- Every public and important internal function returns a rich result type.
- Vulkan calls are always checked with a strict macro that logs file:line + result string + aborts in debug or returns error in release.
- Wayland proxy errors and global binding failures are handled gracefully with clear messages.
- Logging system with levels (ERROR, WARN, INFO, DEBUG, TRACE) that can be controlled at runtime or compile time.
- Context struct carries last error + human-readable message.
- For Python bindings later: errors become Python exceptions cleanly.

### Planned Error Type (in `include/graphway/graphway.h`)

```c
typedef enum GraphwayResult {
    GRAPHWAY_SUCCESS = 0,
    GRAPHWAY_ERROR_UNKNOWN,
    GRAPHWAY_ERROR_OUT_OF_MEMORY,
    GRAPHWAY_ERROR_VULKAN_INIT_FAILED,
    GRAPHWAY_ERROR_WAYLAND_CONNECT_FAILED,
    GRAPHWAY_ERROR_NO_SUITABLE_GPU,
    GRAPHWAY_ERROR_SURFACE_CREATION_FAILED,
    GRAPHWAY_ERROR_SWAPCHAIN_CREATION_FAILED,
    GRAPHWAY_ERROR_SHADER_COMPILATION_FAILED,
    GRAPHWAY_ERROR_INVALID_ARGUMENT,
    GRAPHWAY_ERROR_NOT_IMPLEMENTED,
    // ... more specific codes
} GraphwayResult;
```

We will also have:
- `const char* graphway_result_to_string(GraphwayResult r);`
- Detailed logging macros: `GW_LOG_ERROR`, `GW_LOG_WARN`, etc.
- `VK_CHECK(result, msg)` macro that does full diagnostic + returns on failure.
- Optional: stack traces or Vulkan object naming for validation layers.

**Nuance / Edge Cases Handled**:
- Out-of-date swapchain (`VK_ERROR_OUT_OF_DATE_KHR`) → recreate automatically.
- Device lost → attempt recovery or clean shutdown with message.
- Missing optional extensions/protocols → graceful fallback or clear warning.
- Multiple GPUs or no Wayland presentation support → clear error + suggestion.
- Protocol version mismatches → explicit check.

We will implement the full error + logging system in the first coding phase.

---

## Current Status & Roadmap

**Phase 0 (Now)**: Project foundation — README, structure, Makefile, error framework, version control. (This document + files)

**Phase 1**: Basic Wayland client + xdg_toplevel window + Vulkan 1.4 instance + device selection + Wayland surface + swapchain + clearing render loop with proper error handling and blocking idle.

**Phase 2**: 2D graph data structures, line/scatter rendering, basic pan/zoom interaction, dirty flag system, uniform buffer updates.

**Phase 3**: Text rendering (SDF or freetype), axes, grid, legends, tooltips, color palettes.

**Phase 4**: 3D graphs (perspective, orbit camera, depth), more plot types.

**Phase 5**: Python bindings (ctypes → extension), numpy integration, examples, documentation.

**Phase 6**: Polish, performance tuning, multiple windows/figures, export (PNG/PDF via Vulkan or other), animations, live updating.

---

## Development Environment

Your system is **ideal**:

- Arch Linux + cachyos repositories + **linux-cachyos-rc** kernel (latest improvements, excellent for new Vulkan/Wayland features)
- Wayland 1.25 + protocols 1.49
- Vulkan Headers **1.4.350.1** + validation layers + glslang + shaderc
- clang 22 (excellent C23 support)
- Intel GPU (from package list) — great for development (stable drivers, good power characteristics)

**Missing package you should install**:
```bash
sudo pacman -S vulkan-icd-loader
```

Then run the big upgrade you started (`sudo pacman -Syu`).

**Recommended tools**:
- `renderdoc` (for Vulkan debugging)
- `wayland-info`, `vulkaninfo`
- `htop`, `powertop`, `strace` (for power & syscall analysis)
- `clang-format`, `clang-tidy`

---

## Project Structure & Backbone

Clean separation between public API, internal implementation, generated code, and assets.

```
graphway/
├── README.md
├── .gitignore
├── Makefile                 # Advanced: protocols, shaders, debug/release, sanitizers, library + example
├── include/
│   └── graphway/
│       ├── graphway.h       # Public stable C API (for ctypes/Python later)
│       └── internal/        # Private headers (error, log, context structs)
├── src/
│   ├── graphway.c           # Public API implementation + context lifecycle
│   ├── wayland_client.c     # Wayland connection, registry, xdg_shell, input, protocols
│   ├── vulkan_context.c     # Instance, device selection (power-aware), surface, swapchain, queues
│   ├── renderer.c           # Command recording, pipelines, frame submission, dirty handling
│   ├── graph.c              # Plot data structures, series, view state, interaction logic
│   └── utils.c              # Error handling, logging, memory, string helpers
├── protocols/               # Generated by wayland-scanner (xdg-shell, presentation-time, etc.)
├── shaders/
│   ├── line.vert / line.frag
│   ├── point.vert / point.frag
│   ├── text.vert / text.frag (later)
│   └── compiled/            # .spv files (gitignored)
├── python/
│   └── graphway.py          # ctypes wrapper + high-level Python API (later)
├── docs/
│   └── architecture.md      # Detailed design docs (we will expand)
├── tests/                   # Unit/integration tests (future)
├── assets/                  # Fonts, default palettes, icons (future)
└── build/                   # All output (gitignored)
```

**Backbone Principles**:
- One context struct owns everything (Wayland display/surface, Vulkan instance/device/swapchain, graph state).
- Clear ownership and lifetime rules.
- All Vulkan objects are created/destroyed in strict reverse order.
- Wayland listeners are set once; we use user data pointers to reach back to our context.
- No global state.

---

## Building

See the advanced Makefile (created alongside this README). It supports:

- Protocol code generation (`wayland-scanner`)
- Shader compilation to SPIR-V
- Debug build (sanitizers, validation layers forced, symbols)
- Release build (LTO, native arch, stripped)
- Building both shared library and example binary
- `make format`, `make tidy` hints

Typical workflow:
```bash
make                # debug build
make release        # optimized
./build/graphway_example
```

---

## Version Control

We use **git** from day one for history, branching (feature/error-handling, phase1-window, etc.), and collaboration.

Commands to initialize (run on your machine):

```bash
cd ~/graphway          # or wherever you clone/work
git init
git add .
git commit -m "chore: initial project skeleton, README, Makefile, .gitignore, error philosophy"
```

We will create branches for each major phase.

---

## Contributing / Collaboration

This is a teaching + building project. We proceed step-by-step:

1. You review/improve this README + structure.
2. We finalize error handling + logging system (header + implementation).
3. We write the first working code: clean Wayland window + Vulkan surface that clears to a nice color and idles at 0% CPU.
4. Then add graph logic, interaction, etc.

All code will be heavily commented, follow strict style, and include error paths.

---

