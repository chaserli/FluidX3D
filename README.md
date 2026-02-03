# FluidX3D (macOS native attempt)

This branch is a macOS‑only, Metal‑backend attempt to run FluidX3D natively. It strips Linux/X11/Windows/OpenCL build paths and focuses on a single platform target to simplify iteration.

## Constraints
- No Linux/X11/Windows code paths in this branch.
- OpenCL runtime/device code is not used at build time. Kernel source is still derived from the OpenCL C template and translated to Metal Shading Language at runtime.
- `makefile` is the only supported build entrypoint.

