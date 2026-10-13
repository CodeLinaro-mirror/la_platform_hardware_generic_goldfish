# Component: Crash Report Advisor (`advisor`)

**Role:** Automated Crash Triage, Minidump Analysis, and Buganizer Deduplication Tool
**Location:** `emulator/crashreport/tool/advisor/`
**Build Targets:** `@goldfish//emulator/crashreport/tool/advisor:advisor`, `@goldfish//emulator/crashreport/tool/advisor:advisor_test`, `@goldfish//emulator/crashreport/tool/advisor:buganizer_test`, `@goldfish//emulator/crashreport/tool/advisor:buganizer_e2e_test`

## Overview
Python CLI pipeline (`@goldfish//emulator/crashreport/tool/advisor:advisor`) that fetches production emulator crash reports from `go/crash`, downloads symbols from Android Build (`go/ab`), symbolizes minidumps via `@goldfish//emulator/crashreport/tool:crashreport`, generates root-cause analyses, and manages Buganizer issues and hotlists in Component `29601`.

## Key Modules, Classes & Functions
- **`advisor.py`** (`CrashAdvisorApp`, `main`):
  - End-to-end orchestrator (`run`, `_process_closed_loop`, `_execute_agent_handoff`).
- **`buganizer.py`** (`BuganizerClient`, `BuganizerError`, `BuganizerAuthError`):
  - Issue search, creation, regression reopening, and comment deduplication (`search_issue_by_signature`, `create_issue`, `update_issue_comment`, `reopen_issue_as_regression`, `has_new_crash_information`, `format_new_crash_info_comment`, `derive_crash_title`, `clean_version_string`).
  - Hotlist organization (`ensure_crash_hotlists`, `get_or_create_hotlist`, `add_issue_to_hotlist`) for `GENERAL_CRASHES_HOTLIST_NAME` (`"qemu-next-crashes"`) and milestone `VERSION_CRASHES_HOTLIST_PREFIX` (`"qemu-next-crashes-version-<version>"`).
- **`client.py`** (`GossoClient`):
  - Fetches crash report JSON metadata and minidump files via `gosso` (`fetch_url_to_file`) with automatic fallback to `stubby` RPCs against `blade:crash-queryapi` (`fetch_report_via_stubby`, `fetch_file_via_stubby`).
- **`metadata.py`** (`CrashMetadata`):
  - Parses `report_proto` JSON (`report_id`, `product_name`, `version`, `build_id`, `os_summary`, `build_target`, `custom_keys` breadcrumbs, `primary_signature`, `stable_signature`, `modules`).
- **Supporting Modules & Tests**:
  - `symbols.py` (`SymbolFetcher`), `dump.py` (`CrashReportDumper`), `context.py`, `rca.py` (`CrashReportAnalyzer`), `api.py`.
  - Unit tests: `advisor_test.py` (`:advisor_test`) and `buganizer_test.py` (`:buganizer_test`).
