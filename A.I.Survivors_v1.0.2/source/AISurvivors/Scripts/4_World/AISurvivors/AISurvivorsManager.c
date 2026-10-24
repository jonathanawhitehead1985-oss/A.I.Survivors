/**
 * AISurvivorsManager.c
 *
 * Server-side singleton: loads settings and saved state, keeps MaxTribes tribes alive on the map,
 * ticks them, and saves everything periodically and on shutdown.
 */

class AISurvivorsManager : Managed
{
	protected static ref AISurvivorsManager s_Instance;
	protected static ref set<Object> s_TribeObjects = new set<Object>;
	protected static ref AISurvivorsSettings s_DefaultSettings;

	protected ref AISurvivorsSettings m_Settings;
	protected ref AISurvivorsWorld m_World;
	protected ref array<ref AISurvivorsTribe> m_Tribes = {};
	protected int m_NextTribeID = 1;

	protected ref Timer m_TickTimer;
	protected float m_SaveAccumulator;
	protected float m_SpawnAccumulator;
	protected bool m_Started;

	static AISurvivorsManager Get()
	{
		return s_Instance;
	}

	static AISurvivorsSettings GetSettings()
	{
		if (s_Instance && s_Instance.m_Settings)
			return s_Instance.m_Settings;

		//! Client side / before init: defaults (only TribesHostileToEachOther is read this way)
		if (!s_DefaultSettings)
			s_DefaultSettings = new AISurvivorsSettings();
		return s_DefaultSettings;
	}

	static void RegisterTribeObject(Object obj)
	{
		if (obj)
			s_TribeObjects.Insert(obj);
	}

	static void UnregisterTribeObject(Object obj)
	{
		int idx = s_TribeObjects.Find(obj);
		if (idx > -1)
			s_TribeObjects.Remove(idx);
	}

	static bool IsTribeObject(Object obj)
	{
		return obj && s_TribeObjects.Find(obj) > -1;
	}

	//! Called from MissionServer.OnInit
	static void Init()
	{
		if (!g_Game.IsDedicatedServer() && g_Game.IsMultiplayer())
			return;

		if (s_Instance)
			return;

		s_Instance = new AISurvivorsManager();
		s_Instance.Setup();
	}

	static void Shutdown()
	{
		if (!s_Instance)
			return;

		s_Instance.SaveState();
		s_Instance.Stop();
		s_Instance = null;
	}

	protected void Setup()
	{
		m_Settings = AISurvivorsSettings.Load();
		AISurvivorsLog.s_Debug = m_Settings.DebugLogging;

		if (!m_Settings.Enabled)
		{
			AISurvivorsLog.Info("Disabled in settings.");
			return;
		}

		//! Give the world, the CE and Expansion a moment to load (persisted base objects must exist before adopting them)
		float delay = m_Settings.FirstSpawnDelaySeconds;
		if (delay < 15)
			delay = 15;

		AISurvivorsLog.Info("A.I. Survivors starting in " + delay + " seconds");
		int delayMs = delay * 1000;
		g_Game.GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(Start, delayMs, false);
	}

	void Start()
	{
		if (m_Started)
			return;

		m_Started = true;

		m_World = new AISurvivorsWorld();
		m_World.Init(m_Settings);

		if (m_World.GetBaseAnchors().Count() == 0)
		{
			AISurvivorsLog.Warn("No base anchors could be placed on this map - A.I. Survivors is idle. Check BaseAnchors in the settings.");
			return;
		}

		LoadState();

		m_TickTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
		m_TickTimer.Run(2.0, this, "Tick", null, true);

		AISurvivorsLog.Info(string.Format("Running: %1 tribe(s) loaded, max %2", m_Tribes.Count(), m_Settings.MaxTribes));
	}

	protected void Stop()
	{
		if (m_TickTimer)
			m_TickTimer.Stop();

		foreach (AISurvivorsTribe tribe: m_Tribes)
			tribe.Despawn();
	}

	void Tick()
	{
		float dt = 2.0;

		for (int i = m_Tribes.Count() - 1; i >= 0; i--)
		{
			AISurvivorsTribe tribe = m_Tribes[i];

			if (tribe.m_State == AISV_TribeState.WIPED)
			{
				if (tribe.m_Abandoned)
				{
					//! Clean up the empty base after a while (players had their chance to loot it)
					if (m_Settings.AbandonedBaseCleanupMinutes > 0 && AISurvivorsTribe.Now() - tribe.m_WipedAt > m_Settings.AbandonedBaseCleanupMinutes * 60.0)
					{
						if (AISurvivorsWorld.DistanceToNearestPlayer(tribe.m_Base.m_Center) > 300.0)
						{
							AISurvivorsLog.Info(tribe.GetTribeLabel() + " abandoned base removed");
							tribe.m_Base.DeleteAll();
							m_Tribes.Remove(i);
						}
					}
					continue;
				}

				if (AISurvivorsTribe.Now() >= tribe.m_RespawnAt)
				{
					if (!tribe.TryRespawn())
					{
						AISurvivorsLog.Info(tribe.GetTribeLabel() + " base was destroyed - the tribe will not return. A new tribe will settle elsewhere.");
						tribe.m_Abandoned = true;
					}
				}
				continue;
			}

			tribe.Tick(dt);
		}

		//! Fill free tribe slots
		m_SpawnAccumulator += dt;
		if (m_SpawnAccumulator >= 30.0)
		{
			m_SpawnAccumulator = 0;
			if (CountLivingSlots() < m_Settings.MaxTribes)
				SpawnNewTribe();
		}

		m_SaveAccumulator += dt;
		if (m_SaveAccumulator >= 300.0)
		{
			m_SaveAccumulator = 0;
			SaveState();
		}
	}

	protected int CountLivingSlots()
	{
		int count;
		foreach (AISurvivorsTribe tribe: m_Tribes)
		{
			if (!tribe.m_Abandoned)
				count++;
		}
		return count;
	}

	protected array<vector> GetBasePositions()
	{
		array<vector> positions = {};
		foreach (AISurvivorsTribe tribe: m_Tribes)
		{
			if (tribe.m_Base)
				positions.Insert(tribe.m_Base.m_Center);
		}
		return positions;
	}

	protected AISV_Temperament RollTemperament()
	{
		auto w = m_Settings.Temperaments;
		float total = w.Friendly + w.Neutral + w.Hostile + w.Bandit;
		float roll = Math.RandomFloat(0, total);

		if (roll < w.Friendly)
			return AISV_Temperament.FRIENDLY;
		if (roll < w.Friendly + w.Neutral)
			return AISV_Temperament.NEUTRAL;
		if (roll < w.Friendly + w.Neutral + w.Hostile)
			return AISV_Temperament.HOSTILE;

		return AISV_Temperament.BANDIT;
	}

	protected string PickTribeName()
	{
		TStringArray unused = {};
		foreach (string name: m_Settings.TribeNames)
		{
			bool taken = false;
			foreach (AISurvivorsTribe tribe: m_Tribes)
			{
				if (tribe.m_Name == name && !tribe.m_Abandoned)
				{
					taken = true;
					break;
				}
			}

			if (!taken)
				unused.Insert(name);
		}

		if (unused.Count() > 0)
			return unused.GetRandomElement();

		return "Survivors " + m_NextTribeID;
	}

	protected AISurvivorsPlace PickAnchor()
	{
		array<ref AISurvivorsPlace> anchors = m_World.GetBaseAnchors();
		array<AISurvivorsPlace> free = {};

		foreach (AISurvivorsPlace anchor: anchors)
		{
			bool used = false;
			foreach (AISurvivorsTribe tribe: m_Tribes)
			{
				if (!tribe.m_Abandoned && tribe.m_Base && vector.Distance(tribe.m_Base.m_Center, anchor.m_Position) < m_Settings.MinDistanceBetweenBases)
				{
					used = true;
					break;
				}
			}

			if (!used)
				free.Insert(anchor);
		}

		if (free.Count() == 0)
			return null;

		return free.GetRandomElement();
	}

	protected void SpawnNewTribe()
	{
		for (int attempt = 0; attempt < 5; attempt++)
		{
			AISurvivorsPlace anchor = PickAnchor();
			if (!anchor)
			{
				AISurvivorsLog.Debug("No free base anchor left for a new tribe");
				return;
			}

			array<vector> basePositions = GetBasePositions();
			vector site = AISurvivorsWorld.FindBaseSite(anchor.m_Position, m_Settings, basePositions);
			if (site == vector.Zero)
			{
				AISurvivorsLog.Debug("No suitable base site near " + anchor.m_Name + " right now");
				continue;
			}

			AISurvivorsTribe tribe = new AISurvivorsTribe(m_NextTribeID++, PickTribeName(), RollTemperament(), m_Settings, m_World);
			tribe.m_Base = new AISurvivorsBase(site, Math.RandomFloat(-180, 180), anchor.m_Name, m_Settings);
			tribe.m_MemberTarget = Math.RandomIntInclusive(m_Settings.MinMembers, m_Settings.MaxMembers);
			tribe.m_Materials = m_Settings.BuildSettings.StartingMaterials;
			tribe.m_NextRecruitTime = AISurvivorsTribe.Now() + m_Settings.RecruitIntervalMinutes * 60.0;
			m_Tribes.Insert(tribe);

			tribe.SpawnAllAtBase(tribe.m_MemberTarget);

			//! Settling in: build the camp first
			tribe.SetActivity(AISV_Activity.BUILD, 1800);

			if (m_Settings.Vehicles.Enabled && Math.RandomFloat01() < m_Settings.Vehicles.StartWithVehicleChance)
				SpawnBaseVehicle(tribe);

			AISurvivorsLog.Info(string.Format("New tribe %1 (%2 survivors) settled near %3 at %4", tribe.GetTribeLabel(), tribe.m_MemberTarget, anchor.m_Name, site));
			SaveState();
			return;
		}
	}

	protected void SpawnBaseVehicle(AISurvivorsTribe tribe)
	{
		if (m_Settings.Vehicles.VehicleClasses.Count() == 0)
			return;

		string type = m_Settings.Vehicles.VehicleClasses.GetRandomElement();

		for (int attempt = 0; attempt < 10; attempt++)
		{
			vector pos = ExpansionStatic.GetSurfacePosition(ExpansionMath.GetRandomPointInRing(tribe.m_Base.m_Center, m_Settings.BuildSettings.WallRingRadius + 8.0, m_Settings.BuildSettings.WallRingRadius + 20.0));
			if (!AISurvivorsWorld.IsClearOfObjects(pos, 5.0) || AISurvivorsWorld.IsWater(pos))
				continue;

			CarScript car = CarScript.Cast(g_Game.CreateObjectEx(type, pos, ECE_PLACE_ON_SURFACE | ECE_CREATEPHYSICS));
			if (!car)
			{
				AISurvivorsLog.Warn("Could not spawn vehicle " + type);
				return;
			}

			car.SetOrientation(Vector(Math.RandomFloat(-180, 180), 0, 0));
			car.OnDebugSpawn();
			car.Update();
			tribe.m_Base.m_Vehicle = car;
			AISurvivorsLog.Debug(tribe.GetTribeLabel() + " has a " + type);
			return;
		}
	}

	//! ---------------------------------------------------------------------------------------------
	//! Persistence
	//! ---------------------------------------------------------------------------------------------

	protected void LoadState()
	{
		AISurvivorsStateData data = AISurvivorsStateData.Load();

		if (data.WorldName != "" && data.WorldName != g_Game.GetWorldName())
		{
			AISurvivorsLog.Warn("Saved state is for map '" + data.WorldName + "' - ignoring it on '" + g_Game.GetWorldName() + "'");
			return;
		}

		m_NextTribeID = data.NextTribeID;

		foreach (AISurvivorsTribeData tribeData: data.Tribes)
		{
			AISurvivorsTribe tribe = AISurvivorsTribe.Load(tribeData, m_Settings, m_World);
			m_Tribes.Insert(tribe);

			if (tribe.m_ID >= m_NextTribeID)
				m_NextTribeID = tribe.m_ID + 1;

			if (tribe.m_State != AISV_TribeState.WIPED)
			{
				//! Come back after the restart: spawn at the base if nobody is right there, else wait (virtual)
				if (AISurvivorsWorld.DistanceToNearestPlayer(tribe.m_Base.m_Center) > m_Settings.MinSpawnDistanceFromPlayers || !m_Settings.VirtualizeWhenNoPlayersNearby)
				{
					int count = tribe.m_MemberTarget;
					if (count < 1)
						count = m_Settings.MinMembers;
					tribe.SpawnAllAtBase(count);
				}
				else
				{
					tribe.m_State = AISV_TribeState.VIRTUAL;
					tribe.m_VirtualMembers = tribe.m_MemberTarget;
				}

				tribe.m_NextRecruitTime = AISurvivorsTribe.Now() + m_Settings.RecruitIntervalMinutes * 60.0;
			}

			AISurvivorsLog.Info(string.Format("Restored %1 (%2 structures, gear tier %3)", tribe.GetTribeLabel(), tribe.m_Base.GetStructureCount(), tribe.m_GearTier));
		}
	}

	void SaveState()
	{
		if (!m_Started)
			return;

		AISurvivorsStateData data = new AISurvivorsStateData();
		data.WorldName = g_Game.GetWorldName();
		data.NextTribeID = m_NextTribeID;

		foreach (AISurvivorsTribe tribe: m_Tribes)
		{
			if (tribe.m_Abandoned)
				continue;

			data.Tribes.Insert(tribe.Save());
		}

		data.Save();
	}

	//! ---------------------------------------------------------------------------------------------
	//! Admin helpers (call from the server console / your admin tool scripts)
	//! ---------------------------------------------------------------------------------------------

	void DumpStatus()
	{
		foreach (AISurvivorsTribe tribe: m_Tribes)
		{
			AISurvivorsLog.Info(string.Format("%1 state=%2 activity=%3 alive=%4/%5 materials=%6 metal=%7 lootRuns=%8 tier=%9", tribe.GetTribeLabel(), typename.EnumToString(AISV_TribeState, tribe.m_State), typename.EnumToString(AISV_Activity, tribe.m_Activity), tribe.GetAliveCount(), tribe.m_MemberTarget, tribe.m_Materials, tribe.m_Metal, tribe.m_LootRuns, tribe.m_GearTier));
		}
	}
}
