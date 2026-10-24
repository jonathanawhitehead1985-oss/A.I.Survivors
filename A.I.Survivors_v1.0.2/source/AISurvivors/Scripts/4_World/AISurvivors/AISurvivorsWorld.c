/**
 * AISurvivorsWorld.c
 *
 * Map helpers: resolves named places against the map's own CfgWorlds names (via Expansion's location reader),
 * converts grid references, and finds flat open ground for a base.
 */

class AISurvivorsPlace : Managed
{
	string m_Name;
	string m_Type;
	vector m_Position;
	float m_Radius;
	float m_Weight;

	void AISurvivorsPlace(string name, string type, vector position, float radius, float weight)
	{
		m_Name = name;
		m_Type = type;
		m_Position = position;
		m_Radius = radius;
		m_Weight = weight;
	}

	bool IsMilitary()
	{
		string t = m_Type;
		t.ToLower();
		return t.Contains("military") || t.Contains("airfield") || t.Contains("airbase");
	}
}

class AISurvivorsWorld : Managed
{
	protected ref array<ref ExpansionLocation> m_WorldLocations;
	protected ref array<ref AISurvivorsPlace> m_BaseAnchors = {};
	protected ref array<ref AISurvivorsPlace> m_LootPlaces = {};
	protected float m_WorldSize;

	void Init(AISurvivorsSettings settings)
	{
		m_WorldSize = g_Game.GetWorld().GetWorldSize();
		if (m_WorldSize <= 0)
			m_WorldSize = 15360;

		m_WorldLocations = ExpansionLocation.GetWorldLocations();

		AISurvivorsLog.Info(string.Format("World '%1', size %2 m, %3 named map locations", g_Game.GetWorldName(), m_WorldSize, m_WorldLocations.Count()));

		m_BaseAnchors.Clear();
		m_LootPlaces.Clear();

		foreach (AISurvivorsNamedPoint anchor: settings.BaseAnchors)
		{
			AISurvivorsPlace anchorPlace = Resolve(anchor, settings.GridOriginTopLeft);
			if (anchorPlace)
				m_BaseAnchors.Insert(anchorPlace);
		}

		foreach (AISurvivorsNamedPoint loot: settings.LootLocations)
		{
			AISurvivorsPlace lootPlace = Resolve(loot, settings.GridOriginTopLeft);
			if (lootPlace)
				m_LootPlaces.Insert(lootPlace);
		}

		if (m_BaseAnchors.Count() < 3 && settings.FallbackToAllSettlements)
		{
			AISurvivorsLog.Warn("Fewer than 3 base anchors resolved - using every village/town of this map instead.");
			foreach (ExpansionLocation loc: m_WorldLocations)
			{
				if (ExpansionString.ContainsAny(loc.Type, {"Village", "City", "Capital", "Local"}))
					m_BaseAnchors.Insert(new AISurvivorsPlace(loc.Name, loc.Type, ExpansionStatic.GetSurfacePosition(loc.Position), loc.Radius, 1.0));
			}
		}

		if (m_LootPlaces.Count() < 3)
		{
			AISurvivorsLog.Warn("Fewer than 3 loot locations resolved - using every named map location instead.");
			foreach (ExpansionLocation loc2: m_WorldLocations)
			{
				m_LootPlaces.Insert(new AISurvivorsPlace(loc2.Name, loc2.Type, ExpansionStatic.GetSurfacePosition(loc2.Position), loc2.Radius, 1.0));
			}
		}

		AISurvivorsLog.Info(string.Format("%1 base anchors, %2 loot locations ready", m_BaseAnchors.Count(), m_LootPlaces.Count()));
	}

	static string Normalize(string text)
	{
		string lower = text;
		lower.ToLower();
		string result;
		for (int i = 0; i < lower.Length(); i++)
		{
			string c = lower.Get(i);
			int code = c.ToAscii();
			if ((code >= 48 && code <= 57) || (code >= 97 && code <= 122))
				result += c;
		}
		return result;
	}

	vector GridToWorld(string grid, bool originTopLeft)
	{
		TStringArray parts = {};
		grid.Replace(",", " ");
		grid.Split(" ", parts);

		TStringArray clean = {};
		foreach (string p: parts)
		{
			p.TrimInPlace();
			if (p != "")
				clean.Insert(p);
		}

		if (clean.Count() != 2)
			return vector.Zero;

		float gx = clean[0].ToFloat();
		float gy = clean[1].ToFloat();

		float x = gx * 100.0 + 50.0;
		float z;
		if (originTopLeft)
			z = m_WorldSize - (gy * 100.0 + 50.0);
		else
			z = gy * 100.0 + 50.0;

		return ExpansionStatic.GetSurfacePosition(Vector(x, 0, z));
	}

	AISurvivorsPlace Resolve(AISurvivorsNamedPoint point, bool originTopLeft)
	{
		string wanted = Normalize(point.Name);
		vector gridPos;
		if (point.Grid != "")
			gridPos = GridToWorld(point.Grid, originTopLeft);

		ExpansionLocation best;
		if (wanted != "")
		{
			//! 1st pass: exact name / class name match
			foreach (ExpansionLocation loc: m_WorldLocations)
			{
				if (Normalize(loc.Name) == wanted || Normalize(loc.m_ClassName) == wanted)
				{
					best = loc;
					break;
				}
			}

			//! 2nd pass: class name contains the name (e.g. "Settlement_BameSabz")
			if (!best && wanted.Length() >= 4)
			{
				float bestDist = float.MAX;
				foreach (ExpansionLocation loc3: m_WorldLocations)
				{
					string cls = Normalize(loc3.m_ClassName);
					string nm = Normalize(loc3.Name);
					if (cls.Contains(wanted) || nm.Contains(wanted))
					{
						float d;
						if (gridPos != vector.Zero)
							d = vector.Distance(gridPos, loc3.Position);
						if (!best || d < bestDist)
						{
							best = loc3;
							bestDist = d;
						}
					}
				}
			}
		}

		if (best)
		{
			vector pos = ExpansionStatic.GetSurfacePosition(best.Position);
			if (gridPos != vector.Zero && vector.Distance(gridPos, pos) > 700.0)
				AISurvivorsLog.Warn(string.Format("'%1' found in map config at %2 but its grid %3 points to %4 - using the map config position. (If many places show this, flip GridOriginTopLeft.)", point.Name, pos, point.Grid, gridPos));

			AISurvivorsLog.Debug(string.Format("Resolved '%1' -> %2 (%3)", point.Name, pos, best.m_ClassName));
			return new AISurvivorsPlace(point.Name, point.Type, pos, point.Radius, point.Weight);
		}

		if (gridPos != vector.Zero)
		{
			AISurvivorsLog.Debug(string.Format("'%1' not in map config, using grid %2 -> %3", point.Name, point.Grid, gridPos));
			return new AISurvivorsPlace(point.Name, point.Type, gridPos, point.Radius, point.Weight);
		}

		AISurvivorsLog.Warn("Could not place '" + point.Name + "' (not in map config and no grid given) - skipped.");
		return null;
	}

	array<ref AISurvivorsPlace> GetBaseAnchors()
	{
		return m_BaseAnchors;
	}

	array<ref AISurvivorsPlace> GetLootPlaces()
	{
		return m_LootPlaces;
	}

	float GetWorldSize()
	{
		return m_WorldSize;
	}

	//! ---------------------------------------------------------------------------------------------
	//! Position checks
	//! ---------------------------------------------------------------------------------------------

	static bool IsWater(vector pos)
	{
		return g_Game.SurfaceIsSea(pos[0], pos[2]) || g_Game.SurfaceIsPond(pos[0], pos[2]);
	}

	static bool IsFlat(vector center, float radius, float minNormalY)
	{
		for (int i = 0; i < 9; i++)
		{
			vector sample = center;
			if (i > 0)
			{
				float angle = (i - 1) * 45.0 * Math.DEG2RAD;
				sample[0] = center[0] + Math.Sin(angle) * radius;
				sample[2] = center[2] + Math.Cos(angle) * radius;
			}

			if (IsWater(sample))
				return false;

			vector normal = g_Game.SurfaceGetNormal(sample[0], sample[2]);
			if (normal[1] < minNormalY)
				return false;
		}

		//! Height spread over the footprint must also be small
		float minY = float.MAX;
		float maxY = -float.MAX;
		for (int j = 0; j < 8; j++)
		{
			float a = j * 45.0 * Math.DEG2RAD;
			float y = g_Game.SurfaceY(center[0] + Math.Sin(a) * radius, center[2] + Math.Cos(a) * radius);
			if (y < minY)
				minY = y;
			if (y > maxY)
				maxY = y;
		}

		return (maxY - minY) <= 3.0;
	}

	//! Rejects sites with buildings, roads, rocks or other large objects in the footprint.
	static bool IsClearOfObjects(vector center, float radius)
	{
		if (g_Game.SurfaceRoadY(center[0], center[2]) > g_Game.SurfaceY(center[0], center[2]) + 0.05)
			return false;

		array<Object> objects = {};
		g_Game.GetObjectsAtPosition(center, radius, objects, null);
		foreach (Object obj: objects)
		{
			if (!obj)
				continue;

			if (obj.IsBuilding() || obj.IsRock() || obj.IsTransport())
				return false;

			if (obj.IsInherited(BaseBuildingBase))
				return false;

			//! Plain static map objects (walls, wrecks, poles...) - trees and bushes are fine
			if (obj.IsTree() || obj.IsBush())
				continue;

			if (obj.IsInherited(Man) || obj.IsInherited(DayZCreatureAI) || obj.IsInherited(ItemBase))
				continue;

			if (obj.IsPlainObject() || obj.IsScenery())
				return false;
		}

		return true;
	}

	static float DistanceToNearestPlayer(vector pos)
	{
		array<Man> players = {};
		g_Game.GetPlayers(players);

		float best = float.MAX;
		foreach (Man man: players)
		{
			if (!man || !man.GetIdentity() || !man.IsAlive())
				continue;

			float d = vector.Distance(man.GetPosition(), pos);
			if (d < best)
				best = d;
		}

		return best;
	}

	static bool IsNearPlayerTerritory(vector pos, float radius)
	{
		array<Object> objects = {};
		g_Game.GetObjectsAtPosition(pos, radius, objects, null);
		foreach (Object obj: objects)
		{
			TerritoryFlag flag;
			if (Class.CastTo(flag, obj) && !AISurvivorsManager.IsTribeObject(flag))
				return true;
		}

		return false;
	}

	static bool IsInNoGoArea(vector pos)
	{
		auto locationSettings = GetExpansionSettings().GetAILocation();
		if (locationSettings && locationSettings.NoGoAreas && locationSettings.NoGoAreas.IsPointInside(pos))
			return true;

		return false;
	}

	//! Find a base site near an anchor. Returns vector.Zero if none found.
	static vector FindBaseSite(vector anchor, AISurvivorsSettings settings, array<vector> otherBases)
	{
		for (int attempt = 0; attempt < 40; attempt++)
		{
			vector candidate = ExpansionMath.GetRandomPointInRing(anchor, settings.BaseDistanceFromAnchorMin, settings.BaseDistanceFromAnchorMax);
			candidate = ExpansionStatic.GetSurfacePosition(candidate);

			bool tooClose = false;
			foreach (vector other: otherBases)
			{
				if (vector.Distance(other, candidate) < settings.MinDistanceBetweenBases)
				{
					tooClose = true;
					break;
				}
			}

			if (tooClose)
				continue;

			if (IsWater(candidate))
				continue;

			if (!IsFlat(candidate, settings.BuildSettings.WallRingRadius + 4.0, settings.MinSiteFlatness))
				continue;

			if (!IsClearOfObjects(candidate, settings.BuildSettings.WallRingRadius + 6.0))
				continue;

			if (IsInNoGoArea(candidate))
				continue;

			if (IsNearPlayerTerritory(candidate, settings.MinDistanceFromPlayerTerritory))
				continue;

			if (DistanceToNearestPlayer(candidate) < settings.MinSpawnDistanceFromPlayers)
				continue;

			return candidate;
		}

		return vector.Zero;
	}

	//! A spot in the wild for hunting: away from the base, not water, preferably flat-ish.
	static vector FindHuntingGround(vector base, float minDist, float maxDist)
	{
		for (int attempt = 0; attempt < 20; attempt++)
		{
			vector candidate = ExpansionStatic.GetSurfacePosition(ExpansionMath.GetRandomPointInRing(base, minDist, maxDist));
			if (IsWater(candidate))
				continue;

			if (IsInNoGoArea(candidate))
				continue;

			return candidate;
		}

		return ExpansionStatic.GetSurfacePosition(ExpansionMath.GetRandomPointInRing(base, minDist, maxDist));
	}
}
