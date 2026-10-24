/**
 * eAIFactionAISurvivors.c
 *
 * One faction INSTANCE per eAIGroup. Every instance points at its tribe, so two groups of the same tribe
 * (home guards + field squad) are allies, while every other tribe is a rival - just like player groups.
 *
 * Hostility rules (server side):
 *  - Anyone inside the base radius while the alarm is up: always an enemy (extremely hostile).
 *  - A player with an active grudge (shot or killed one of us): enemy.
 *  - Otherwise depends on the tribe temperament:
 *      FRIENDLY  -> tolerated, only fights back when attacked
 *      NEUTRAL   -> "guard" behaviour: opens fire when you raise a weapon at them or attack them
 *      HOSTILE   -> kill on sight
 *      BANDIT    -> kill on sight (and goes out hunting players)
 */
[eAIRegisterFaction(eAIFactionAISurvivors)]
class eAIFactionAISurvivors : eAIFaction
{
	protected AISurvivorsTribe m_AISV_Tribe;

	void eAIFactionAISurvivors()
	{
		m_Loadout = "SurvivorLoadout";
	}

	void AISV_SetTribe(AISurvivorsTribe tribe)
	{
		m_AISV_Tribe = tribe;
	}

	AISurvivorsTribe AISV_GetTribe()
	{
		return m_AISV_Tribe;
	}

	override string GetDisplayName()
	{
		if (m_AISV_Tribe)
			return m_AISV_Tribe.m_Name;

		return "Survivors";
	}

	override bool IsGuard()
	{
		if (m_AISV_Tribe)
			return m_AISV_Tribe.AISV_UseGuardLogic();

		return false;
	}

	override bool IsFriendly(notnull eAIFaction other)
	{
		if (other.IsPassive())
			return true;

		eAIFactionAISurvivors otherSurvivors;
		if (Class.CastTo(otherSurvivors, other))
		{
			AISurvivorsTribe otherTribe = otherSurvivors.AISV_GetTribe();

			if (m_AISV_Tribe && otherTribe == m_AISV_Tribe)
				return true;

			//! Client side (no tribe objects) or both tribes calm and the server is set to allow it
			if (!m_AISV_Tribe || !otherTribe)
				return false;

			if (m_AISV_Tribe.IsAlarmed() || otherTribe.IsAlarmed())
				return false;

			return !AISurvivorsManager.GetSettings().TribesHostileToEachOther;
		}

		return false;
	}

	override bool IsFriendlyEntity(EntityAI other, DayZPlayer factionMember = null)
	{
		if (!other || !m_AISV_Tribe)
			return false;

		//! Animals are game, zombies are zombies
		if (other.IsInherited(DayZCreatureAI))
			return false;

		PlayerBase player;
		if (!Class.CastTo(player, other))
			return false;

		//! Other AI are judged by faction (IsFriendly), never by this function
		if (player.IsAI())
			return false;

		return m_AISV_Tribe.AISV_ToleratesPlayer(player);
	}
};
