/**
 * AISurvivorsTribe.c
 *
 * The "brain" of one survivor group (3-5 AI). Expansion AI does the moment-to-moment work (moving, shooting,
 * looting buildings, bandaging, eating, skinning, boarding the leader's car). This class decides what the group
 * does next, the way a player group would:
 *
 *   settle -> build camp -> loot towns/military -> come home & stash loot -> build/upgrade -> hunt -> raid players
 *
 * and it turns the whole group extremely hostile the moment a player, another AI or a zombie walks into the base.
 */

class AISurvivorsTribe : Managed
{
	int m_ID;
	string m_Name;
	AISV_Temperament m_Temperament;
	AISV_TribeState m_State = AISV_TribeState.ACTIVE;
	ref AISurvivorsBase m_Base;

	//! Field squad (does the activities) and home guards (patrol the base). Same tribe, so they are allies.
	ref eAIGroup m_Squad;
	ref eAIGroup m_Guards;
	ref array<eAIBase> m_Members = {};

	int m_MemberTarget;
	int m_VirtualMembers;
	int m_GearTier;
	int m_LootRuns;
	int m_Materials;
	int m_Metal;

	AISV_Activity m_Activity = AISV_Activity.NONE;
	float m_ActivityStart;
	float m_ActivityEnd;
	vector m_ActivityTarget;
	AISurvivorsPlace m_LootPlace;
	ref array<AISurvivorsPlace> m_LootPlaces = {};
	PlayerBase m_RaidTarget;
	int m_BuildStepsThisSession;
	float m_NextBuildTime;
	//! When the builder started trying to reach the current build spot (0 = standing there)
	float m_BuildWaitStart;
	float m_LastBuildMoveOrder;

	float m_AlarmUntil;
	ref map<string, float> m_Grudges = new map<string, float>;
	float m_NextRecruitTime;
	float m_RespawnAt;
	float m_WipedAt;
	bool m_Abandoned;
	float m_NextVirtualCycle;

	//! Vehicle trip
	AISV_TripPhase m_TripPhase = AISV_TripPhase.NONE;
	CarScript m_TripCar;
	vector m_TripDestination;
	float m_TripPhaseStart;
	bool m_TripBoardRequested;
	AISV_Activity m_TripFollowUp;

	protected float m_BrainAccumulator;
	protected float m_UpkeepAccumulator;
	protected float m_DamageCheckAccumulator;

	protected AISurvivorsSettings m_Settings;
	protected AISurvivorsWorld m_World;

	void AISurvivorsTribe(int id, string name, AISV_Temperament temperament, AISurvivorsSettings settings, AISurvivorsWorld world)
	{
		m_ID = id;
		m_Name = name;
		m_Temperament = temperament;
		m_Settings = settings;
		m_World = world;
	}

	void ~AISurvivorsTribe()
	{
		if (m_TripCar && m_TripCar.m_AISV_Autopilot)
			m_TripCar.m_AISV_Autopilot.Stop();
	}

	string GetTribeLabel()
	{
		return string.Format("[%1 #%2 %3]", m_Name, m_ID, typename.EnumToString(AISV_Temperament, m_Temperament));
	}

	static float Now()
	{
		return g_Game.GetTickTime();
	}

	//! =============================================================================================
	//! Hostility (used by eAIFactionAISurvivors)
	//! =============================================================================================

	bool IsAlarmed()
	{
		return m_AlarmUntil > Now();
	}

	//! Base intruders are handled separately (forced max threat), so the guard logic only governs people outside the base
	bool AISV_UseGuardLogic()
	{
		return m_Temperament == AISV_Temperament.FRIENDLY || m_Temperament == AISV_Temperament.NEUTRAL;
	}

	bool AISV_ToleratesPlayer(PlayerBase player)
	{
		if (!player || !player.IsAlive())
			return true;

		//! Inside the base while we are alerted = dead
		if (IsAlarmed() && m_Base && m_Base.IsInside(player.GetPosition(), m_Settings.BaseRadius + 15.0))
			return false;

		//! Anyone inside the base radius is never tolerated, alarm or not
		if (m_Base && m_Base.IsInside(player.GetPosition(), m_Settings.BaseRadius))
			return false;

		if (HasGrudge(player))
			return false;

		if (m_Activity == AISV_Activity.RAID && player == m_RaidTarget)
			return false;

		return m_Temperament == AISV_Temperament.FRIENDLY;
	}

	static string GetPlayerKey(PlayerBase player)
	{
		if (player && player.GetIdentity())
			return player.GetIdentity().GetId();

		return "";
	}

	bool HasGrudge(PlayerBase player)
	{
		string key = GetPlayerKey(player);
		if (key == "")
			return false;

		float until;
		if (m_Grudges.Find(key, until))
		{
			if (until > Now())
				return true;

			m_Grudges.Remove(key);
		}

		return false;
	}

	void AddGrudge(PlayerBase player, float minutes)
	{
		string key = GetPlayerKey(player);
		if (key == "")
			return;

		float until = Now() + minutes * 60.0;
		float current;
		if (m_Grudges.Find(key, current) && current > until)
			return;

		m_Grudges.Set(key, until);
	}

	//! =============================================================================================
	//! Members & groups
	//! =============================================================================================

	protected eAIGroup CreateTribeGroup(string suffix)
	{
		eAIFactionAISurvivors faction = new eAIFactionAISurvivors();
		faction.AISV_SetTribe(this);

		eAIGroup group = eAIGroup.CreateGroup(faction);
		group.SetName(m_Name + suffix);
		return group;
	}

	protected void PruneMembers()
	{
		for (int i = m_Members.Count() - 1; i >= 0; i--)
		{
			eAIBase ai = m_Members[i];
			if (!ai || !ai.IsAlive())
				m_Members.Remove(i);
		}

		//! Drop references to emptied groups so a fresh one is created for the next member
		if (m_Squad && m_Squad.Count() == 0)
			m_Squad = null;

		if (m_Guards && m_Guards.Count() == 0)
			m_Guards = null;
	}

	int GetAliveCount()
	{
		int count;
		foreach (eAIBase ai: m_Members)
		{
			if (ai && ai.IsAlive())
				count++;
		}
		return count;
	}

	eAIBase GetSquadLeader()
	{
		if (!m_Squad || m_Squad.Count() == 0)
			return null;

		return eAIBase.Cast(m_Squad.GetFormationLeader());
	}

	vector GetSquadPosition()
	{
		eAIBase leader = GetSquadLeader();
		if (leader)
			return leader.GetPosition();

		if (m_Base)
			return m_Base.m_Center;

		return vector.Zero;
	}

	bool IsSquadHome()
	{
		eAIBase leader = GetSquadLeader();
		if (!leader)
			return true;

		return m_Base.IsInside(leader.GetPosition(), 35.0);
	}

	protected string PickLoadout()
	{
		if (m_Settings.GearTiers.Count() == 0)
			return "SurvivorLoadout";

		int tier = m_GearTier;
		if (tier >= m_Settings.GearTiers.Count())
			tier = m_Settings.GearTiers.Count() - 1;
		if (tier < 0)
			tier = 0;

		AISurvivorsGearTier gear = m_Settings.GearTiers[tier];
		if (!gear || !gear.Loadouts || gear.Loadouts.Count() == 0)
			return "SurvivorLoadout";

		return gear.Loadouts.GetRandomElement();
	}

	eAIBase SpawnMember(vector pos, bool asGuard)
	{
		pos = ExpansionAISpawnBase.GetPlacementPosition(pos);

		string unit = eAISurvivor.GetQuasiRandom();
		eAIBase ai = eAIBase.Cast(g_Game.CreateObjectEx(unit, pos, ECE_CREATEPHYSICS | ECE_KEEPHEIGHT | ECE_NOSURFACEALIGN));
		if (!ai)
		{
			AISurvivorsLog.Warn(GetTribeLabel() + " could not create AI " + unit);
			return null;
		}

		ai.SetPosition(pos);
		ai.SetOrientation(Vector(Math.RandomFloat(-180, 180), 0, 0));

		ExpansionHumanLoadout.Apply(ai, PickLoadout(), false);

		if (Math.RandomFloat01() < m_Settings.AI.KnifeChance)
			ai.GetInventory().CreateInInventory("HuntingKnife");

		auto stats = m_Settings.AI;
		ai.SetMovementSpeedLimits(eAIMovementSpeed.JOG, eAIMovementSpeed.SPRINT);
		ai.Expansion_SetCanBeLooted(stats.CanBeLooted);
		ai.eAI_SetUnlimitedReload(stats.UnlimitedReload);
		ai.eAI_SetAccuracy(stats.AccuracyMin, stats.AccuracyMax);
		ai.eAI_SetThreatDistanceLimit(stats.ThreatDistanceLimit);
		ai.eAI_SetNoiseInvestigationDistanceLimit(stats.NoiseInvestigationDistanceLimit);
		ai.eAI_SetDamageMultiplier(stats.DamageMultiplier);
		ai.eAI_SetDamageReceivedMultiplier(stats.DamageReceivedMultiplier);
		ai.m_eAI_HeadshotResistance = stats.HeadshotResistance;
		ai.eAI_SetLootingBehavior(eAILootingBehavior.ALL);

		ai.m_AISV_Tribe = this;

		eAIGroup group;
		if (asGuard)
		{
			if (!m_Guards)
				m_Guards = CreateTribeGroup(" (home)");
			group = m_Guards;
		}
		else
		{
			if (!m_Squad)
				m_Squad = CreateTribeGroup("");
			group = m_Squad;
		}

		ai.SetGroup(group);
		m_Members.Insert(ai);

		if (asGuard)
			SetupGuardPatrol();

		AISurvivorsLog.Debug(GetTribeLabel() + " spawned member " + unit + " at " + pos);
		return ai;
	}

	//! Spawn the whole tribe at its base (new tribe, respawn after wipe or coming back from virtual)
	void SpawnAllAtBase(int count)
	{
		for (int i = 0; i < count; i++)
		{
			vector pos = ExpansionMath.GetRandomPointInCircle(m_Base.m_Center, 6.0);
			SpawnMember(pos, false);
		}

		RebalanceGroups();
		SetActivity(AISV_Activity.REST, Math.RandomFloat(20, 60));
	}

	//! Keep N members at home as guards when the tribe is big enough
	void RebalanceGroups()
	{
		int alive = GetAliveCount();
		int wantedGuards = 0;
		if (alive >= 4)
		{
			wantedGuards = m_Settings.BaseGuards;
			if (wantedGuards > alive - 2)
				wantedGuards = alive - 2;
			if (wantedGuards < 0)
				wantedGuards = 0;
		}

		int currentGuards = 0;
		if (m_Guards)
			currentGuards = m_Guards.Count();

		//! Move from squad to guards
		while (currentGuards < wantedGuards && m_Squad && m_Squad.Count() > 1)
		{
			eAIBase toGuard = eAIBase.Cast(m_Squad.GetMember(m_Squad.Count() - 1));
			if (!toGuard)
				break;

			if (!m_Guards)
				m_Guards = CreateTribeGroup(" (home)");

			toGuard.SetGroup(m_Guards);
			currentGuards++;
		}

		//! Move from guards to squad
		while (currentGuards > wantedGuards && m_Guards && m_Guards.Count() > 0)
		{
			eAIBase toSquad = eAIBase.Cast(m_Guards.GetMember(m_Guards.Count() - 1));
			if (!toSquad)
				break;

			if (!m_Squad)
				m_Squad = CreateTribeGroup("");

			toSquad.SetGroup(m_Squad);
			currentGuards--;
		}

		SetupGuardPatrol();
	}

	protected void SetupGuardPatrol()
	{
		if (!m_Guards || m_Guards.Count() == 0)
			return;

		m_Guards.ClearWaypoints();
		array<vector> guardPoints = m_Base.GetPatrolPoints();
		foreach (vector p: guardPoints)
			m_Guards.AddWaypoint(p);
		m_Guards.SetWaypointBehaviour(eAIWaypointBehavior.LOOP);
		SetGroupSpeed(m_Guards, eAIMovementSpeed.WALK, eAIMovementSpeed.SPRINT);
	}

	protected void SetGroupSpeed(eAIGroup group, int speed, int threatSpeed)
	{
		if (!group)
			return;

		for (int i = 0; i < group.Count(); i++)
		{
			eAIBase ai = eAIBase.Cast(group.GetMember(i));
			if (ai)
				ai.SetMovementSpeedLimits(speed, threatSpeed);
		}
	}

	protected void SquadMoveTo(vector pos, int speed)
	{
		if (!m_Squad)
			return;

		m_Squad.ClearWaypoints();
		m_Squad.AddWaypoint(pos);
		m_Squad.SetWaypointBehaviour(eAIWaypointBehavior.HALT);
		SetGroupSpeed(m_Squad, speed, eAIMovementSpeed.SPRINT);
	}

	protected void SquadPatrol(array<vector> points, int speed)
	{
		if (!m_Squad || points.Count() == 0)
			return;

		m_Squad.ClearWaypoints();
		foreach (vector p: points)
			m_Squad.AddWaypoint(p);
		m_Squad.SetWaypointBehaviour(eAIWaypointBehavior.LOOP);
		SetGroupSpeed(m_Squad, speed, eAIMovementSpeed.SPRINT);
	}

	//! Expansion's ROAMING behaviour: walk from place to place and loot every building on the way
	protected void SquadRoam()
	{
		if (!m_Squad)
			return;

		m_Squad.ClearWaypoints();
		RefillRoamingLocations(true);
		m_Squad.SetWaypointBehaviour(eAIWaypointBehavior.ROAMING);
		SetGroupSpeed(m_Squad, eAIMovementSpeed.JOG, eAIMovementSpeed.SPRINT);
	}

	//! Expansion falls back to the whole-map list when a group has < 2 roaming locations left, so keep ours topped up
	protected void RefillRoamingLocations(bool force)
	{
		if (!m_Squad)
			return;

		if (!force && m_Squad.m_RoamingLocations.Count() >= 3)
			return;

		m_Squad.m_RoamingLocations.Clear();
		foreach (AISurvivorsPlace place: m_LootPlaces)
		{
			if (!place)
				continue;

			m_Squad.m_RoamingLocations.Insert(new ExpansionAIRoamingLocation(place.m_Position, place.m_Radius, place.m_Name, place.m_Type));
		}
	}

	//! =============================================================================================
	//! Tick
	//! =============================================================================================

	void Tick(float dt)
	{
		switch (m_State)
		{
			case AISV_TribeState.WIPED:
				return;

			case AISV_TribeState.VIRTUAL:
				TickVirtual();
				return;
		}

		PruneMembers();

		if (m_Members.Count() == 0)
		{
			OnWiped();
			return;
		}

		CheckIntruders();

		if (m_TripPhase != AISV_TripPhase.NONE)
			UpdateTrip();

		m_BrainAccumulator += dt;
		if (m_BrainAccumulator >= 5.0)
		{
			m_BrainAccumulator = 0;
			UpdateBrain();
		}

		m_DamageCheckAccumulator += dt;
		if (m_DamageCheckAccumulator >= 60.0)
		{
			m_DamageCheckAccumulator = 0;
			m_Base.CheckForDamage();
			TryRecruit();
			TryVirtualize();
		}

		m_UpkeepAccumulator += dt;
		if (m_UpkeepAccumulator >= 600.0)
		{
			m_UpkeepAccumulator = 0;
			m_Base.Upkeep();
		}
	}

	//! =============================================================================================
	//! Base defence
	//! =============================================================================================

	protected void CheckIntruders()
	{
		vector center = m_Base.m_Center;
		float radius = m_Settings.BaseRadius;

		array<Man> intruderPlayers = {};
		array<EntityAI> intruderOthers = {};

		array<Man> players = {};
		g_Game.GetPlayers(players);
		foreach (Man man: players)
		{
			PlayerBase pb;
			if (!Class.CastTo(pb, man) || !pb.IsAlive() || !pb.GetIdentity())
				continue;

			if (pb.Expansion_HasAdminToolInvisibility())
				continue;

			if (m_Base.IsInside(pb.GetPosition(), radius))
				intruderPlayers.Insert(pb);
		}

		array<Object> objects = {};
		g_Game.GetObjectsAtPosition(center, radius, objects, null);
		foreach (Object obj: objects)
		{
			ZombieBase zombie;
			if (Class.CastTo(zombie, obj))
			{
				if (zombie.IsAlive())
					intruderOthers.Insert(zombie);
				continue;
			}

			eAIBase otherAI;
			if (Class.CastTo(otherAI, obj))
			{
				if (otherAI.IsAlive() && otherAI.m_AISV_Tribe != this)
					intruderPlayers.Insert(otherAI);
			}
		}

		if (intruderPlayers.Count() == 0 && intruderOthers.Count() == 0)
		{
			if (m_Activity == AISV_Activity.DEFEND && !IsAlarmed())
				OnAlarmEnded();
			return;
		}

		bool wasAlarmed = IsAlarmed();
		m_AlarmUntil = Now() + m_Settings.AlarmDurationSeconds;

		if (!wasAlarmed)
			OnAlarmStarted(intruderPlayers.Count(), intruderOthers.Count());

		//! Every member knows exactly where every intruder is
		foreach (Man intruder: intruderPlayers)
		{
			DayZPlayerImplement dpi;
			if (Class.CastTo(dpi, intruder))
			{
				PlayerBase intruderPlayer;
				if (Class.CastTo(intruderPlayer, intruder) && !intruderPlayer.IsAI())
					AddGrudge(intruderPlayer, m_Settings.GrudgeMinutes);

				AlertGroup(m_Squad, dpi.GetTargetInformation());
				AlertGroup(m_Guards, dpi.GetTargetInformation());
			}
		}

		foreach (EntityAI other: intruderOthers)
		{
			ZombieBase z;
			if (Class.CastTo(z, other))
			{
				AlertGroup(m_Squad, z.GetTargetInformation());
				AlertGroup(m_Guards, z.GetTargetInformation());
			}
		}
	}

	protected void AlertGroup(eAIGroup group, eAITargetInformation info)
	{
		if (!group || group.Count() == 0 || !info)
			return;

		//! Squad members far away (beyond recall distance) don't magically know about it
		DayZPlayerImplement leader = group.GetLeader();
		if (!leader)
			return;

		if (group == m_Squad && !m_Base.IsInside(leader.GetPosition(), m_Settings.BaseRecallDistance))
			return;

		group.AddTarget(leader, info, -1, true, 1.0);
	}

	protected void OnAlarmStarted(int players, int others)
	{
		AISurvivorsLog.Info(string.Format("%1 base alarm! %2 player(s)/AI and %3 zombie(s) inside the perimeter", GetTribeLabel(), players, others));

		SetGroupSpeed(m_Guards, eAIMovementSpeed.JOG, eAIMovementSpeed.SPRINT);

		//! Recall the squad if it is close enough to hear about it
		eAIBase leader = GetSquadLeader();
		if (leader && m_Base.IsInside(leader.GetPosition(), m_Settings.BaseRecallDistance))
		{
			AbortTrip(false);
			SetActivity(AISV_Activity.DEFEND, m_Settings.AlarmDurationSeconds + 60.0);
		}
	}

	protected void OnAlarmEnded()
	{
		AISurvivorsLog.Debug(GetTribeLabel() + " alarm over");
		RebalanceGroups();
		SetActivity(AISV_Activity.REST, Math.RandomFloat(30, 90));
	}

	//! =============================================================================================
	//! Events from members
	//! =============================================================================================

	void OnMemberHit(eAIBase member, PlayerBase attacker)
	{
		if (!attacker || attacker.IsAI())
			return;

		AddGrudge(attacker, m_Settings.GrudgeMinutes);
	}

	void OnMemberKilled(eAIBase member, Object killer)
	{
		m_Members.RemoveItem(member);

		PlayerBase killerPlayer;
		EntityAI killerEntity;
		if (Class.CastTo(killerEntity, killer))
			Class.CastTo(killerPlayer, killerEntity.GetHierarchyRoot());

		if (killerPlayer && !killerPlayer.IsAI() && killerPlayer.GetIdentity())
		{
			AISurvivorsLog.Info(string.Format("%1 lost a member to player %2", GetTribeLabel(), killerPlayer.GetIdentity().GetName()));
			AddGrudge(killerPlayer, m_Settings.GrudgeMinutes * 2.0);

			//! Like real players: revenge. Go after the killer if enough of us are left.
			if (GetAliveCount() >= 2 && m_Activity != AISV_Activity.DEFEND && killerPlayer.IsAlive())
			{
				m_RaidTarget = killerPlayer;
				AbortTrip(false);
				SetActivity(AISV_Activity.RAID, Math.RandomFloat(m_Settings.RaidMinutesMin, m_Settings.RaidMinutesMax) * 60.0);
			}
		}

		m_NextRecruitTime = Math.Max(m_NextRecruitTime, Now() + m_Settings.RecruitIntervalMinutes * 60.0);
	}

	//! =============================================================================================
	//! Activities
	//! =============================================================================================

	void SetActivity(AISV_Activity activity, float durationSeconds)
	{
		m_Activity = activity;
		m_ActivityStart = Now();
		m_ActivityEnd = Now() + durationSeconds;

		AISurvivorsLog.Debug(GetTribeLabel() + " -> " + typename.EnumToString(AISV_Activity, activity) + " for " + Math.Round(durationSeconds) + "s");

		switch (activity)
		{
			case AISV_Activity.REST:
				SquadMoveTo(ExpansionMath.GetRandomPointInCircle(m_Base.m_Center, 4.0), eAIMovementSpeed.WALK);
				break;

			case AISV_Activity.BUILD:
				m_BuildStepsThisSession = 0;
				m_NextBuildTime = Now() + 5.0;
				m_BuildWaitStart = 0;
				m_LastBuildMoveOrder = Now();
				SquadMoveTo(m_Base.GetCurrentBuildPosition(), eAIMovementSpeed.WALK);
				break;

			case AISV_Activity.LOOT:
				StartLootRun();
				break;

			case AISV_Activity.HUNT:
				StartHunt();
				break;

			case AISV_Activity.RAID:
				StartRaid();
				break;

			case AISV_Activity.RETURN:
				StartReturn();
				break;

			case AISV_Activity.DEFEND:
				if (IsSquadHome())
					SquadPatrol(m_Base.GetPatrolPoints(), eAIMovementSpeed.JOG);
				else
					SquadMoveTo(m_Base.m_Center, eAIMovementSpeed.SPRINT);
				break;
		}
	}

	protected void UpdateBrain()
	{
		if (!m_Squad)
		{
			//! Only guards left - move the extra ones into a new field squad
			if (m_Guards && m_Guards.Count() > 0)
				RebalanceGroups();
			return;
		}

		RefillRoamingLocations(false);

		//! A car trip is in progress - the trip logic drives the squad
		if (m_TripPhase != AISV_TripPhase.NONE)
			return;

		float now = Now();

		switch (m_Activity)
		{
			case AISV_Activity.NONE:
				ChooseNextActivity();
				return;

			case AISV_Activity.DEFEND:
				if (!IsAlarmed())
				{
					OnAlarmEnded();
					return;
				}
				if (IsSquadHome() && m_Squad.GetWaypointBehaviour() != eAIWaypointBehavior.LOOP)
					SquadPatrol(m_Base.GetPatrolPoints(), eAIMovementSpeed.JOG);
				return;

			case AISV_Activity.REST:
				UpdateRest();
				if (now >= m_ActivityEnd)
					ChooseNextActivity();
				return;

			case AISV_Activity.BUILD:
				UpdateBuild();
				return;

			case AISV_Activity.RAID:
				UpdateRaid();
				if (now >= m_ActivityEnd || !m_RaidTarget || !m_RaidTarget.IsAlive())
					SetActivity(AISV_Activity.RETURN, 3600);
				return;

			case AISV_Activity.LOOT:
			case AISV_Activity.HUNT:
				if (now >= m_ActivityEnd)
					SetActivity(AISV_Activity.RETURN, 3600);
				return;

			case AISV_Activity.RETURN:
				//! Taking far too long (stuck, lost) - give up and treat it as being home soon
				if (now >= m_ActivityEnd)
				{
					SetActivity(AISV_Activity.REST, 600);
					return;
				}
				UpdateReturn();
				return;
		}
	}

	protected void ChooseNextActivity()
	{
		//! Build whenever there is something to build and we can afford it
		if (!m_Base.IsComplete() && m_Materials >= m_Base.GetCurrentCost() && Math.RandomFloat01() < 0.7)
		{
			SetActivity(AISV_Activity.BUILD, 1800);
			return;
		}

		auto w = m_Settings.Activities;
		float loot = w.Loot;
		float hunt = w.Hunt;
		float raid = 0;
		float rest = w.Rest;

		if (m_Temperament == AISV_Temperament.HOSTILE)
			raid = w.Raid;
		else if (m_Temperament == AISV_Temperament.BANDIT)
			raid = w.Raid * 2.0;

		//! Running low on materials makes looting more attractive
		if (!m_Base.IsComplete() && m_Materials < m_Base.GetCurrentCost())
			loot *= 1.5;

		float total = loot + hunt + raid + rest;
		float roll = Math.RandomFloat(0, total);

		if (roll < loot)
			SetActivity(AISV_Activity.LOOT, Math.RandomFloat(m_Settings.LootRunMinutesMin, m_Settings.LootRunMinutesMax) * 60.0);
		else if (roll < loot + hunt)
			SetActivity(AISV_Activity.HUNT, Math.RandomFloat(m_Settings.HuntMinutesMin, m_Settings.HuntMinutesMax) * 60.0);
		else if (roll < loot + hunt + raid)
			SetActivity(AISV_Activity.RAID, Math.RandomFloat(m_Settings.RaidMinutesMin, m_Settings.RaidMinutesMax) * 60.0);
		else
			SetActivity(AISV_Activity.REST, Math.RandomFloat(m_Settings.RestMinutesMin, m_Settings.RestMinutesMax) * 60.0);
	}

	bool IsHunting()
	{
		return m_Activity == AISV_Activity.HUNT && m_State == AISV_TribeState.ACTIVE;
	}

	//! ---- Rest ----------------------------------------------------------------------------------

	protected void UpdateRest()
	{
		if (!m_Squad)
			return;

		for (int i = 0; i < m_Squad.Count(); i++)
		{
			eAIBase ai = eAIBase.Cast(m_Squad.GetMember(i));
			if (!ai || ai.GetThreatToSelf() > 0.15 || ai.IsInTransport())
				continue;

			if (!m_Base.IsInside(ai.GetPosition(), 12.0))
				continue;

			if (Math.RandomFloat01() < 0.12)
			{
				int emote;
				switch (Math.RandomInt(0, 4))
				{
					case 0:
						emote = EmoteConstants.ID_EMOTE_CAMPFIRE;
						break;
					case 1:
						emote = EmoteConstants.ID_EMOTE_SITA;
						break;
					case 2:
						emote = EmoteConstants.ID_EMOTE_SITB;
						break;
					default:
						emote = EmoteConstants.ID_EMOTE_WATCHING;
						break;
				}
				ai.Expansion_SetEmote(emote, true, Math.RandomInt(15000, 40000));
			}
		}
	}

	//! ---- Build ---------------------------------------------------------------------------------

	protected void UpdateBuild()
	{
		if (m_Base.IsComplete() || m_BuildStepsThisSession >= m_Settings.BuildSettings.MaxStepsPerSession || m_Materials < m_Base.GetCurrentCost() || Now() >= m_ActivityEnd)
		{
			SetActivity(AISV_Activity.REST, Math.RandomFloat(m_Settings.RestMinutesMin, m_Settings.RestMinutesMax) * 60.0);
			return;
		}

		eAIBase builder = GetSquadLeader();
		if (!builder)
			return;

		vector buildPos = m_Base.GetCurrentBuildPosition();
		if (vector.Distance(builder.GetPosition(), buildPos) > 8.0)
		{
			if (m_BuildWaitStart <= 0)
				m_BuildWaitStart = Now();

			//! Re-issue the move order regularly. Tested in-game: the AI often gets pulled away from the
			//! spot (zombies, other tribes, looting) after reaching the old waypoint, and with a single
			//! HALT waypoint it never walks back, which stalled the base after the first wall.
			if (Now() - m_LastBuildMoveOrder >= 15.0 || m_Squad.GetCurrentWaypoint() != buildPos)
			{
				SquadMoveTo(buildPos, eAIMovementSpeed.WALK);
				m_LastBuildMoveOrder = Now();
			}

			//! Building is simulated anyway: if they can't reach the exact spot within a minute but are
			//! at the base, carry on so the base never stalls.
			if (Now() - m_BuildWaitStart < 60.0 || !m_Base.IsInside(builder.GetPosition(), m_Settings.BuildSettings.WallRingRadius + 30.0))
				return;

			AISurvivorsLog.Debug(GetTribeLabel() + " builder could not reach the build spot - building from where they stand");
		}

		m_BuildWaitStart = 0;

		if (Now() < m_NextBuildTime)
			return;

		int cost = m_Base.GetCurrentCost();
		int planBefore = m_Base.m_PlanIndex;

		//! A step can return false when it only finished a structure - try a couple of times
		bool built;
		for (int attempt = 0; attempt < 3 && !built && !m_Base.IsComplete(); attempt++)
			built = m_Base.BuildStep(builder, m_Metal);

		if (built)
		{
			m_Materials -= cost;
			if (m_Materials < 0)
				m_Materials = 0;

			m_BuildStepsThisSession++;
			builder.Expansion_SetEmote(EmoteConstants.ID_EMOTE_THUMB, true);
		}

		m_NextBuildTime = Now() + m_Settings.BuildSettings.BuildStepSeconds;

		//! Walk to the next spot as soon as it changes (a new structure was placed or one was finished)
		vector nextPos = m_Base.GetCurrentBuildPosition();
		if (m_Base.m_PlanIndex != planBefore || vector.Distance(nextPos, buildPos) > 3.0)
		{
			SquadMoveTo(nextPos, eAIMovementSpeed.WALK);
			m_LastBuildMoveOrder = Now();
		}
	}

	//! ---- Loot ----------------------------------------------------------------------------------

	protected void PickLootPlaces()
	{
		m_LootPlaces.Clear();
		m_LootPlace = null;

		array<AISurvivorsPlace> candidates = {};
		array<float> weights = {};
		float total;

		array<ref AISurvivorsPlace> allLootPlaces = m_World.GetLootPlaces();
		foreach (AISurvivorsPlace place: allLootPlaces)
		{
			float dist = vector.Distance(place.m_Position, m_Base.m_Center);
			if (dist > m_Settings.LootRadius)
				continue;

			float weight = place.m_Weight;

			//! Military is dangerous - fresh tribes prefer towns, geared tribes go military
			if (place.IsMilitary())
				weight *= 0.4 + m_GearTier * 0.4;

			//! Closer is more attractive (but not exclusively)
			weight *= ExpansionMath.LinearConversion(0, m_Settings.LootRadius, dist, 1.5, 0.5);

			candidates.Insert(place);
			weights.Insert(weight);
			total += weight;
		}

		if (candidates.Count() == 0)
		{
			//! Nothing in range - just take the closest place on the map
			float best = float.MAX;
			foreach (AISurvivorsPlace p2: allLootPlaces)
			{
				float d2 = vector.Distance(p2.m_Position, m_Base.m_Center);
				if (d2 < best)
				{
					best = d2;
					m_LootPlace = p2;
				}
			}
		}
		else
		{
			float roll = Math.RandomFloat(0, total);
			for (int i = 0; i < candidates.Count(); i++)
			{
				roll -= weights[i];
				if (roll <= 0)
				{
					m_LootPlace = candidates[i];
					break;
				}
			}

			if (!m_LootPlace)
				m_LootPlace = candidates[candidates.Count() - 1];
		}

		if (!m_LootPlace)
			return;

		m_LootPlaces.Insert(m_LootPlace);

		//! Add up to two neighbours of the main target so the group moves on like players do
		foreach (AISurvivorsPlace p3: allLootPlaces)
		{
			if (p3 == m_LootPlace || m_LootPlaces.Count() >= 3)
				continue;

			if (vector.Distance(p3.m_Position, m_LootPlace.m_Position) < 900.0)
				m_LootPlaces.Insert(p3);
		}
	}

	protected void StartLootRun()
	{
		PickLootPlaces();
		if (!m_LootPlace)
		{
			SetActivity(AISV_Activity.REST, 120);
			return;
		}

		m_ActivityTarget = m_LootPlace.m_Position;
		AISurvivorsLog.Debug(GetTribeLabel() + " heading out to loot " + m_LootPlace.m_Name);

		if (TryStartTrip(m_LootPlace.m_Position, AISV_Activity.LOOT))
			return;

		SquadRoam();
	}

	//! ---- Hunt ----------------------------------------------------------------------------------

	protected void StartHunt()
	{
		vector ground = AISurvivorsWorld.FindHuntingGround(m_Base.m_Center, 400.0, 1100.0);
		m_ActivityTarget = ground;

		array<vector> loop = {};
		for (int i = 0; i < 4; i++)
		{
			float rad = (i * 90.0 + Math.RandomFloat(-20, 20)) * Math.DEG2RAD;
			loop.Insert(ExpansionStatic.GetSurfacePosition(Vector(ground[0] + Math.Sin(rad) * 140.0, 0, ground[2] + Math.Cos(rad) * 140.0)));
		}

		SquadPatrol(loop, eAIMovementSpeed.WALK);
	}

	//! ---- Raid (PvP) ----------------------------------------------------------------------------

	protected PlayerBase FindRaidTarget()
	{
		vector from = GetSquadPosition();

		array<Man> players = {};
		g_Game.GetPlayers(players);

		PlayerBase best;
		float bestDist = m_Settings.RaidSearchRadius;
		foreach (Man man: players)
		{
			PlayerBase pb;
			if (!Class.CastTo(pb, man) || !pb.IsAlive() || !pb.GetIdentity())
				continue;

			if (pb.Expansion_IsInSafeZone() || pb.Expansion_HasAdminToolInvisibility())
				continue;

			float d = vector.Distance(pb.GetPosition(), from);
			if (d < bestDist)
			{
				bestDist = d;
				best = pb;
			}
		}

		return best;
	}

	protected void StartRaid()
	{
		if (!m_RaidTarget || !m_RaidTarget.IsAlive())
			m_RaidTarget = FindRaidTarget();

		if (!m_RaidTarget)
		{
			//! Nobody around to hunt - go loot instead
			SetActivity(AISV_Activity.LOOT, Math.RandomFloat(m_Settings.LootRunMinutesMin, m_Settings.LootRunMinutesMax) * 60.0);
			return;
		}

		AISurvivorsLog.Info(GetTribeLabel() + " is hunting player " + m_RaidTarget.GetIdentity().GetName());

		m_ActivityTarget = m_RaidTarget.GetPosition();
		if (TryStartTrip(m_ActivityTarget, AISV_Activity.RAID))
			return;

		SquadMoveTo(m_ActivityTarget, eAIMovementSpeed.JOG);
	}

	protected void UpdateRaid()
	{
		if (!m_RaidTarget || !m_RaidTarget.IsAlive())
			return;

		if (m_RaidTarget.Expansion_IsInSafeZone())
		{
			m_RaidTarget = null;
			return;
		}

		vector targetPos = m_RaidTarget.GetPosition();

		//! Don't chase forever across the map
		if (vector.Distance(targetPos, m_Base.m_Center) > m_Settings.LootRadius + m_Settings.RaidSearchRadius)
		{
			m_RaidTarget = null;
			return;
		}

		//! Update the chase waypoint when the target moved
		if (vector.Distance(targetPos, m_ActivityTarget) > 30.0)
		{
			m_ActivityTarget = targetPos;
			SquadMoveTo(targetPos, eAIMovementSpeed.JOG);
		}
	}

	//! ---- Return home ---------------------------------------------------------------------------

	protected void StartReturn()
	{
		m_RaidTarget = null;
		m_ActivityTarget = m_Base.m_Center;

		if (TryStartTrip(m_Base.m_Center, AISV_Activity.RETURN))
			return;

		SquadMoveTo(m_Base.m_Center, eAIMovementSpeed.JOG);
	}

	protected void UpdateReturn()
	{
		eAIBase leader = GetSquadLeader();
		if (!leader)
			return;

		if (!m_Base.IsInside(leader.GetPosition(), 20.0))
		{
			if (m_Squad.GetCurrentWaypoint() == vector.Zero || m_Squad.GetWaypoints().Count() == 0)
				SquadMoveTo(m_Base.m_Center, eAIMovementSpeed.JOG);
			return;
		}

		OnArrivedHome();
	}

	protected void OnArrivedHome()
	{
		float tripMinutes = (Now() - m_ActivityStart) / 60.0;

		//! Rewards for a loot run, scaled by where they went
		if (m_LootPlace)
		{
			m_LootRuns++;

			int materials = Math.RandomIntInclusive(2, 4);
			int metal = 0;
			string t = m_LootPlace.m_Type;
			t.ToLower();

			if (m_LootPlace.IsMilitary())
			{
				metal = Math.RandomIntInclusive(2, 4);
			}
			else if (t.Contains("industrial"))
			{
				materials += Math.RandomIntInclusive(1, 3);
				metal = Math.RandomIntInclusive(1, 3);
			}
			else if (t.Contains("city") || t.Contains("town"))
			{
				materials += Math.RandomIntInclusive(1, 4);
			}

			m_Materials += materials;
			m_Metal += metal;
			UpdateGearTier();

			AISurvivorsLog.Debug(string.Format("%1 back from %2 (%3 min): +%4 materials +%5 metal, %6 loot runs, gear tier %7", GetTribeLabel(), m_LootPlace.m_Name, Math.Round(tripMinutes), materials, metal, m_LootRuns, m_GearTier));
			m_LootPlace = null;
		}

		StashLoot();

		//! Park the car at the base (if they actually drove it home)
		if (m_TripCar && m_Base.IsInside(m_TripCar.GetPosition(), 80.0))
			m_Base.m_Vehicle = m_TripCar;
		m_TripCar = null;

		RebalanceGroups();
		SetActivity(AISV_Activity.REST, Math.RandomFloat(m_Settings.RestMinutesMin, m_Settings.RestMinutesMax) * 60.0);
	}

	protected void UpdateGearTier()
	{
		while (m_GearTier + 1 < m_Settings.GearTiers.Count() && m_LootRuns >= m_Settings.GearTiers[m_GearTier + 1].LootRunsRequired)
		{
			m_GearTier++;
			AISurvivorsLog.Debug(GetTribeLabel() + " reached gear tier " + m_Settings.GearTiers[m_GearTier].Name);
		}
	}

	protected static ref TStringArray s_KeepKinds = {"Magazine_Base", "Ammunition_Base", "Edible_Base", "BandageDressing", "Rag", "HuntingKnife", "KitchenKnife", "CombatKnife", "SteakKnife", "StoneKnife", "Canteen", "WaterBottle"};

	protected bool ShouldKeepItem(EntityAI item)
	{
		foreach (string kind: s_KeepKinds)
		{
			if (item.IsKindOf(kind))
				return true;
		}

		return false;
	}

	//! Survivors drop spare stuff (extra guns, tools, clothes) in the base stash - lootable by raiders
	protected void StashLoot()
	{
		EntityAI stash = m_Base.GetStash();
		if (!stash || !m_Squad)
			return;

		for (int i = 0; i < m_Squad.Count(); i++)
		{
			eAIBase ai = eAIBase.Cast(m_Squad.GetMember(i));
			if (!ai || !ai.IsAlive())
				continue;

			array<EntityAI> items = {};
			ai.GetInventory().EnumerateInventory(InventoryTraversalType.PREORDER, items);

			int moved = 0;
			foreach (EntityAI item: items)
			{
				if (moved >= m_Settings.StashItemsPerMember)
					break;

				if (!item || item == ai)
					continue;

				InventoryLocation il = new InventoryLocation();
				item.GetInventory().GetCurrentInventoryLocation(il);
				if (il.GetType() != InventoryLocationType.CARGO)
					continue;

				//! Keep what they need to survive and fight (ammo, food, bandages, knives)
				if (ShouldKeepItem(item))
					continue;

				if (ai.ServerTakeEntityToTargetCargo(stash, item))
					moved++;
			}
		}
	}

	//! =============================================================================================
	//! Vehicles
	//! =============================================================================================

	protected CarScript FindCar(vector near)
	{
		if (!m_Settings.Vehicles.Enabled)
			return null;

		if (m_Base.m_Vehicle && vector.Distance(m_Base.m_Vehicle.GetPosition(), near) < 150.0 && AISurvivorsAutopilot.IsDrivable(m_Base.m_Vehicle))
			return m_Base.m_Vehicle;

		if (m_TripCar && vector.Distance(m_TripCar.GetPosition(), near) < 150.0 && AISurvivorsAutopilot.IsDrivable(m_TripCar))
			return m_TripCar;

		array<Object> objects = {};
		g_Game.GetObjectsAtPosition(near, m_Settings.Vehicles.CommandeerRadius, objects, null);
		foreach (Object obj: objects)
		{
			CarScript car;
			if (Class.CastTo(car, obj) && AISurvivorsAutopilot.IsDrivable(car))
				return car;
		}

		return null;
	}

	protected bool TryStartTrip(vector destination, AISV_Activity followUp)
	{
		if (!m_Settings.Vehicles.Enabled || !m_Squad)
			return false;

		eAIBase leader = GetSquadLeader();
		if (!leader || leader.IsInTransport())
			return false;

		if (vector.Distance(leader.GetPosition(), destination) < m_Settings.Vehicles.MinTripDistance)
			return false;

		CarScript car = FindCar(leader.GetPosition());
		if (!car)
			return false;

		m_TripCar = car;
		m_TripDestination = destination;
		m_TripFollowUp = followUp;
		m_TripBoardRequested = false;
		SetTripPhase(AISV_TripPhase.BOARDING);

		vector entryPos;
		vector entryDir;
		car.CrewEntryWS(DayZPlayerConstants.VEHICLESEAT_DRIVER, entryPos, entryDir);
		SquadMoveTo(entryPos, eAIMovementSpeed.JOG);

		AISurvivorsLog.Debug(GetTribeLabel() + " taking " + car.GetType() + " to " + destination);
		return true;
	}

	protected void SetTripPhase(AISV_TripPhase phase)
	{
		m_TripPhase = phase;
		m_TripPhaseStart = Now();
	}

	protected void AbortTrip(bool continueOnFoot)
	{
		if (m_TripPhase == AISV_TripPhase.NONE)
			return;

		if (m_TripCar && m_TripCar.m_AISV_Autopilot)
			m_TripCar.m_AISV_Autopilot.Stop();

		eAIBase leader = GetSquadLeader();
		if (leader && leader.IsInTransport())
		{
			//! Get out first, then continue
			SetTripPhase(AISV_TripPhase.LEAVING);
			if (!continueOnFoot)
				m_TripFollowUp = AISV_Activity.NONE;
			return;
		}

		m_TripPhase = AISV_TripPhase.NONE;

		if (continueOnFoot)
			ContinueAfterTrip();
	}

	//! After the car trip (or a failed one) carry on with the activity on foot
	protected void ContinueAfterTrip()
	{
		switch (m_TripFollowUp)
		{
			case AISV_Activity.LOOT:
				SquadRoam();
				break;

			case AISV_Activity.RAID:
				if (m_RaidTarget)
					SquadMoveTo(m_RaidTarget.GetPosition(), eAIMovementSpeed.JOG);
				break;

			case AISV_Activity.RETURN:
				SquadMoveTo(m_Base.m_Center, eAIMovementSpeed.JOG);
				break;
		}

		m_TripFollowUp = AISV_Activity.NONE;
	}

	protected void UpdateTrip()
	{
		eAIBase leader = GetSquadLeader();
		if (!leader)
		{
			m_TripPhase = AISV_TripPhase.NONE;
			return;
		}

		float phaseTime = Now() - m_TripPhaseStart;

		if (m_TripPhase == AISV_TripPhase.BOARDING)
			UpdateTripBoarding(leader, phaseTime);
		else if (m_TripPhase == AISV_TripPhase.DRIVING)
			UpdateTripDriving(leader);
		else if (m_TripPhase == AISV_TripPhase.LEAVING)
			UpdateTripLeaving(leader, phaseTime);
	}

	protected void UpdateTripBoarding(eAIBase leader, float phaseTime)
	{
		if (!m_TripCar || (!leader.IsInTransport() && !AISurvivorsAutopilot.IsDrivable(m_TripCar)) || phaseTime > 90.0 || leader.GetThreatToSelf() >= 0.4)
		{
			AbortTrip(true);
			return;
		}

		if (leader.IsInTransport())
		{
			//! Give the others a moment to jump in
			if (AllSquadInCar() || phaseTime > 40.0)
			{
				if (!m_TripCar.m_AISV_Autopilot)
					m_TripCar.m_AISV_Autopilot = new AISurvivorsAutopilot();

				if (m_TripCar.m_AISV_Autopilot.Start(m_TripCar.GetPosition(), m_TripDestination, m_Settings.Vehicles))
					SetTripPhase(AISV_TripPhase.DRIVING);
				else
					AbortTrip(true);
			}
			return;
		}

		vector entryPos;
		vector entryDir;
		m_TripCar.CrewEntryWS(DayZPlayerConstants.VEHICLESEAT_DRIVER, entryPos, entryDir);

		if (!m_TripBoardRequested && vector.Distance(leader.GetPosition(), entryPos) < 2.0 && m_TripCar.IsAreaAtDoorFree(DayZPlayerConstants.VEHICLESEAT_DRIVER) && leader.Expansion_IsAnimationIdle())
		{
			leader.Notify_Transport(m_TripCar, DayZPlayerConstants.VEHICLESEAT_DRIVER);
			m_TripBoardRequested = true;
		}
	}

	protected void UpdateTripDriving(eAIBase leader)
	{
		if (!m_TripCar || !leader.IsInTransport() || !m_TripCar.m_AISV_Autopilot)
		{
			AbortTrip(true);
			return;
		}

		AISurvivorsAutopilot pilot = m_TripCar.m_AISV_Autopilot;
		if (pilot.HasArrived() || pilot.HasFailed() || !pilot.IsActive())
		{
			if (pilot.HasFailed())
				AISurvivorsLog.Debug(GetTribeLabel() + " car got stuck - continuing on foot");

			SetTripPhase(AISV_TripPhase.LEAVING);
		}
	}

	protected void UpdateTripLeaving(eAIBase leader, float phaseTime)
	{
		if (leader.IsInTransport())
		{
			HumanCommandVehicle vehCmd = leader.GetCommand_Vehicle();
			if (vehCmd && !vehCmd.IsGettingOut() && !vehCmd.IsGettingIn() && m_TripCar && m_TripCar.GetSpeedometerAbsolute() < 3.0)
			{
				if (m_TripCar.EngineIsOn())
					m_TripCar.EngineStop();

				vehCmd.GetOutVehicle();
			}

			if (phaseTime > 30.0)
			{
				//! Give up waiting, they'll sort it out
				m_TripPhase = AISV_TripPhase.NONE;
				ContinueAfterTrip();
			}
			return;
		}

		//! Wait a moment for the passengers
		if (phaseTime > 6.0)
		{
			m_TripPhase = AISV_TripPhase.NONE;
			ContinueAfterTrip();
		}
	}

	protected bool AllSquadInCar()
	{
		if (!m_Squad || !m_TripCar)
			return false;

		for (int i = 0; i < m_Squad.Count(); i++)
		{
			DayZPlayerImplement member = m_Squad.GetMember(i);
			if (member && member.GetParent() != m_TripCar)
				return false;
		}

		return true;
	}

	//! =============================================================================================
	//! Recruiting, virtualization, wipe
	//! =============================================================================================

	protected void TryRecruit()
	{
		if (GetAliveCount() >= m_MemberTarget || IsAlarmed() || Now() < m_NextRecruitTime)
			return;

		//! A newcomer walks in from out of sight and joins up
		for (int attempt = 0; attempt < 8; attempt++)
		{
			vector pos = ExpansionStatic.GetSurfacePosition(ExpansionMath.GetRandomPointInRing(m_Base.m_Center, 250.0, 450.0));
			if (AISurvivorsWorld.IsWater(pos))
				continue;

			if (AISurvivorsWorld.DistanceToNearestPlayer(pos) < 250.0)
				continue;

			bool asGuard = m_Squad == null || !IsSquadHome();
			if (m_Squad == null)
				asGuard = false;

			if (SpawnMember(pos, asGuard))
			{
				AISurvivorsLog.Debug(GetTribeLabel() + " recruited a new survivor (" + GetAliveCount() + "/" + m_MemberTarget + ")");
				if (IsSquadHome())
					RebalanceGroups();
			}

			break;
		}

		m_NextRecruitTime = Now() + m_Settings.RecruitIntervalMinutes * 60.0;
	}

	protected void TryVirtualize()
	{
		if (!m_Settings.VirtualizeWhenNoPlayersNearby || IsAlarmed() || m_TripPhase != AISV_TripPhase.NONE)
			return;

		if (m_Activity != AISV_Activity.REST && m_Activity != AISV_Activity.BUILD)
			return;

		foreach (eAIBase ai: m_Members)
		{
			if (!ai)
				continue;

			if (!m_Base.IsInside(ai.GetPosition(), 60.0) || ai.GetThreatToSelf() > 0.2)
				return;
		}

		if (AISurvivorsWorld.DistanceToNearestPlayer(m_Base.m_Center) < m_Settings.VirtualizeDistance)
			return;

		m_VirtualMembers = GetAliveCount();
		foreach (eAIBase member: m_Members)
		{
			if (member)
			{
				member.m_AISV_Tribe = null;
				member.eAI_Despawn();
			}
		}

		m_Members.Clear();
		m_Squad = null;
		m_Guards = null;
		m_State = AISV_TribeState.VIRTUAL;
		m_NextVirtualCycle = Now() + Math.RandomFloat(15, 25) * 60.0;

		AISurvivorsLog.Debug(GetTribeLabel() + " virtualized (" + m_VirtualMembers + " members, no players nearby)");
	}

	protected void TickVirtual()
	{
		//! Someone is coming - bring the tribe back
		if (AISurvivorsWorld.DistanceToNearestPlayer(m_Base.m_Center) < m_Settings.DevirtualizeDistance)
		{
			Devirtualize();
			return;
		}

		//! Off-screen life goes on: loot runs and new recruits
		if (Now() >= m_NextVirtualCycle)
		{
			m_NextVirtualCycle = Now() + Math.RandomFloat(15, 25) * 60.0;

			m_LootRuns++;
			m_Materials += Math.RandomIntInclusive(2, 6);
			if (Math.RandomFloat01() < 0.35)
				m_Metal += Math.RandomIntInclusive(1, 3);
			UpdateGearTier();

			if (m_VirtualMembers < m_MemberTarget && Math.RandomFloat01() < 0.5)
				m_VirtualMembers++;
		}
	}

	void Devirtualize()
	{
		m_State = AISV_TribeState.ACTIVE;
		int count = m_VirtualMembers;
		if (count < 1)
			count = 1;
		SpawnAllAtBase(count);
		m_VirtualMembers = 0;
		AISurvivorsLog.Debug(GetTribeLabel() + " back in the world (" + count + " members)");
	}

	protected void OnWiped()
	{
		AISurvivorsLog.Info(GetTribeLabel() + " has been wiped out. Base left behind at " + m_Base.m_Center);

		AbortTrip(false);
		m_TripPhase = AISV_TripPhase.NONE;
		m_State = AISV_TribeState.WIPED;
		m_WipedAt = Now();
		m_RespawnAt = Now() + m_Settings.TribeRespawnMinutes * 60.0;
		m_Activity = AISV_Activity.NONE;
		m_Squad = null;
		m_Guards = null;
		m_AlarmUntil = 0;
	}

	//! Called by the manager when the respawn timer is up. Returns false if the base is gone (tribe should be retired).
	bool TryRespawn()
	{
		if (!m_Base.IsFlagStanding() && m_Base.GetStructureCount() < 3)
			return false;

		if (AISurvivorsWorld.DistanceToNearestPlayer(m_Base.m_Center) < m_Settings.MinSpawnDistanceFromPlayers)
		{
			m_RespawnAt = Now() + 120.0;
			return true;
		}

		//! The survivors who "regroup" lost their gear
		if (m_GearTier > 0)
			m_GearTier--;

		m_State = AISV_TribeState.ACTIVE;
		m_MemberTarget = Math.RandomIntInclusive(m_Settings.MinMembers, m_Settings.MaxMembers);
		m_Grudges.Clear();
		SpawnAllAtBase(m_MemberTarget);
		m_NextRecruitTime = Now() + m_Settings.RecruitIntervalMinutes * 60.0;

		AISurvivorsLog.Info(GetTribeLabel() + " regrouped at their base with " + m_MemberTarget + " survivors");
		return true;
	}

	void Despawn()
	{
		AbortTrip(false);
		foreach (eAIBase ai: m_Members)
		{
			if (ai)
			{
				ai.m_AISV_Tribe = null;
				ai.eAI_Despawn();
			}
		}

		m_Members.Clear();
		m_Squad = null;
		m_Guards = null;
	}

	//! =============================================================================================
	//! Persistence
	//! =============================================================================================

	AISurvivorsTribeData Save()
	{
		AISurvivorsTribeData data = new AISurvivorsTribeData();
		data.ID = m_ID;
		data.Name = m_Name;
		data.Temperament = m_Temperament;
		data.MemberTarget = m_MemberTarget;
		data.GearTier = m_GearTier;
		data.LootRuns = m_LootRuns;
		data.Materials = m_Materials;
		data.Metal = m_Metal;
		data.Wiped = m_State == AISV_TribeState.WIPED;
		if (data.Wiped)
			data.RespawnMinutesLeft = Math.Max(0, (m_RespawnAt - Now()) / 60.0);

		m_Base.SaveTo(data);
		return data;
	}

	static AISurvivorsTribe Load(AISurvivorsTribeData data, AISurvivorsSettings settings, AISurvivorsWorld world)
	{
		AISurvivorsTribe tribe = new AISurvivorsTribe(data.ID, data.Name, data.Temperament, settings, world);
		tribe.m_MemberTarget = data.MemberTarget;
		tribe.m_GearTier = data.GearTier;
		tribe.m_LootRuns = data.LootRuns;
		tribe.m_Materials = data.Materials;
		tribe.m_Metal = data.Metal;

		tribe.m_Base = new AISurvivorsBase(data.BaseCenter, data.BaseYaw, data.AnchorName, settings);
		tribe.m_Base.AdoptFrom(data);

		if (data.Wiped)
		{
			tribe.m_State = AISV_TribeState.WIPED;
			tribe.m_RespawnAt = Now() + data.RespawnMinutesLeft * 60.0;
			tribe.m_WipedAt = Now();
		}

		return tribe;
	}
}
