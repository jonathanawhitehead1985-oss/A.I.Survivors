# A.I. Survivors

Groups of 3–5 AI survivors live on the map like a real player group would: they settle near a village, build a real base, go out to loot towns and military, come home to stash their gear, hunt, drive cars, hunt players, and defend their base to the death.

Built on **DayZ Expansion AI**: Expansion handles movement, shooting, looting buildings, bandaging, eating, skinning animals and getting into cars. This mod adds the group "brain", the base building and the base defence on top. Default locations are set up for **Nasdara** (Badlands).

> **v1.0.2 (Oct 24 2026): tested in-game.** Run on a local DayZDiag server and client on **Nasdara**, with CF, Dabs Framework, DayZ-Expansion-Core and DayZ-Expansion-AI **1.9.80** (DayZ 1.30). Confirmed working:
>
> - Compiles on server and client with no script errors. The survivor faction registers and syncs to players.
> - All 20 base villages and 49 of the 52 loot places resolve by name. The other 3 checkpoints/camps fall back to their grid reference.
> - All 6 tribes spawn (3–5 survivors each, Expansion loadouts, some with a car) and build their base: campfire, crate, tent, territory flag (support + pole), then wooden walls part by part.
> - Tribes head out on loot runs and go idle when no player is near.
> - After a server restart every tribe gets its existing base back and carries on building.
>
> Not yet seen in a test: driving, raiding a player and base defence against a player (no player stayed near them long enough). Watch those first on your server.
>
> **Fixed in 1.0.2** (found by running it; the earlier versions did not compile):
>
> 1. **Compile error:** a settings field named `Building` clashed with DayZ's `Building` class. It is now `BuildSettings` (same place in the JSON file).
> 2. **Compile error:** the tribe's `GetDebugName()` clashed with a method every DayZ object already has. Renamed to `GetTribeLabel()`.
> 3. **Crash** (`NULL pointer` in `SetupGuardPatrol`) whenever a tribe with home guards spawned: a list returned from a function was freed before the loop over it ran. It is now kept in a variable.
> 4. **Bases stalled after the first wall:** builders that got pulled away (zombies, other tribes, looting) never walked back. The move order is now repeated, and after a minute they build from wherever they are at the base.
> 5. Base pieces that vanished during a restart are rebuilt (since 1.0.1).
>
> The PBO in `@AISurvivors/addons` is built from this source but **not signed**. Run `Sign_AISurvivors.bat` before putting it on a server with `verifySignatures = 2`.

---

## What the survivors do

| Player behaviour | How A.I. Survivors does it |
|---|---|
| Spawn in a group | 3–5 survivors (`MinMembers` / `MaxMembers`) spawn together at a new base site, out of sight of real players. |
| Build a base | Real vanilla objects, built **one part at a time** by a survivor standing at the spot: campfire, crate, tent → **territory flag** (raised, so the base doesn't despawn) → ring of **fences** with an entrance gap → **watchtower** → sea chest, barrel and crates. Walls start as wood; once the group has collected enough metal (from military loot runs) new walls are metal. Players can raid, dismantle and loot everything. If a structure gets destroyed, the group rebuilds it. |
| Loot | The group picks a town, city, industrial site or military base within `LootRadius` (closer places and, for well-geared groups, military are preferred). It walks or drives there and uses Expansion's roaming/looting to go building to building, picking up weapons, clothing upgrades, food and meds. |
| Stash loot | When they get home, each survivor drops spare items (extra guns, tools, clothes) into the base stash. Ammo, food, bandages and knives stay on them. |
| Progress | Loot runs earn *materials* (used to build) and *metal*. Completed runs raise the group's **gear tier** (Fresh → Scavenger → Armed → Geared → Veteran), so new recruits spawn better equipped. |
| Hunt | The group heads into the wilderness and goes after any game animal it sees, then skins it. Expansion handles the shooting and skinning; most survivors carry a hunting knife. |
| Drive | *Experimental.* For long trips the leader takes the base car (or any empty, drivable, unlocked car nearby) and the rest of the group gets in. A simple autopilot follows a navmesh route that prefers roads. If the car gets stuck, they get out and walk. |
| PvP | Every group has a temperament (see below). **Bandit** and **Hostile** groups go out hunting the nearest player. Every group holds a grudge against anyone who shoots them, and goes after a player who kills one of them. Different groups shoot each other on sight (`TribesHostileToEachOther`). |
| Defend the base | Any player, other AI or **zombie** inside `BaseRadius` (75 m) sets off the alarm. Every member of the group gets the intruder as a maximum-threat target, the home guard engages, and the field squad runs home if it is within `BaseRecallDistance` (1.5 km). Intruders also get a grudge, so a player who pokes the base and leaves stays a target. |
| Grow back | Dead members are replaced over time: a newcomer walks in from 250–450 m out and joins up. A group that is wiped out comes back to its base after `TribeRespawnMinutes`, unless players destroyed the flag. In that case a new group settles somewhere else and the old base is cleaned up later. |

**Temperaments** (weights in `Temperaments`):

- **Friendly**: never shoots first outside its base.
- **Neutral**: opens fire if you aim at them or attack them (Expansion's guard logic).
- **Hostile**: shoots on sight.
- **Bandit**: shoots on sight and actively hunts players.

All four are always extremely hostile inside their base radius.

Two groups per tribe: when a tribe has 4 or more members, `BaseGuards` (default 1) stay home and patrol the perimeter while the rest go out. Both groups belong to the same tribe, so they never fight each other.

---

## Install

1. Build the PBO, or use the pre-packed `@AISurvivors/addons/AISurvivors.pbo`. To build it yourself, open DayZ Tools → Addon Builder, set the source to the `AISurvivors` folder and the prefix to `AISurvivors`. Binarizing isn't needed.
2. **Sign it** with your server key (DayZ Tools → DS Utils) and put the `.bikey` in your server's `keys` folder. Clients need the mod too, because the survivor faction is synced to clients.
3. Load order (server **and** client):

   ```
   -mod=@CF;@Dabs Framework;@DayZ-Expansion-Core;@DayZ-Expansion-AI;@AISurvivors
   ```

   **`@Dabs Framework` is required.** Expansion Core depends on it (`DF_Scripts`), and without it the server stops on a missing-addon error before this mod ever loads. Earlier versions of this README and the test launcher left it out. `@DayZ-Expansion` (the main mod) is optional for A.I. Survivors; add it if your server uses it.
4. Start the server once. The mod writes `profiles\AISurvivors\AISurvivorsSettings.json`. Edit it, then restart.

---

## Nasdara locations

Every place in the settings is looked up **by name** in the map's own location config first. That is the same list the in-game map uses, so positions are exact. If a name isn't found, the mod falls back to the **grid reference** taken from nasdaramap.com. Example: `"104 057"` means 10.4 km east and 5.7 km from the top edge. Set `GridOriginTopLeft` to false if your grids count from the bottom.

- `BaseAnchors`: 20 villages that groups settle near. The base is placed 250–800 m away, on flat open ground, at least 900 m from other bases and 350 m from player flags.
- `LootLocations`: Dashtabad, Nasdara, 15 towns and industrial sites, 20 villages and 15 military sites (Dashtabad Airbase, the COPs, FOB Atlas, Baza Zenit, NII-Mirnyy, the 754 sites, the checkpoints, Zulfiqar…).

On the first boot the server log lists any name it couldn't match, and warns if a name and its grid disagree by more than 700 m. If many places show that warning, flip `GridOriginTopLeft`. Turn on `DebugLogging` to see every resolved position. The mod also works on other maps: replace the two lists, or empty them to use every village and town on the map.

---

## Key settings

| Setting | Default | Meaning |
|---|---|---|
| `MaxTribes` | 6 | Groups on the map at once |
| `MinMembers` / `MaxMembers` | 3 / 5 | Group size |
| `BaseRadius` | 75 | Hostility radius around a base |
| `BaseRecallDistance` | 1500 | Field squad comes home from this far when the base is attacked |
| `AlarmDurationSeconds` | 180 | How long the alert lasts after the last intruder |
| `LootRadius` | 3000 | How far a group goes to loot |
| `RaidSearchRadius` | 1500 | How far Bandit/Hostile groups look for players to hunt |
| `GrudgeMinutes` | 30 | How long a group stays hostile to a player who shot it (doubled for a kill) |
| `TribeRespawnMinutes` | 45 | Wiped group comes back after this long |
| `RecruitIntervalMinutes` | 15 | Time between replacement recruits |
| `Activities` | Loot 50, Hunt 20, Raid 15, Rest 15 | How a group spends its time |
| `GearTiers` | 5 tiers | Expansion loadouts per tier, and loot runs needed to reach it |
| `AI` | accuracy 0.25–0.6 | Accuracy, damage, threat and noise ranges, knife chance |
| `BuildSettings` | 10 walls, radius 12 m | Layout, build speed, materials per action, flags, tents |
| `Vehicles.Enabled` | true | Turns driving on or off. Also: car list, cruise speed, stuck detection |
| `VirtualizeWhenNoPlayersNearby` | true | Despawns a resting group's AI when no player is within 1.8 km, and brings it back at 1.4 km. Saves server performance; the group keeps progressing while virtual. |

Saved state (bases, structures, progress) is in `profiles\AISurvivors\AISurvivorsState.json`. The base objects are saved by the game like any player base. After a restart the mod finds them again by type and position.

---

## Testing

**Fastest way: double-click `Test_AISurvivors_Diag.bat`.** It starts a DayZDiag server on Nasdara plus a DayZDiag client that joins it. It loads CF, Dabs Framework, Expansion Core and Expansion AI straight from your Steam Workshop folder, plus `@AISurvivors` from this folder. Nothing needs signing or copying. Everything it creates lives in `_TestServer` (delete that folder to reset). `Test_ServerOnly.bat` does the same without the game client, which is enough to watch tribes spawn and build in the log. Both stop any earlier test session first.

After the first boot, open `_TestServer\Profiles\AISurvivors\AISurvivorsSettings.json`, set `"DebugLogging": 1` and `"FirstSpawnDelaySeconds": 20`, then restart. The server log is the newest `_TestServer\Profiles\script_*.log`. Search it for `[AISurvivors]` and `SCRIPT    (E)`.

For your real (retail) server: run `Sign_AISurvivors.bat` once after every rebuild. It creates your key, signs the PBO and copies the `.bikey`. Then `Start_RetailTestServer.bat` starts the Steam DayZServer with the right mods and copies every mod's key into `keys` (your servers use `verifySignatures = 2`).

Things to check first:

1. **It compiles.** Any script error names the file and line.
2. **Locations.** `World 'xxx', size ... m`, then `N base anchors, M loot locations ready`.
3. **First group.** After `FirstSpawnDelaySeconds` (90 s) you should see `New tribe [Name #1 TEMPERAMENT] settled near <village>`. Teleport there and watch them build the camp and the flag. With debug on, each part is logged.
4. **Base defence.** Walk inside 75 m: `base alarm!` is logged and they engage.
5. **Driving.** This is the most experimental part. If cars behave badly on Nasdara's roads, set `Vehicles.Enabled` to false. Everything else works on foot.

## Known limitations

- **Building is simulated.** A survivor stands at the spot and the part appears. They don't carry or attach real planks and nails. Progress is limited by abstract *materials* earned on loot runs.
- **Gear resets.** Survivors brought back after being virtualized, or after a group is wiped, spawn with their gear tier's loadout, not the exact items they had looted.
- **Car stealing.** Survivors only take empty cars that are drivable and not locked by Expansion's key system. They never pull a player out of a car.
- **Expansion version.** Written against the Expansion AI source as of October 2026 (1.9.78). A big Expansion AI update could rename something this mod uses. The script log will point at the line.
