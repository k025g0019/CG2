# ImCoolBar

macOS Dock-style magnification bar for [Dear ImGui](https://github.com/ocornut/imgui).

## Features

- **Bubble magnification effect** -- Items smoothly scale up when the mouse approaches, mimicking the macOS Dock behavior (cos^8 curve).
- **Cursor pinning** -- The exact pixel under the cursor stays fixed as items grow, even for edge items.
- **Horizontal and Vertical** -- Supports both orientations via `ImCoolBarFlags_Horizontal` / `ImCoolBarFlags_Vertical`.
- **Configurable anchor** -- Position the bar anywhere on screen with `anchor` (viewport-relative, 0..1).
- **Per-bar settings** -- Normal/hovered size, animation speed, effect strength.
- **Built-in demo window** -- `ShowCoolBarDemoWindow()` lets you tweak all settings at runtime, with save/reset defaults.
- **Built-in metrics window** -- `ShowCoolBarMetrics()` to inspect internal state.
- **C API** -- All functions available as plain C wrappers for binding generators (cimgui, etc.).
- **DLL-ready** -- `IMCOOLBAR_API` macro for `__declspec(dllexport)` support.

![Demo](https://github.com/aiekick/ImEffects/blob/master/doc/ImCoolBar.gif)

## Demo App

A full demo is available in the [ImEffects](https://github.com/aiekick/ImEffects) repository or with this [Emscripten](https://aiekick.github.io/ImEffects/) demo

## Integration

### 1. Add files

Copy `ImCoolBar.h` and `ImCoolBar.cpp` into your project. Include `ImCoolBar.h` after `imgui.h`.

### 2. Usage

```cpp
#include "ImCoolBar.h"

// Optional: check version at startup
IMCOOLBAR_CHECKVERSION();

// In your render loop
ImGui::ImCoolBarSettings settings;
settings.normalSize = 40.0f;
settings.hoveredSize = 150.0f;
settings.anchor = ImVec2(0.5f, 1.0f);  // bottom-center

if (ImGui::BeginCoolBar("MyDock", ImCoolBarFlags_Horizontal, settings)) {
    for (int i = 0; i < itemCount; ++i) {
        if (ImGui::CoolBarItem()) {
            float w = ImGui::GetCoolBarItemWidth();
            float s = ImGui::GetCoolBarItemScale();
            ImGui::ImageButton("##icon", icons[i], ImVec2(w, w));
        }
    }
    ImGui::EndCoolBar();
}
```

### 3. Demo window

```cpp
static bool showDemo = true;
static ImGui::ImCoolBarSettings settings;
static ImGui::ImCoolBarSettings defaults = settings;
ImGui::ShowCoolBarDemoWindow(&showDemo, &settings, &defaults);
```

## API

```cpp
namespace ImGui {
    bool BeginCoolBar(const char* aLabel, ImCoolBarFlags aCBFlags = ImCoolBarFlags_Vertical,
                      const ImCoolBarSettings& arSettings = {}, ImGuiWindowFlags aWinFlags = ImGuiWindowFlags_None);
    void EndCoolBar();
    bool CoolBarItem();
    float GetCoolBarItemWidth();
    float GetCoolBarItemScale();
    void ShowCoolBarMetrics(bool* apoOpen);
    void ShowCoolBarDemoWindow(bool* apoOpen = nullptr, ImCoolBarSettings* apoSettings = nullptr,
                               ImCoolBarSettings* apoDefaultSettings = nullptr);
    bool CoolBarDebugCheckVersion(const char* aVersion, size_t aSettingsSize);
}
```

## Settings

`ImCoolBarSettings` parameters:

| Parameter | Default | Description |
|---|---|---|
| `anchor` | `(0.5, 0.5)` | Viewport-relative position (0..1), clamped |
| `normalSize` | `40` | Item size when not hovered (pixels) |
| `hoveredSize` | `150` | Item size at maximum hover (pixels) |
| `animStep` | `0.05` | Animation speed factor (framerate-independent) |
| `effectStrength` | `0.5` | Bubble curve spread (0..1) |

## C API

All functions are available as C wrappers prefixed with `ImCoolBar_`:

```c
bool ImCoolBar_BeginCoolBar(const char* aLabel, int aCBFlags, float aNormalSize, float aHoveredSize,
                             float aAnimStep, float aEffectStrength, float aAnchorX, float aAnchorY, int aWinFlags);
void ImCoolBar_EndCoolBar(void);
bool ImCoolBar_CoolBarItem(void);
float ImCoolBar_GetCoolBarItemWidth(void);
float ImCoolBar_GetCoolBarItemScale(void);
void ImCoolBar_ShowCoolBarMetrics(bool* apoOpen);
void ImCoolBar_ShowCoolBarDemoWindow(bool* apoOpen,
    float* apoNormalSize, float* apoHoveredSize, float* apoAnimStep, float* apoEffectStrength, float* apoAnchorX, float* apoAnchorY,
    float* apoDefNormalSize, float* apoDefHoveredSize, float* apoDefAnimStep, float* apoDefEffectStrength, float* apoDefAnchorX, float* apoDefAnchorY);
const char* ImCoolBar_GetVersion(void);
int ImCoolBar_GetVersionNum(void);
```

## License

MIT License -- Copyright (c) 2024-2026 Stephane Cuillerdier (aka Aiekick)
