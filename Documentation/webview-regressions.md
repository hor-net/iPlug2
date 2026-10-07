# Shared WebView changes: mandatory plugin-update regression checks

When updating **any plugin** against this iPlug2 checkout, explicitly assess
whether it uses `IPlug/Extras/WebView/` or another modified shared framework path.
Do not assume a successful TotalEQ test validates another plugin. For WebView
plugins, run the checks below before considering the update release-ready.
For other UI backends, record WebView checks as not applicable, but still assess
shared API/timer changes and run that plugin's normal regressions.

## Local framework changes to track

- `769560fea`: initial JavaScript evaluations are no longer discarded solely
  because WKWebView reports loading; readiness/snapshots belong to a document.
- `ca1567765`: suspend delivery during navigation, detach/stop closed views,
  and add Debug-only lifecycle diagnostics.
- `b0f9a3c49`: guard out-of-order/double WebView teardown.
- Related shared behavior: `IPlug/IPlugAPIBase.cpp::OnTimer` drains processor
  parameter notifications for VST3 (used by TotalEQ LINK I/O).
- Where used, separately assess the plugin's jsiplug version: document identity
  notifications and spectrum-grid recovery are library changes, not iPlug2.

Revisit this inventory after merging upstream iPlug2 or changing these paths.
These are local candidate changes, not proof of compatibility across plugins,
hosts, formats or operating systems.

## Required checks for each affected plugin

- Build the actual affected formats/configurations; restart the host fully after
  replacing binaries. Confirm which binary/framework revision was tested.
- Open the editor in a fresh real host, then close/reopen repeatedly. Include
  **host startup autoload/project restore with the editor saved open**; opening
  manually after startup is a distinct case and does not cover it.
- Set non-default host parameters before opening. Verify controls, numeric
  readouts, EQ/graph points and curves reflect host state on the first opening,
  reopening and project restore. Initialization must not create host edits.
- Where applicable, verify duplicate readiness and a real document reload:
  no default-value reset, missing snapshot or premature JavaScript calls.
- Close while loading, then reopen; exercise multiple instances. Check for late
  callbacks, crashes, blank views and cross-instance state contamination.
- Check meters, grid and spectrum, resizing/theme/range changes, interaction and
  host-linked parameter updates. A structured screenshot alone proves none of
  these behaviors; DSP/validator success alone does not prove rendering.
- Run that plugin's existing DSP/preset/project-state regressions and format
  validators. Record host/OS/format, results and explicitly untested platforms.
  Windows/WebView2 paths changed here have not been runtime-certified.

## Existing helpers and unresolved baseline

TotalEQ provides native editor tests and isolated REAPER fresh-host/autoload
harnesses under `Projects/HoRNetTotalEQMK2/tests/`. Adapt fixtures/assertions to
other plugins; do not replay TotalEQ parameter identifiers as a generic test.
Its README documents exact commands, diagnostic artifacts and limitations.

**TotalEQ's cold REAPER autoload white UI remains unresolved.** LaunchServices
restoration with an editor saved open is reproducibly white locally; captured
GPU stacks are in WebKit audio-session/CoreAudio HAL initialization, followed by
GPU watchdog termination and loss of WebContent before JSREADY. Manual reopening
can succeed. Do not label this baseline fixed or waive another plugin's startup
checks because warm editor tests pass. The separate natural grid disappearance
has not been causally linked to that failure.

Debug macOS lifecycle logs: `/tmp/iplug-webview-<host-PID>.log`. Keep diagnostics
free of URLs, licensing data and parameter-message bodies. Never commit private
projects, credentials, raw user artifacts or GPU stack reports.
