# Fixtures

Sample platform responses used by the unit tests. They are **constructed**, not
recorded: each one follows the response models the Unity SDK 2.0 parses
(`ApexTypes.cs`, `PixoPlatformAPIHandler.cs`), with made-up values. Replace
them with responses recorded from staging once the core runs against it.

`session_join_response.json` is the shape this core expects from the new
session event route. `session_join_legacy_response.json` is the shape `/event`
returns today; the core accepts both.

`module_catalog_response.json` is a small module list for filtering, sorting
and paging: one module per lifecycle case, including Archived, a custom
lifecycle name ("Beta") and a version with no lifecycle.

`xapi_join_statement.json`, `xapi_event_statement.json` and
`xapi_complete_statement.json` are **recorded**: the statements TinCan.NET
(the `TinCan.dll` the Unity SDK 1.x shipped) serializes for a join, an event
and a completion built the way 1.x built them, with a fixed statement ID and
time. The core's xAPI session format must produce the same statements.
