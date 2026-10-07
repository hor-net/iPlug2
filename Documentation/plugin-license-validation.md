# HoRNet plugin licensing: intentional startup rejection

User-confirmed policy, 2026-10-07: **for all HoRNet plugins, startup failure/crash
when the user's required license is absent is intentional.** It is a licensing
requirement, not a scanner/rendering bug to suppress.

- Preserve the existing authentication/integrity checks and intentional rejection
  behavior. Do not remove `abort()`, bypass the checksum, silently accept missing
  license data or substitute an unlicensed instance just to make scanning pass.
- A full Release/template distribution may require personalization before it can
  be loaded. Failure of an unpersonalized package is expected and does not prove
  a customer regression. Debug/demo behavior is not equivalent to licensed Release.
- Investigate scanning/loading customer reports with the actual format/platform
  and a valid personalized package. Distinguish missing/invalid licensing from
  inability to locate/read **valid** license data (for example, path encoding),
  deployment/signature/architecture failures and unrelated UI/DSP problems.
- If correcting valid-license loading, ensure that valid licenses load while
  missing/invalid licenses still undergo the existing intentional rejection.
- macOS resource reads must not use `CFStringGetCStringPtr` as the only path
  conversion: it is an optional zero-copy hint and can return `nullptr` for valid
  Japanese/accented paths, turning a valid license into an aborted scan. Use a
  `CFStringGetCString(..., kCFStringEncodingUTF8)` owned buffer (TotalEQ:
  `MacResourcePath.h`). Apply the same pattern to every plugin sharing this
  md5/user.dat mechanism. Regression matrix:
  `Projects/HoRNetTotalEQMK2/tests/run_license_regression.sh`.
- Isolate such tests in disposable host processes/packages. Never overwrite the
  user's installed binary/config/license. Never commit or print license data,
  customer personalizations, hashes/tokens, or credentials; do not distribute QA
  personalizations.

Relevant TotalEQ example: macOS full Release reads `Contents/Resources/user.dat`
and verifies its owner/hash at construction. In July 2026's local 2.0.7 package,
an unpersonalized `user.dat` is a template, not an install-ready customer license.
A valid-license path-handling defect (`CFStringGetCStringPtr` returning nullptr
for Unicode paths) was reproduced with identical license bytes passing from an
ASCII path and failing from Japanese/accented paths, and has since been fixed in
TotalEQ (`MacResourcePath.h`, `CFStringGetCString` UTF-8). The 9-case matrix
demonstrates the fix loading valid Unicode licenses while preserving intentional
rejection for missing/bad/template data. This was not established as the cause of
support ticket 4177490 (#54/#62), and other plugins with the same mechanism still
need the fix ported.
