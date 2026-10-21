# Component: Host Timezone

**Role:** Detects the host timezone as a zoneinfo name (e.g. `Asia/Tokyo`) and picks the timezone reported to the guest.
**Location:** `hardware/generic/goldfish/emulator/libs/timezone`
**Namespace:** `goldfish::timezone`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `GetHostTimezone()` | `@goldfish//emulator/libs/timezone` | `include/goldfish/timezone/host_timezone.h` | Returns the host timezone, or `std::nullopt` if it cannot be determined. |
| `GetGuestTimezone()` | `@goldfish//emulator/libs/timezone` | `include/goldfish/timezone/host_timezone.h` | Returns the requested timezone (e.g. the `-timezone` value) if it is a zoneinfo name, otherwise the host timezone. |
| `IsZoneinfoName()` | `@goldfish//emulator/libs/timezone` | `include/goldfish/timezone/host_timezone.h` | Checks the `Area/Location[/SubLocation]` form. |

Tested in `host_timezone_test.cc` (`@goldfish//emulator/libs/timezone:host_timezone_test`).

## Critical Infrastructure
* **Linux & macOS:** `TZ` if it holds a zoneinfo name, then the zoneinfo file that `/etc/localtime` links to, then the file under `TZDIR` (default `/usr/share/zoneinfo`) with the same content as `/etc/localtime`. The `posix` and `right` copies of the database are skipped.
* **Windows:** the registry key name from `GetDynamicTimeZoneInformation()` (e.g. `Tokyo Standard Time`), which is never localized, mapped with a table generated from CLDR `windowsZones.xml`. The localized `strftime("%Z")` name is only a fallback.
* **Testing:** the parsing and mapping helpers in `goldfish::timezone::internal` are compiled on every host, so the Windows table is unit-tested on Linux too.

## Threading Model
* **Thread Safe:** Functions have no shared state. `GetHostTimezone()` reads the environment and the file system on every call, so call it once and keep the result.
