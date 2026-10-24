/**
 * Links every survivor to its tribe and reports hits/deaths so the tribe can hold grudges and take revenge.
 */
modded class eAIBase
{
	//! Server only. Weak reference (tribe is Managed, so this nulls itself if the tribe goes away).
	AISurvivorsTribe m_AISV_Tribe;

	override void EEKilled(Object killer)
	{
		AISurvivorsTribe tribe = m_AISV_Tribe;

		super.EEKilled(killer);

		if (tribe && g_Game.IsServer())
			tribe.OnMemberKilled(this, killer);
	}

	override void EEHitBy(TotalDamageResult damageResult, int damageType, EntityAI source, int component, string dmgZone, string ammo, vector modelPos, float speedCoef)
	{
		super.EEHitBy(damageResult, damageType, source, component, dmgZone, ammo, modelPos, speedCoef);

		if (!m_AISV_Tribe || !source || !g_Game.IsServer())
			return;

		PlayerBase attacker;
		if (Class.CastTo(attacker, source.GetHierarchyRoot()) && attacker != this)
			m_AISV_Tribe.OnMemberHit(this, attacker);
	}

	//! While the tribe is out hunting, survivors go after game even if their pockets are full of food
	override bool eAI_ShouldProcureFood()
	{
		if (m_AISV_Tribe && m_AISV_Tribe.IsHunting())
			return true;

		return super.eAI_ShouldProcureFood();
	}
};
