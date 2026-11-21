# Contributing to SchrödingerClip

Thank you for your interest in contributing to **SchrödingerClip**!

## Development Requirements

- **OS**: Windows 10 / Windows 11 (x64)
- **Compiler**: MinGW-w64 `g++` (C++17, zero external third-party dependencies)
- **Shell**: PowerShell 5.1+ or PowerShell 7 (`pwsh`)

## Build & Verify Locally

All pull requests **must** compile with zero warnings under `-Wall -Wextra -Werror` and pass 100% of both the adversarial boundary unit suite and the real OS COM clipboard E2E suite:

```powershell
powershell -ExecutionPolicy Bypass -File .\verify_e2e.ps1
```

## Architectural Non-Negotiables

Before submitting changes to the OLE/COM core (`src/ole/`), verify that every PR preserves all five invariants:

1. **Strict Lazy Evaluation (`0 Bytes` at `Ctrl+C`)**: Never perform format conversion, rasterization, temporary file writes, or network requests inside `WM_CLIPBOARDUPDATE`, `SuperpositionDataObject` construction, or `IDataObject::QueryGetData`.
2. **True Chunked Streaming (`TYMED_ISTREAM`)**: Remote virtual files must stream directly through `HttpChunkedStream::Read` in bounded 64 KB–256 KB slices (`< 8 MB` daemon RAM delta) — never buffer multi-megabyte remote bodies into `MemoryStream` or temp files.
3. **Premature-Collapse Shield**: Always keep `CanIncludeInClipboardHistory = 0`, `CanUploadToCloudClipboard = 0`, and RPC caller PID sniffer blocking intact.
4. **Non-Destructive Multi-Collapse**: Single `Ctrl+C` superpositions must keep clipboard sequence numbers stable across repeated heterogeneous `Ctrl+V` pastes.
5. **Clean STA Teardown**: Always release OLE clipboard handles via `OleSetClipboard(nullptr)` without deadlocks or dangling pointers.

## Submitting Pull Requests

1. Fork the repository and create a descriptive feature or fix branch.
2. Add unit/E2E assertions in `tests/edge_cases_test.cpp` or `tests/e2e_verify.cpp` covering your change.
3. Run `.\verify_e2e.ps1` and confirm 100% pass rate with 0 warnings.
4. Open a Pull Request describing the motivation, behavior change, and verification output.
