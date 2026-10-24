/**
 * AISurvivorsState.c
 *
 * Plain data saved to $profile:AISurvivors\AISurvivorsState.json so tribes and their bases survive restarts.
 * The base structures themselves (fences, flag, crates...) are persisted by the game like any player base;
 * this file remembers which objects belong to which tribe so they can be re-adopted after a restart.
 */

class AISurvivorsStructureData
{
	string Type;
	vector Position;
	vector Orientation;
	int PlanIndex;
}

class AISurvivorsTribeData
{
	int ID;
	string Name;
	int Temperament;
	vector BaseCenter;
	float BaseYaw;
	string AnchorName;
	int MemberTarget;
	int GearTier;
	int LootRuns;
	int Materials;
	int Metal;
	int PlanIndex;
	bool Wiped;
	//! Minutes left until a wiped tribe returns (counted down while the server runs).
	float RespawnMinutesLeft;
	ref array<ref AISurvivorsStructureData> Structures = {};
}

class AISurvivorsStateData
{
	int Version = 1;
	string WorldName;
	int NextTribeID = 1;
	ref array<ref AISurvivorsTribeData> Tribes = {};

	static AISurvivorsStateData Load()
	{
		AISurvivorsStateData data = new AISurvivorsStateData();
		if (!FileExist(AISV_STATE_FILE))
			return data;

		string error;
		if (!JsonFileLoader<AISurvivorsStateData>.LoadFile(AISV_STATE_FILE, data, error))
		{
			AISurvivorsLog.Warn("Could not read saved state, starting fresh: " + error);
			return new AISurvivorsStateData();
		}

		if (!data.Tribes)
			data.Tribes = {};

		return data;
	}

	void Save()
	{
		if (!FileExist(AISV_PROFILE_FOLDER))
			MakeDirectory(AISV_PROFILE_FOLDER);

		string error;
		if (!JsonFileLoader<AISurvivorsStateData>.SaveFile(AISV_STATE_FILE, this, error))
			AISurvivorsLog.Warn("Could not save state: " + error);
	}
}
