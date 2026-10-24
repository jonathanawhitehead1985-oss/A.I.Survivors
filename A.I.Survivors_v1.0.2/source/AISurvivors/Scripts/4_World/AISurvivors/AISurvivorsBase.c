/**
 * AISurvivorsBase.c
 *
 * A tribe's base: a build plan (camp -> flag -> walls -> watchtower -> storage) that survivors construct
 * one part at a time with real vanilla base-building objects, so players can raid, dismantle and loot it.
 */

class AISurvivorsStructurePlan : Managed
{
	int m_Index;
	AISV_StructureKind m_Kind;
	string m_Type;
	vector m_LocalOffset;
	float m_LocalYaw;
	int m_PartCost;

	EntityAI m_Object;
	bool m_Spawned;
	bool m_Complete;
	//! Fence material picked when the fence is placed: "_wood_" or "_metal_" parts are excluded accordingly
	string m_ExcludeMaterial;
	//! Parts the game refused to build (never retried, so a broken part can't stall the base)
	ref TStringArray m_FailedParts = {};

	void AISurvivorsStructurePlan(int index, AISV_StructureKind kind, string type, vector localOffset, float localYaw, int partCost)
	{
		m_Index = index;
		m_Kind = kind;
		m_Type = type;
		m_LocalOffset = localOffset;
		m_LocalYaw = localYaw;
		m_PartCost = partCost;
	}
}

class AISurvivorsBase : Managed
{
	vector m_Center;
	float m_Yaw;
	string m_AnchorName;

	ref array<ref AISurvivorsStructurePlan> m_Plan = {};
	int m_PlanIndex;

	TerritoryFlag m_Flag;
	CarScript m_Vehicle;

	protected AISurvivorsSettings m_Settings;

	void AISurvivorsBase(vector center, float yaw, string anchorName, AISurvivorsSettings settings)
	{
		m_Center = center;
		m_Yaw = yaw;
		m_AnchorName = anchorName;
		m_Settings = settings;

		BuildPlan();
	}

	//! ---------------------------------------------------------------------------------------------
	//! Plan
	//! ---------------------------------------------------------------------------------------------

	protected void AddPlan(AISV_StructureKind kind, string type, vector offset, float yaw, int cost)
	{
		m_Plan.Insert(new AISurvivorsStructurePlan(m_Plan.Count(), kind, type, offset, yaw, cost));
	}

	protected void BuildPlan()
	{
		m_Plan.Clear();

		auto build = m_Settings.BuildSettings;

		//! 1. Camp
		AddPlan(AISV_StructureKind.FIREPLACE, "Fireplace", "0 0 0", 0, 0);
		AddPlan(AISV_StructureKind.CONTAINER, "WoodenCrate", "2.5 0 1.5", 30, 0);
		AddPlan(AISV_StructureKind.TENT, build.TentClasses.GetRandomElement(), "-5 0 3", 90, build.CostTent);

		//! 2. Claim the land
		AddPlan(AISV_StructureKind.FLAG, "TerritoryFlag", "0 0 -6", 0, build.CostFlag);

		//! 3. Wall ring with an opening at the front (segment 0)
		int segments = build.WallSegments;
		for (int i = 1; i < segments; i++)
		{
			float angle = (360.0 / segments) * i;
			float rad = angle * Math.DEG2RAD;
			vector offset = Vector(Math.Sin(rad) * build.WallRingRadius, 0, Math.Cos(rad) * build.WallRingRadius);
			AddPlan(AISV_StructureKind.FENCE, "Fence", offset, angle, build.CostWallPart);
		}

		//! 4. Watchtower inside the ring
		if (build.BuildWatchtower)
			AddPlan(AISV_StructureKind.WATCHTOWER, "Watchtower", Vector(build.WallRingRadius * 0.45, 0, -build.WallRingRadius * 0.45), 45, build.CostWatchtowerPart);

		//! 5. More storage
		AddPlan(AISV_StructureKind.CONTAINER, "SeaChest", "-2.5 0 -2", 200, build.CostStorage);
		AddPlan(AISV_StructureKind.CONTAINER, "Barrel_Green", "-3.5 0 -0.5", 0, build.CostStorage);
		AddPlan(AISV_StructureKind.CONTAINER, "WoodenCrate", "3.5 0 -1.5", 120, build.CostStorage);
	}

	vector GetWorldPosition(AISurvivorsStructurePlan plan)
	{
		float rad = m_Yaw * Math.DEG2RAD;
		float x = plan.m_LocalOffset[0] * Math.Cos(rad) + plan.m_LocalOffset[2] * Math.Sin(rad);
		float z = -plan.m_LocalOffset[0] * Math.Sin(rad) + plan.m_LocalOffset[2] * Math.Cos(rad);
		return ExpansionStatic.GetSurfacePosition(Vector(m_Center[0] + x, 0, m_Center[2] + z));
	}

	float GetWorldYaw(AISurvivorsStructurePlan plan)
	{
		float yaw = m_Yaw + plan.m_LocalYaw;
		while (yaw > 180.0)
			yaw -= 360.0;
		while (yaw < -180.0)
			yaw += 360.0;
		return yaw;
	}

	bool IsComplete()
	{
		return m_PlanIndex >= m_Plan.Count();
	}

	AISurvivorsStructurePlan GetCurrentPlan()
	{
		if (IsComplete())
			return null;

		return m_Plan[m_PlanIndex];
	}

	//! Where the builder should stand for the current plan entry
	vector GetCurrentBuildPosition()
	{
		AISurvivorsStructurePlan plan = GetCurrentPlan();
		if (!plan)
			return m_Center;

		vector pos = GetWorldPosition(plan);
		//! Stand on the inner side of walls
		vector toCenter = vector.Direction(pos, m_Center);
		toCenter[1] = 0;
		if (toCenter.Length() > 3.0)
			pos = ExpansionStatic.GetSurfacePosition(pos + toCenter.Normalized() * 2.5);

		return pos;
	}

	int GetCurrentCost()
	{
		AISurvivorsStructurePlan plan = GetCurrentPlan();
		if (!plan)
			return 0;

		return plan.m_PartCost;
	}

	//! ---------------------------------------------------------------------------------------------
	//! Construction
	//! ---------------------------------------------------------------------------------------------

	//! Performs one construction action. Returns true if something was spawned or built.
	//! builder must be a living survivor (vanilla needs a Man to credit the build to).
	bool BuildStep(notnull eAIBase builder, int metal)
	{
		AISurvivorsStructurePlan plan = GetCurrentPlan();
		if (!plan)
			return false;

		//! Object got destroyed or deleted after it was placed (raided) -> rebuild it
		if (plan.m_Spawned && !plan.m_Object)
		{
			plan.m_Spawned = false;
			plan.m_Complete = false;
		}

		if (!plan.m_Spawned)
		{
			if (!SpawnStructure(plan, metal))
			{
				AISurvivorsLog.Warn("Could not spawn " + plan.m_Type + " - skipping it");
				m_PlanIndex++;
				return false;
			}

			if (plan.m_Complete)
				m_PlanIndex++;

			return true;
		}

		BaseBuildingBase bb;
		if (Class.CastTo(bb, plan.m_Object))
		{
			if (BuildNextPart(bb, builder, plan))
				return true;
		}

		plan.m_Complete = true;
		OnStructureComplete(plan);
		m_PlanIndex++;
		return false;
	}

	protected bool SpawnStructure(AISurvivorsStructurePlan plan, int metal)
	{
		vector pos = GetWorldPosition(plan);
		float yaw = GetWorldYaw(plan);

		int flags = ECE_PLACE_ON_SURFACE | ECE_CREATEPHYSICS | ECE_UPDATEPATHGRAPH;
		EntityAI obj = EntityAI.Cast(g_Game.CreateObjectEx(plan.m_Type, pos, flags));
		if (!obj)
			return false;

		obj.SetOrientation(Vector(yaw, 0, 0));
		obj.Update();

		plan.m_Object = obj;
		plan.m_Spawned = true;

		TentBase tent;

		switch (plan.m_Kind)
		{
			case AISV_StructureKind.FIREPLACE:
				SetupFireplace(obj);
				plan.m_Complete = true;
				break;

			case AISV_StructureKind.TENT:
				if (Class.CastTo(tent, obj))
					tent.Pitch(true, true);
				plan.m_Complete = true;
				break;

			case AISV_StructureKind.CONTAINER:
				plan.m_Complete = true;
				break;

			case AISV_StructureKind.FENCE:
				if (metal >= m_Settings.BuildSettings.MetalForMetalWalls)
					plan.m_ExcludeMaterial = "_wood_";
				else
					plan.m_ExcludeMaterial = "_metal_";
				break;

			case AISV_StructureKind.FLAG:
				Class.CastTo(m_Flag, obj);
				break;
		}

		AISurvivorsManager.RegisterTribeObject(obj);
		AISurvivorsLog.Debug("Placed " + plan.m_Type + " at " + pos);
		return true;
	}

	protected bool IsPartExcluded(AISurvivorsStructurePlan plan, string partName)
	{
		string lower = partName;
		lower.ToLower();

		if (plan.m_Kind == AISV_StructureKind.FENCE)
		{
			if (lower.Contains("gate") || lower.Contains("platform"))
				return true;

			if (plan.m_ExcludeMaterial != "" && lower.Contains(plan.m_ExcludeMaterial))
				return true;
		}
		else if (plan.m_Kind == AISV_StructureKind.WATCHTOWER)
		{
			foreach (string exclude: m_Settings.BuildSettings.WatchtowerExcludeParts)
			{
				string ex = exclude;
				ex.ToLower();
				if (ex != "" && lower.Contains(ex))
					return true;
			}
		}

		return false;
	}

	//! Builds the next part whose requirements are met. Returns false when nothing is left to build.
	protected bool BuildNextPart(BaseBuildingBase bb, eAIBase builder, AISurvivorsStructurePlan plan)
	{
		Construction construction = bb.GetConstruction();
		if (!construction)
			return false;

		//! Base part first
		ConstructionPart basePart = construction.GetBaseConstructionPart();
		if (basePart && !basePart.IsBuilt())
		{
			string baseName = basePart.GetPartName();
			if (plan.m_FailedParts.Find(baseName) > -1)
				return false;

			construction.BuildPartServerEx(builder, baseName, AT_BUILD_PART);
			if (!construction.IsPartConstructed(baseName))
			{
				AISurvivorsLog.Warn("Could not build base part '" + baseName + "' of " + plan.m_Type);
				plan.m_FailedParts.Insert(baseName);
			}
			return true;
		}

		array<ConstructionPart> parts = construction.GetConstructionParts().GetValueArray();
		foreach (ConstructionPart part: parts)
		{
			if (!part || part.IsBuilt())
				continue;

			string partName = part.GetPartName();

			if (IsPartExcluded(plan, partName) || plan.m_FailedParts.Find(partName) > -1)
				continue;

			if (!construction.HasRequiredPart(partName))
				continue;

			if (construction.HasConflictPart(partName))
				continue;

			construction.BuildPartServerEx(builder, partName, AT_BUILD_PART);
			if (!construction.IsPartConstructed(partName))
			{
				AISurvivorsLog.Warn("Could not build part '" + partName + "' of " + plan.m_Type);
				plan.m_FailedParts.Insert(partName);
			}
			else
			{
				AISurvivorsLog.Debug("Built " + partName + " on " + plan.m_Type);
			}
			return true;
		}

		return false;
	}

	protected void OnStructureComplete(AISurvivorsStructurePlan plan)
	{
		if (plan.m_Kind == AISV_StructureKind.FLAG && m_Flag)
			RaiseFlag();
	}

	protected void SetupFireplace(EntityAI obj)
	{
		FireplaceBase fireplace;
		if (!Class.CastTo(fireplace, obj))
			return;

		fireplace.GetInventory().CreateAttachment("Firewood");
		fireplace.GetInventory().CreateAttachment("WoodenStick");
		fireplace.GetInventory().CreateAttachment("Rag");
		fireplace.StartFire(true);
	}

	void RaiseFlag()
	{
		if (!m_Flag)
			return;

		if (!m_Flag.FindAttachmentBySlotName("Material_FPole_Flag"))
		{
			string flagType = m_Settings.BuildSettings.FlagClasses.GetRandomElement();
			if (flagType != "")
				m_Flag.GetInventory().CreateAttachment(flagType);
		}

		//! Phase 0 = flag at the top (vanilla animation is inverted)
		m_Flag.AnimateFlag(0);
		m_Flag.AddRefresherTime01(1.0);
	}

	//! ---------------------------------------------------------------------------------------------
	//! Upkeep
	//! ---------------------------------------------------------------------------------------------

	//! Called periodically: keeps the flag refreshed (which keeps the base from despawning) like players do.
	void Upkeep()
	{
		if (m_Flag && !m_Flag.IsRuined())
		{
			if (m_Flag.FindAttachmentBySlotName("Material_FPole_Flag"))
				m_Flag.AddRefresherTime01(1.0);
		}

		foreach (AISurvivorsStructurePlan plan: m_Plan)
		{
			if (plan.m_Object && plan.m_Object.GetLifetimeMax() > 0)
				plan.m_Object.SetLifetime(plan.m_Object.GetLifetimeMax());
		}
	}

	bool IsFlagStanding()
	{
		return m_Flag && !m_Flag.IsRuined() && m_Flag.FindAttachmentBySlotName("Material_FPole_Flag") != null;
	}

	//! Rewinds the plan to the first structure that was destroyed by players so the tribe rebuilds it.
	void CheckForDamage()
	{
		for (int i = 0; i < m_PlanIndex && i < m_Plan.Count(); i++)
		{
			AISurvivorsStructurePlan plan = m_Plan[i];
			if (plan.m_Spawned && (!plan.m_Object || plan.m_Object.IsRuined()))
			{
				if (plan.m_Object && plan.m_Object.IsRuined())
					g_Game.ObjectDelete(plan.m_Object);

				plan.m_Object = null;
				plan.m_Spawned = false;
				plan.m_Complete = false;
				m_PlanIndex = i;
				AISurvivorsLog.Debug("Base structure " + plan.m_Type + " was destroyed - queued for rebuild");
				return;
			}
		}
	}

	//! First container that still exists (the stash survivors drop loot in)
	EntityAI GetStash()
	{
		foreach (AISurvivorsStructurePlan plan: m_Plan)
		{
			if (plan.m_Kind == AISV_StructureKind.CONTAINER && plan.m_Object && !plan.m_Object.IsRuined())
				return plan.m_Object;
		}

		return null;
	}

	int GetStructureCount()
	{
		int count;
		foreach (AISurvivorsStructurePlan plan: m_Plan)
		{
			if (plan.m_Object)
				count++;
		}
		return count;
	}

	bool IsInside(vector pos, float radius)
	{
		return vector.DistanceSq(Vector(pos[0], 0, pos[2]), Vector(m_Center[0], 0, m_Center[2])) <= radius * radius;
	}

	//! Points around the base used by home guards.
	//! Cached in a member: Enforce Script frees a temporary array returned from a function
	//! before a foreach over it runs, which crashed with "NULL pointer to instance".
	protected ref array<vector> m_PatrolPoints;

	array<vector> GetPatrolPoints()
	{
		if (m_PatrolPoints && m_PatrolPoints.Count() > 0)
			return m_PatrolPoints;

		m_PatrolPoints = new array<vector>;
		float radius = m_Settings.BuildSettings.WallRingRadius + 6.0;
		for (int i = 0; i < 6; i++)
		{
			float rad = (m_Yaw + i * 60.0) * Math.DEG2RAD;
			vector point = ExpansionStatic.GetSurfacePosition(Vector(m_Center[0] + Math.Sin(rad) * radius, 0, m_Center[2] + Math.Cos(rad) * radius));
			m_PatrolPoints.Insert(point);
		}
		return m_PatrolPoints;
	}

	void DeleteAll()
	{
		foreach (AISurvivorsStructurePlan plan: m_Plan)
		{
			if (plan.m_Object)
			{
				AISurvivorsManager.UnregisterTribeObject(plan.m_Object);
				g_Game.ObjectDelete(plan.m_Object);
			}
		}

		if (m_Vehicle && !m_Vehicle.CrewMember(DayZPlayerConstants.VEHICLESEAT_DRIVER))
			g_Game.ObjectDelete(m_Vehicle);

		m_Plan.Clear();
	}

	//! ---------------------------------------------------------------------------------------------
	//! Persistence
	//! ---------------------------------------------------------------------------------------------

	void SaveTo(AISurvivorsTribeData data)
	{
		data.BaseCenter = m_Center;
		data.BaseYaw = m_Yaw;
		data.AnchorName = m_AnchorName;
		data.PlanIndex = m_PlanIndex;
		data.Structures.Clear();

		foreach (AISurvivorsStructurePlan plan: m_Plan)
		{
			if (!plan.m_Object)
				continue;

			AISurvivorsStructureData s = new AISurvivorsStructureData();
			s.Type = plan.m_Object.GetType();
			s.Position = plan.m_Object.GetPosition();
			s.Orientation = plan.m_Object.GetOrientation();
			s.PlanIndex = plan.m_Index;
			data.Structures.Insert(s);
		}
	}

	//! After a restart the game has already re-created the persisted objects. Find them again.
	void AdoptFrom(AISurvivorsTribeData data)
	{
		m_PlanIndex = data.PlanIndex;

		foreach (AISurvivorsStructureData s: data.Structures)
		{
			if (s.PlanIndex < 0 || s.PlanIndex >= m_Plan.Count())
				continue;

			AISurvivorsStructurePlan plan = m_Plan[s.PlanIndex];

			array<Object> objects = {};
			g_Game.GetObjectsAtPosition3D(s.Position, 1.5, objects, null);

			EntityAI found;
			foreach (Object obj: objects)
			{
				if (obj && obj.GetType() == s.Type && !AISurvivorsManager.IsTribeObject(obj))
				{
					found = EntityAI.Cast(obj);
					break;
				}
			}

			if (!found)
				continue;

			plan.m_Object = found;
			plan.m_Spawned = true;
			plan.m_Complete = s.PlanIndex < m_PlanIndex;
			AISurvivorsManager.RegisterTribeObject(found);

			if (plan.m_Kind == AISV_StructureKind.FLAG)
				Class.CastTo(m_Flag, found);

			if (plan.m_Kind == AISV_StructureKind.FENCE)
				plan.m_ExcludeMaterial = "_metal_";
		}

		//! Anything that was placed before the restart but is now gone gets rebuilt.
		//! (CheckForDamage only looks at plans flagged as spawned, and plans whose object was not found
		//! above are not flagged, so rewind to the first missing structure here.)
		int limit = m_PlanIndex;
		if (limit > m_Plan.Count())
			limit = m_Plan.Count();
		for (int i = 0; i < limit; i++)
		{
			if (!m_Plan[i].m_Object)
			{
				AISurvivorsLog.Debug("Base structure " + m_Plan[i].m_Type + " is missing after the restart - queued for rebuild");
				m_PlanIndex = i;
				break;
			}
		}

		CheckForDamage();
	}
}
