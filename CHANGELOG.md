# Changelog

All notable changes to **SchrödingerClip** are documented in this file.

## [1.0.1] - 2026-11-22

### Fixed
- **Zero-Network `QueryGetData` Guarantee**: Separated `allowNetworkProbe` between `IDataObject::QueryGetData` (`false`) and `IDataObject::GetData` (`true`), and added an obvious non-image extension fast-path (`.bin`, `.zip`, `.iso`, `.pdf`, `.mp4`, etc.) so probing clipboard formats never triggers premature `HTTP HEAD` requests.
- **Headless CI Determinism**: Pinned explicit Explorer / Terminal observer contexts inside `edge_cases_test.cpp` so `QueryGetData(CFSTR_FILECONTENTS)` and `GetData` checks remain 100% deterministic even when launched from a foreground `pwsh.exe` / `conhost.exe` terminal on GitHub Actions `windows-latest`.
- **LaTeX Card Header Glyph**: Fixed wide-string escape `L"\u03A8"` in `RenderMathFormulaBitmap2x` so the Quantum Equation Typesetter badge renders crisp Greek `Ψ` instead of `Î¨`.
- **Graceful Shutdown & COM Stream Lifetime Safety**:
  - Replaced blocking `SendMessageW` calls in `ObservatoryHttpServer` with bounded `SendMessageTimeoutW(..., SMTO_ABORTIFHUNG, ...)`.
  - Added active handler draining in `ObservatoryHttpServer::Stop()` to prevent races on teardown.
  - Promoted `TelemetryHub` to `std::shared_ptr` and captured `std::weak_ptr` inside `HttpChunkedStream` completion callbacks.

### Added
- **Observatory UI Enhancements**:
  - Exposed `POST /api/disarm` and added a one-click **Disarm** (`清空叠加态`) button in the Quantum Observatory header.
  - Added `Lock JS/TS Async fetch()` (`mode=10`) to the manual lock selector and a Unix timestamp preset in the Sandbox Playground.
  - Refined Star Graph orbit radii so all 10 eigenstate orbit labels render cleanly without clipping.
- **Repository Maintenance & Release Automation**:
  - Added CI binary artifact uploads, tag-triggered GitHub Release workflow (`release.yml`), `CONTRIBUTING.md`, `CHANGELOG.md`, and structured Issue templates.
  - Expanded adversarial unit suite from 47 to 53 assertions (105/105 total across edge-case + E2E suites).

## [1.0.0] - 2026-11-18

### Added
- Native C++17 Win32/COM STA clipboard daemon (`OleSetClipboard` + custom `IDataObject` + `IEnumFORMATETC` + chunked `WinHTTP` `TYMED_ISTREAM` virtual file streaming).
- Dual-layer premature-collapse shield (`CanIncludeInClipboardHistory = 0`, `CanUploadToCloudClipboard = 0`, `ExcludeClipboardContentFromMonitorProcessing = 1` + RPC caller PID sniffer blacklist).
- Sub-millisecond dual-layer observer detector (host process + active window title micro-context).
- Five built-in local zero-API polymorphic superposition sources (Remote URLs, JSON/CSV/YAML structured data, Code/SVG/Mermaid/LaTeX visual sources, cURL commands, Local files / `#HEX` colors / Unix timestamps).
- Real-time Quantum Clipboard Observatory web console (`http://127.0.0.1:17890`), flicker-free HiDPI desktop micro-HUD (`WS_EX_NOACTIVATE`), system tray menu, and global hotkeys (`Alt+Shift+V`, `Alt+Shift+O`).
- Automated real-OS COM clipboard E2E verification suite (24 MB `TYMED_ISTREAM` SHA-256 byte-for-byte integrity + `< 8 MB` RAM delta proof).
