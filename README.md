# TelemFFB Panel for X-Plane

An X-Plane plugin that brings TelemFFB's in-sim panel into the cockpit: the
same settings and monitor the MSFS toolbar panel shows, drawn with
[Dear ImGui](https://github.com/ocornut/imgui) in an X-Plane window that works
on the desktop and in VR.

It talks to TelemFFB's local panel server (`http://127.0.0.1:9873`), the one
the MSFS toolbar panel uses, so TelemFFB must be running with its in-sim panel
turned on in System Settings. It does not replace the TelemFFB telemetry
plugin (`TelemFFB-XPP`), which is what drives force feedback; this one only
shows and edits TelemFFB's settings.

## Using it

- **Plugins > TelemFFB Panel > Show/Hide Panel**, or bind the command
  `TelemFFB/panel/toggle` to a key or VR controller button.
- In VR the panel follows X-Plane into the headset.

## Building

Windows, Visual Studio 2022 (v143 toolset):

```powershell
.\build.ps1            # Release
.\build.ps1 -Config Debug
```

The plugin is written to `TelemFFB-Panel\64\win.xpl`. To install it by hand,
copy the `TelemFFB-Panel` folder into `X-Plane\Resources\plugins\`.

## Layout

| Path | What |
|---|---|
| `src/plugin.cpp` | Plugin entry points, menu, command, VR |
| `src/ImGuiWindow.*` | An X-Plane modern window hosting Dear ImGui |
| `src/PanelWindow.*` | The panel's content |
| `src/ApiClient.*` | TelemFFB's panel server, polled on a thread of its own |
| `src/Json.*` | A small JSON reader for the server's responses |
| `third_party/XPSDK` | The X-Plane SDK (headers and Windows libraries) |
| `third_party/imgui` | Dear ImGui 1.92.9 and its OpenGL 2 backend |

## License

GNU General Public License v3 - see `LICENSE`. Dear ImGui is MIT licensed
(`third_party/imgui/LICENSE.txt`); the X-Plane SDK carries its own license
(`third_party/XPSDK/license.txt`).
