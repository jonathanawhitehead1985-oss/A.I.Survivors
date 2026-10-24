// A.I. Survivors - player-like AI tribes for DayZ, built on DayZ Expansion AI.
// Load order: @CF, @DayZ-Expansion-Core (pulled in by Expansion), @DayZ-Expansion, @DayZ-Expansion-AI, @AISurvivors
// Must be loaded on BOTH server and client (-mod=), because the custom AI faction is synced to clients by type ID.

class CfgPatches
{
	class AISurvivors_Scripts
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 0.1;
		requiredAddons[] =
		{
			"DZ_Data",
			"DZ_Scripts",
			"DZ_Characters",
			"DayZExpansion_Core_Scripts",
			"DayZExpansion_AI_Scripts"
		};
	};
};

class CfgMods
{
	class AISurvivors
	{
		dir = "AISurvivors";
		name = "A.I. Survivors";
		credits = "Built on DayZ Expansion AI (Enfusion AI Project / DayZ Expansion team)";
		author = "AISurvivors";
		authorID = "";
		version = "1.0.2";
		extra = 0;
		type = "mod";
		hideName = 0;
		hidePicture = 1;
		action = "";
		dependencies[] =
		{
			"Game",
			"World",
			"Mission"
		};
		class defs
		{
			class gameScriptModule
			{
				value = "";
				files[] =
				{
					"AISurvivors/Scripts/3_Game"
				};
			};
			class worldScriptModule
			{
				value = "";
				files[] =
				{
					"AISurvivors/Scripts/4_World"
				};
			};
			class missionScriptModule
			{
				value = "";
				files[] =
				{
					"AISurvivors/Scripts/5_Mission"
				};
			};
		};
	};
};
