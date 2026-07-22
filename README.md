# WAYPLOT

**High-performance, low-power interactive 2D/3D graphing library for modern Linux**  
*C23 + Wayland + Vulkan 1.4*

A specialized, non-bloated built from the ground up for **beautiful visuals**, **smooth interaction**, and **near-zero CPU usage when idle**.

> **License**: This project is released into the **public domain** under the Unlicense.  
> You are free to use, modify, distribute, sell, or do anything you want with this code — **with or without attribution**.

---

## Features

- **True low-power design** — Blocking event loop + dirty-flag rendering + `VK_PRESENT_MODE_FIFO_KHR` → ~0% CPU when the graph is static
- **Modern Wayland + Vulkan 1.4** — Full use of latest protocols (xdg-shell, fractional-scale, presentation-time, explicit sync via drm syncobj) and Vulkan 1.4 features
- **Beautiful by default** — Crisp HiDPI rendering, clean typography, professional color palettes, smooth pan/zoom/rotate
- **Written in clean C23** — Highly optimized, minimal dependencies, compiler-friendly code
- **Full control** — You own the entire rendering and event pipeline (no hidden Python or OpenGL overhead)
- **Python-ready** — Stable public C API designed for easy `ctypes` wrapping and future native Python extension
- **Specialized for scientific visualization** — Focused exclusively on interactive 2D & 3D graphs (line, scatter, bar, surface, etc.)

---

## Why wayplot?

matplotlib is powerful but slow, dated-looking, and surprisingly CPU-heavy even for static plots. Most modern alternatives still rely on Python loops or older graphics APIs.

**wayplot** takes a different approach:

- We speak directly to the compositor via Wayland and the GPU via Vulkan 1.4.
- We only do work when something actually changes.
- We leverage the latest kernel improvements and explicit synchronization for maximum efficiency.

The result is a graphing library that feels native, looks modern, and respects your system resources.

---

## Current Status

**Phase 0 — Foundation Complete** (July 2026)

- Basic display of 3d plots in non resizable window
- Zero CPU usage and 10 to 25mb ram usage (near minimum gpu overhead)
- Unlicense (public domain)

---

## Technical Highlights

### Low-Power Architecture
- Wayland event loop blocks in the kernel when idle
- Rendering only occurs when the internal `dirty` flag is set
- FIFO present mode allows the compositor and GPU to power down between frames
- Explicit sync support (via modern Wayland + Vulkan extensions) for optimal driver behavior on recent kernels

### Error Handling
Every Vulkan and Wayland call is protected. We use a rich `GraphwayResult` enum with specific error codes, detailed logging (file + line), and graceful recovery paths (e.g. swapchain recreation on `VK_ERROR_OUT_OF_DATE_KHR`).

This makes development fast and the final library robust for long-running applications and Python bindings.

### Technology Stack
- **Language**: C23 (clang 22+)
- **Display**: Wayland (1.25+) with xdg-shell, presentation-time, fractional-scale-v1, linux-drm-syncobj-v1
- **Graphics**: Vulkan 1.4 (headers 1.4.350+)
- **Shaders**: GLSL compiled to SPIR-V via glslang / shaderc
- **Build**: Custom Makefile with LTO, sanitizers, and dependency tracking

---

## License

**Unlicense** — Public Domain

This project is released into the public domain. You can do **anything** you want with the code:

- Use it in commercial projects
- Modify it however you like
- Redistribute it (with or without changes)
- Sell it
- Remove all traces of the original author

**No attribution is required.**

See the [LICENSE](LICENSE) file for the full legal text.

---

## Acknowledgments

- The Wayland and Vulkan communities for building excellent modern APIs
- All the developers working on explicit synchronization and power-efficient graphics on Linux
- Everyone who wants better scientific visualization tools on Linux

---

*This README is part of the initial public release of the project foundation (July 2026).*
