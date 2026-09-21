# PlaceName live trace (Linux / Proton)

## Production signal

Wipe uses **`FeSceneMapName+0x1C`** place-id changes (not TitleInformation).

| Id | Area (observed) |
| --- | --- |
| `100200` | Things Betwixt |
| `100400` | Majula |

Log: `area_title: PlaceName id A→B (MapName+0x1C)` then `event: area title card` + wipe.

## Optional TRACE

```ini
[sitcom]
log=true
trace_area_title=true
```

Restart the game. Extra `area_trace:` lines dump MapName fields. Turn off after diagnosing.

## What failed (kept for history)

- Map / load-state area codes — Betwixt & Majula share ids
- `FeSubStateTitleInformation` activate — first card only; silent on Majula walk
- FMOD zone SE codes — not the PlaceName UI
