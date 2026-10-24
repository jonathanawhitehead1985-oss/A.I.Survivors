/**
 * "Extremely hostile" base defence: while a tribe's base is on alert, anything inside the perimeter
 * is treated as maximum threat by that tribe's survivors - no warnings, no eyeballing, they engage.
 */

class AISurvivorsThreat
{
	static bool IsBaseIntruder(eAIBase ai, EntityAI target)
	{
		if (!ai || !target || target == ai)
			return false;

		AISurvivorsTribe tribe = ai.m_AISV_Tribe;
		if (!tribe || !tribe.m_Base || !tribe.IsAlarmed())
			return false;

		if (!target.IsAlive())
			return false;

		eAIBase otherAI;
		if (Class.CastTo(otherAI, target) && otherAI.m_AISV_Tribe == tribe)
			return false;

		return tribe.m_Base.IsInside(target.GetPosition(), AISurvivorsManager.GetSettings().BaseRadius + 15.0);
	}
}

modded class eAIPlayerTargetInformation
{
	override float CalculateThreat(eAIBase ai = null, eAITargetInformationState state = null)
	{
		float threat = super.CalculateThreat(ai, state);

		if (ai && ai.m_AISV_Tribe && AISurvivorsThreat.IsBaseIntruder(ai, GetEntity()))
			return Math.Max(threat, 1.0);

		return threat;
	}
};

modded class eAIZombieTargetInformation
{
	override float CalculateThreat(eAIBase ai = null, eAITargetInformationState state = null)
	{
		float threat = super.CalculateThreat(ai, state);

		if (ai && ai.m_AISV_Tribe && AISurvivorsThreat.IsBaseIntruder(ai, GetEntity()))
			return Math.Max(threat, 0.8);

		return threat;
	}
};
