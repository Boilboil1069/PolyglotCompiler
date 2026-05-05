# Telemetry, Feedback and Crash Reports — Privacy Statement

**Telemetry, feedback and crash uploads are off by default.**
Nothing leaves your machine until you explicitly opt in, and you
may revoke consent at any time.

## Consent

[`ConsentManager`](../../tools/ui/common/telemetry/telemetry.h)
holds three states: `unknown` (never asked), `declined` and
`granted`.  Only `granted` allows collection or upload.
`Revoke()` returns the manager to `declined` — already-buffered
events are kept so you can inspect them, but they will never be
uploaded.

## Field allow-list

`FieldAllowList` is the explicit set of fields that the IDE may
attach to a telemetry event.  Anything that is not on the list
is **stripped** before the event reaches the local preview
buffer, and therefore can never be uploaded.  Each new event id
must declare its field allow-list at registration time.

## Local preview

`TelemetryBuffer` is the bounded, in-memory log of every event
the IDE has recorded since launch.  Open *Settings → Privacy →
Telemetry preview* to inspect the raw events that would be
uploaded.

## Crash reports

`CrashReportStore` always lands reports on disk first, never on
the network.  Each report carries the version, platform, signal
name, symbolised stack and an optional user comment.  Upload is
a separate gate (`MarkUploaded`) that the IDE only invokes after
explicit confirmation.  Pending reports remain locally
inspectable; you may delete them at any time with `Remove(id)`.

## What we never collect

* Source code, file contents, file paths inside your project.
* User names, machine names, IP addresses.
* Any field not present in the per-event allow-list.

## Auditing

Telemetry behaviour is fully covered by unit tests:
[`tests/unit/polyui/localization_test.cpp`](../../tests/unit/polyui/localization_test.cpp).
The tests verify that collection is off by default, that
disallowed fields are stripped, that revoking consent freezes
upload, and that crash reports stay pending until explicitly
acknowledged.
