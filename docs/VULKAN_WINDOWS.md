# Windows Vulkan foundation

The opt-in Windows/Linux Vulkan backend runs the shared RSX draw engine and
translates guest vertex/fragment programs from HLSL to SPIR-V with glslang.
The Windows authored host uses the same backend as Linux. This foundation
does not yet select Vulkan in a downstream game's executable.

## Dependencies and build

Use clang-cl from a Visual Studio x64 developer environment, CMake and Ninja.
Provide Vulkan headers, the SDL2 VC development package, and an installed
glslang/SPIRV package matching the selected build configuration. The GPU
driver supplies the system `vulkan-1.dll`; no Vulkan import library is linked.
The authored host copies SDL2.dll beside its executable.

The initial Windows check used Vulkan-Headers SDK 1.4.363.0, SDL2 2.32.10 and
glslang 16.6.0. The Windows workflow pins and checks the header/SDL archive
digests for its compile and missing-translator contract.

Example in PowerShell, after supplying dependency paths:

```powershell
cmake -S . -B build-vulkan -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release -DPS3RECOMP_RSX_VULKAN=ON "-DPS3RECOMP_VULKAN_INCLUDE_DIR=$env:VULKAN_SDK/Include" "-DSDL2_DIR=$env:SDL2_ROOT/cmake" "-DCMAKE_PREFIX_PATH=$env:GLSLANG_ROOT"
cmake --build build-vulkan --parallel 4 --target ps3recomp_host
python tests/check_rsx_vulkan_host.py build-vulkan/ps3recomp_host.exe --require-hardware --output temp/vulkan-hardware-check
```

`PS3RECOMP_VK_GUEST_PROGRAMS=1` requires a working translator and guest-program
setup. Missing translation fails initialization instead of selecting the
fixed-function contract. With translation deliberately disabled, configure
tests and run `rsx_vulkan_translation_required` to check this rejection.

## Hardware and validation checks

The host checker exercises fourteen authored scenes: clear, draw, depth,
quads, guest shader, textures in local/IO memory, mipmaps, render-to-texture,
depth texture, MRT with both routes, cube sampling and depth bounds. It
requires the real draw engine, successful pixel checks, and no skipped work.
`--require-hardware` rejects a selected CPU or virtual Vulkan device; device
identity and executable SHA256 are recorded with the logs.

For an installed Khronos validation layer, set `VK_LAYER_PATH` to its manifest
directory if it is not registered, then add `--validation` to the checker.
It requests `VK_LAYER_KHRONOS_validation`, requires the loader to report its
insertion, and rejects VUID/validation errors. Validation is a correctness
run, not a performance measurement.

The check is offscreen. For a separate window check, set
`PS3RECOMP_VK_WINDOW=1` before running the host directly. Existing window
failure handling can continue headless, so process exit alone is insufficient
window evidence: require the window/swapchain logs and an observed owned
visible window. The checker deliberately does not count that as tested.

## Remaining integration and performance work

Authored scenes establish backend contracts, not original title assets,
normal PLAY, gameplay, or a frame-rate target. Downstream native selection,
guest/device coherence and title-required capabilities need separate checks.
The current renderer also submits and fence-waits after individual operations;
moving APIs alone does not remove that CPU/GPU serialization or SPU work.

Use matched CPU/GPU timelines and timestamp spans before attributing a title's
bottleneck. Khronos documents [Vulkan profiling](https://docs.vulkan.org/guide/latest/profiling.html)
and the [effect of draining work versus fence-based retirement](https://docs.vulkan.org/samples/latest/samples/performance/wait_idle/README.html).
Those examples are guidance, not performance measurements for this project.
