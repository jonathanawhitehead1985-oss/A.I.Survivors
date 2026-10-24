/**
 * AISurvivorsSettings.c
 *
 * All tunables for A.I. Survivors. Written to $profile:AISurvivors\AISurvivorsSettings.json on first start.
 * Defaults are tuned for the Nasdara (Badlands) map, but every location is looked up by NAME in the
 * map's own CfgWorlds config first, so the mod also works on other maps if you replace the lists.
 */

static const string AISV_PROFILE_FOLDER = "$profile:AISurvivors\\";
static const string AISV_SETTINGS_FILE = AISV_PROFILE_FOLDER + "AISurvivorsSettings.json";
static const string AISV_STATE_FILE = AISV_PROFILE_FOLDER + "AISurvivorsState.json";

class AISurvivorsLog
{
	static bool s_Debug;

	static void Info(string msg)
	{
		Print("[AISurvivors] " + msg);
	}

	static void Debug(string msg)
	{
		if (s_Debug)
			Print("[AISurvivors][debug] " + msg);
	}

	static void Warn(string msg)
	{
		Print("[AISurvivors][WARNING] " + msg);
	}
}

//! A named place on the map. Name is matched against the map's CfgWorlds names (case/space/dash-insensitive).
//! Grid is the 6-digit map grid ("104 057" = 10.4 km east, 5.7 km from the top/north edge) and is only used
//! when the name can't be found in the map config.
class AISurvivorsNamedPoint
{
	string Name;
	string Grid;
	string Type;
	float Radius;
	float Weight;

	void AISurvivorsNamedPoint(string name = "", string grid = "", string type = "Town", float radius = 250.0, float weight = 1.0)
	{
		Name = name;
		Grid = grid;
		Type = type;
		Radius = radius;
		Weight = weight;
	}
}

class AISurvivorsTemperamentWeights
{
	//! FRIENDLY: never shoots first outside its base. Fights back if attacked.
	float Friendly = 15.0;
	//! NEUTRAL: like an armed stranger - opens fire if you raise a weapon at them or attack them.
	float Neutral = 35.0;
	//! HOSTILE: kill on sight outside the base too.
	float Hostile = 30.0;
	//! BANDIT: kill on sight and goes out hunting players (PvP raids).
	float Bandit = 20.0;
}

class AISurvivorsActivityWeights
{
	float Loot = 50.0;
	float Hunt = 20.0;
	float Raid = 15.0;  //! only used by HOSTILE and BANDIT tribes (BANDIT doubles it)
	float Rest = 15.0;
}

class AISurvivorsGearTier
{
	string Name;
	//! Expansion loadout file names (from $profile:ExpansionMod\Loadouts\). One is picked at random per survivor.
	ref TStringArray Loadouts;
	//! Number of completed loot runs the tribe needs before new recruits spawn with this tier.
	int LootRunsRequired;

	void AISurvivorsGearTier(string name = "", TStringArray loadouts = null, int lootRunsRequired = 0)
	{
		Name = name;
		if (loadouts)
			Loadouts = loadouts;
		else
			Loadouts = {};
		LootRunsRequired = lootRunsRequired;
	}
}

class AISurvivorsAIStats
{
	float AccuracyMin = 0.25;
	float AccuracyMax = 0.60;
	float DamageMultiplier = 1.0;
	float DamageReceivedMultiplier = 1.0;
	float ThreatDistanceLimit = 700.0;
	float NoiseInvestigationDistanceLimit = 500.0;
	float HeadshotResistance = 0.0;
	//! 0 = no unlimited reload. See Expansion AI docs for the bitmask.
	int UnlimitedReload = 0;
	bool CanBeLooted = true;
	//! Chance a survivor carries a knife (needed to skin animals after a hunt).
	float KnifeChance = 0.85;
}

class AISurvivorsBuildSettings
{
	//! Seconds between two construction actions while the tribe is building.
	float BuildStepSeconds = 45.0;
	//! Max construction actions per building session before the tribe goes off to do something else.
	int MaxStepsPerSession = 8;
	//! Wall ring
	int WallSegments = 10;
	float WallRingRadius = 12.0;
	//! Metal walls are used once the tribe has gathered this much metal (from military loot runs).
	int MetalForMetalWalls = 6;
	bool BuildWatchtower = true;
	//! Watchtower parts containing any of these strings are skipped ("level_3" keeps it at two floors).
	ref TStringArray WatchtowerExcludeParts = {"level_3"};
	ref TStringArray FlagClasses = {"Flag_Pirates", "Flag_Wolf", "Flag_Bear", "Flag_Rex", "Flag_Snake", "Flag_Crook", "Flag_Refuge", "Flag_HunterZ"};
	ref TStringArray TentClasses = {"MediumTent", "MediumTent_Green", "CarTent"};
	//! Material cost (abstract "materials" gathered on loot runs) per construction ACTION
	//! (placing an object or building one of its parts, e.g. a wall = place + base + lower + upper = 4 actions).
	int CostTent = 1;
	int CostFlag = 1;
	int CostWallPart = 1;
	int CostWatchtowerPart = 1;
	int CostStorage = 1;
	//! Starting materials for a brand new tribe (enough for the camp and the flag).
	int StartingMaterials = 6;
}

class AISurvivorsVehicleSettings
{
	//! Experimental: survivors drive cars on long trips using a simple autopilot. Turn off if it misbehaves on your map.
	bool Enabled = true;
	//! Only use a car when the destination is at least this far away (m).
	float MinTripDistance = 1200.0;
	//! Chance a new tribe starts with a car parked at its base.
	float StartWithVehicleChance = 0.35;
	ref TStringArray VehicleClasses = {"OffroadHatchback", "CivilianSedan", "Hatchback_02", "Sedan_02", "Offroad_02", "Truck_01_Covered"};
	float CruiseSpeedKmh = 50.0;
	//! If the car moves less than this in StuckSeconds, survivors bail out and continue on foot.
	float StuckDistance = 12.0;
	float StuckSeconds = 25.0;
	//! Survivors will also commandeer an empty, drivable car they find within this distance of the group leader.
	float CommandeerRadius = 120.0;
}

class AISurvivorsSettings
{
	int Version = 1;
	bool Enabled = true;
	bool DebugLogging = false;

	//! ---- Population ----
	int MaxTribes = 6;
	int MinMembers = 3;
	int MaxMembers = 5;
	//! Members that stay home to guard while the rest go out (only when the tribe has 4+ members).
	int BaseGuards = 1;
	float FirstSpawnDelaySeconds = 90.0;
	//! Minutes before a wiped-out tribe slot comes back.
	float TribeRespawnMinutes = 45.0;
	//! Minutes between recruiting a new member when the tribe is below its target size.
	float RecruitIntervalMinutes = 15.0;
	//! No new tribe/base or recruit will appear within this distance of a real player.
	float MinSpawnDistanceFromPlayers = 500.0;

	//! ---- Base ----
	//! Anyone (player, other AI or zombie) inside this radius makes the tribe extremely hostile.
	float BaseRadius = 75.0;
	//! Members out in the field within this distance come running home when the base is attacked.
	float BaseRecallDistance = 1500.0;
	//! Seconds the base stays on alert after the last intruder was seen.
	float AlarmDurationSeconds = 180.0;
	float MinDistanceBetweenBases = 900.0;
	//! Keep this far from player territory flags when picking a base site.
	float MinDistanceFromPlayerTerritory = 350.0;
	float BaseDistanceFromAnchorMin = 250.0;
	float BaseDistanceFromAnchorMax = 800.0;
	//! Max terrain slope for a base site (1 = flat). 0.96 ~ 16 degrees.
	float MinSiteFlatness = 0.96;
	//! An abandoned (wiped and not respawned) base is deleted after this many minutes. 0 = never.
	float AbandonedBaseCleanupMinutes = 240.0;

	//! ---- Behaviour ----
	ref AISurvivorsTemperamentWeights Temperaments = new AISurvivorsTemperamentWeights();
	ref AISurvivorsActivityWeights Activities = new AISurvivorsActivityWeights();
	float LootRunMinutesMin = 12.0;
	float LootRunMinutesMax = 25.0;
	float HuntMinutesMin = 8.0;
	float HuntMinutesMax = 18.0;
	float RaidMinutesMin = 8.0;
	float RaidMinutesMax = 15.0;
	float RestMinutesMin = 2.0;
	float RestMinutesMax = 6.0;
	//! How far from their base a tribe will go to loot.
	float LootRadius = 3000.0;
	//! How far from their squad a raiding tribe looks for players to hunt.
	float RaidSearchRadius = 1500.0;
	//! Different tribes shoot each other on sight (like rival player groups).
	bool TribesHostileToEachOther = true;
	//! Minutes a tribe holds a grudge against a player who shot one of them.
	float GrudgeMinutes = 30.0;
	//! Number of items each survivor drops into the base stash when coming home from a loot run.
	int StashItemsPerMember = 3;

	//! ---- Performance ----
	//! Despawn the AI bodies of a tribe that is resting at home with no real player nearby, and respawn them when
	//! someone comes close. The tribe keeps progressing (materials, gear tier) while virtual.
	bool VirtualizeWhenNoPlayersNearby = true;
	float VirtualizeDistance = 1800.0;
	float DevirtualizeDistance = 1400.0;

	ref AISurvivorsAIStats AI = new AISurvivorsAIStats();
	ref AISurvivorsBuildSettings BuildSettings = new AISurvivorsBuildSettings();
	ref AISurvivorsVehicleSettings Vehicles = new AISurvivorsVehicleSettings();

	ref array<ref AISurvivorsGearTier> GearTiers = {};

	//! ---- Map ----
	//! Map grid: true if grid numbers count from the top (north) edge, like iZurvive and nasdaramap.com.
	bool GridOriginTopLeft = true;
	//! Base sites are picked near these places.
	ref array<ref AISurvivorsNamedPoint> BaseAnchors = {};
	//! Places tribes go to loot. Military = better gear & metal, Town/City = more materials.
	ref array<ref AISurvivorsNamedPoint> LootLocations = {};
	//! If fewer than 3 base anchors can be resolved, every village/town on the map is used instead.
	bool FallbackToAllSettlements = true;

	ref TStringArray TribeNames = {};

	void Defaults()
	{
		GearTiers.Clear();
		GearTiers.Insert(new AISurvivorsGearTier("Fresh", {"PlayerSurvivorLoadout"}, 0));
		GearTiers.Insert(new AISurvivorsGearTier("Scavenger", {"SurvivorLoadout", "PlayerSurvivorLoadout"}, 2));
		GearTiers.Insert(new AISurvivorsGearTier("Armed", {"SurvivorLoadout", "BanditLoadout", "PoliceLoadout"}, 5));
		GearTiers.Insert(new AISurvivorsGearTier("Geared", {"BanditLoadout", "HumanLoadout", "TTSKOLoadout"}, 9));
		GearTiers.Insert(new AISurvivorsGearTier("Veteran", {"WestLoadout", "EastLoadout", "GorkaLoadout"}, 14));

		TribeNames = {"Dust Wolves", "Sand Rats", "Red Wadi Crew", "The Nomads", "Black Kavir", "Lut Scavengers", "Oasis Watch", "Rust Lords", "Dry Creek Boys", "Sun Eaters", "Scorpion Clan", "Last Light"};

		NasdaraDefaults();
	}

	//! Coordinates are nasdaramap.com grid references (100 m per unit, counted from the north-west corner).
	void NasdaraDefaults()
	{
		BaseAnchors.Clear();
		//! Villages and small towns - real players settle near these, away from the big cities.
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Karizak", "035 035", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Chashme Sabz", "066 036", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Sabz Pashan", "045 046", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Tor Dara", "041 070", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Sar-e Deh", "030 101", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Bala Kalay", "044 087", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Tut Dara", "054 083", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Gul Zar", "050 098", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Pay-e Kuh", "063 106", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Mian Deh", "073 122", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Tut Mian", "079 096", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Payan Kalay", "093 088", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Mian Dara", "088 038", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Dara-e Nur", "109 024", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Marz-e Lut", "137 036", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Varzaneh", "133 042", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Kariz-e Marz", "107 078", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Dahaneh-e Kavir", "112 138", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Bagh-e Dara", "094 131", "Village"));
		BaseAnchors.Insert(new AISurvivorsNamedPoint("Panj Asyab", "058 028", "Village"));

		LootLocations.Clear();
		//! City / towns
		LootLocations.Insert(new AISurvivorsNamedPoint("Dashtabad", "104 057", "City", 500, 1.5));
		LootLocations.Insert(new AISurvivorsNamedPoint("Nasdara", "101 121", "City", 600, 1.5));
		LootLocations.Insert(new AISurvivorsNamedPoint("Kamar Zir", "094 044", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Shah Yailaq", "101 081", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Bam-e Sabz", "087 078", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Pesh Kuh", "037 054", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Gandomabad", "078 039", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Khorramabad", "046 055", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Sabzabad", "068 089", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("No Abad", "069 111", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Gard Abad", "110 087", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Kucha Dar", "045 062", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Bustanshahr", "073 107", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Dar-e Ardis", "039 111", "Town", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Terminal Nas", "109 125", "Industrial", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Sarman-Naft refinery", "107 104", "Industrial", 300));
		LootLocations.Insert(new AISurvivorsNamedPoint("Nasdar-Beton factory", "097 098", "Industrial", 250));
		//! Villages (lower weight)
		LootLocations.Insert(new AISurvivorsNamedPoint("Shinkalay", "083 062", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Dehqankalay", "099 058", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Mal Kalay", "095 075", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Bagh-e Mir", "047 107", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Anar Juy", "055 073", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Kati Golzar", "062 045", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Qala-e Kuhna", "044 041", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Pir Kalay", "076 077", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Bazar-e Kohna", "079 083", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Yak Chinar", "058 066", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Chahar Tut", "080 076", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Pesh Dasht", "113 069", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Manda Marz", "053 057", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Bad Asyab", "085 049", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Suvin Shahr", "057 037", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Gul Dara", "070 119", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Salar Abad", "079 067", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Bar Kalay", "073 032", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Dost Kalay", "066 050", "Village", 200, 0.6));
		LootLocations.Insert(new AISurvivorsNamedPoint("Dahana", "102 045", "Village", 200, 0.6));
		//! Military
		LootLocations.Insert(new AISurvivorsNamedPoint("Dashtabad Airbase", "119 057", "Military", 400, 1.2));
		LootLocations.Insert(new AISurvivorsNamedPoint("COP Neptune", "128 097", "Military", 150));
		LootLocations.Insert(new AISurvivorsNamedPoint("FOB Atlas", "083 106", "Military", 200));
		LootLocations.Insert(new AISurvivorsNamedPoint("COP Razorback", "047 090", "Military", 150));
		LootLocations.Insert(new AISurvivorsNamedPoint("COP Kestrel", "042 034", "Military", 150));
		LootLocations.Insert(new AISurvivorsNamedPoint("COP Condor", "077 045", "Military", 150));
		LootLocations.Insert(new AISurvivorsNamedPoint("Baza Zenit", "063 064", "Military", 250));
		LootLocations.Insert(new AISurvivorsNamedPoint("NII-Mirnyy", "061 071", "Military", 250));
		LootLocations.Insert(new AISurvivorsNamedPoint("Spets-Stvol 754", "066 060", "Military", 200));
		LootLocations.Insert(new AISurvivorsNamedPoint("Proval-754", "071 062", "Military", 200));
		LootLocations.Insert(new AISurvivorsNamedPoint("OKB-754", "065 064", "Military", 250));
		LootLocations.Insert(new AISurvivorsNamedPoint("Dar-e Ardis Military Checkpoint", "044 116", "Military", 120));
		LootLocations.Insert(new AISurvivorsNamedPoint("Salar Abad Military Checkpoint", "072 067", "Military", 120));
		LootLocations.Insert(new AISurvivorsNamedPoint("Pesh Dasht Military Camp", "113 073", "Military", 200));
		LootLocations.Insert(new AISurvivorsNamedPoint("Zulfiqar missile base", "027 072", "Military", 300));
	}

	static AISurvivorsSettings Load()
	{
		AISurvivorsSettings settings = new AISurvivorsSettings();

		if (!FileExist(AISV_PROFILE_FOLDER))
			MakeDirectory(AISV_PROFILE_FOLDER);

		if (FileExist(AISV_SETTINGS_FILE))
		{
			string error;
			if (!JsonFileLoader<AISurvivorsSettings>.LoadFile(AISV_SETTINGS_FILE, settings, error))
			{
				AISurvivorsLog.Warn("Could not read " + AISV_SETTINGS_FILE + " - using defaults (file NOT overwritten). " + error);
				settings = new AISurvivorsSettings();
				settings.Defaults();
				return settings;
			}

			settings.Validate();
			AISurvivorsLog.Info("Loaded settings from " + AISV_SETTINGS_FILE);
		}
		else
		{
			settings.Defaults();
			settings.Save();
			AISurvivorsLog.Info("Created default settings at " + AISV_SETTINGS_FILE);
		}

		return settings;
	}

	void Validate()
	{
		if (!GearTiers)
			GearTiers = new array<ref AISurvivorsGearTier>;
		if (!TribeNames)
			TribeNames = new TStringArray;
		if (!BaseAnchors)
			BaseAnchors = new array<ref AISurvivorsNamedPoint>;
		if (!LootLocations)
			LootLocations = new array<ref AISurvivorsNamedPoint>;

		if (GearTiers.Count() == 0 || TribeNames.Count() == 0 || (BaseAnchors.Count() == 0 && !FallbackToAllSettlements))
		{
			AISurvivorsSettings defaults = new AISurvivorsSettings();
			defaults.Defaults();

			if (GearTiers.Count() == 0)
			{
				foreach (AISurvivorsGearTier tier: defaults.GearTiers)
					GearTiers.Insert(tier);
			}

			if (TribeNames.Count() == 0)
				TribeNames.InsertAll(defaults.TribeNames);

			if (BaseAnchors.Count() == 0 && !FallbackToAllSettlements)
				FallbackToAllSettlements = true;
		}

		if (MinMembers < 1)
			MinMembers = 1;
		if (MaxMembers < MinMembers)
			MaxMembers = MinMembers;
		if (BaseRadius < 20.0)
			BaseRadius = 20.0;
		if (!BuildSettings)
			BuildSettings = new AISurvivorsBuildSettings();
		if (!Vehicles)
			Vehicles = new AISurvivorsVehicleSettings();
		if (!AI)
			AI = new AISurvivorsAIStats();
		if (!Temperaments)
			Temperaments = new AISurvivorsTemperamentWeights();
		if (!Activities)
			Activities = new AISurvivorsActivityWeights();
		if (BuildSettings.WallSegments < 0)
			BuildSettings.WallSegments = 0;
		if (BuildSettings.WallSegments > 24)
			BuildSettings.WallSegments = 24;
	}

	void Save()
	{
		string error;
		if (!JsonFileLoader<AISurvivorsSettings>.SaveFile(AISV_SETTINGS_FILE, this, error))
			AISurvivorsLog.Warn("Could not save settings: " + error);
	}
}
