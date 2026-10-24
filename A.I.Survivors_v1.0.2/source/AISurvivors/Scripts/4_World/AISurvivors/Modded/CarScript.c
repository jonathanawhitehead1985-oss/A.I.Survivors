/**
 * Lets a survivor in the driver seat drive with AISurvivorsAutopilot.
 * Expansion AI also hooks OnInput for AI drivers; we run after it and take over the controls for our survivors,
 * and keep the car parked (brakes on) whenever no trip is active.
 */
modded class CarScript
{
	ref AISurvivorsAutopilot m_AISV_Autopilot;

	override void OnInput(float dt)
	{
		super.OnInput(dt);

		eAIBase driver;
		if (!Class.CastTo(driver, CrewMember(DayZPlayerConstants.VEHICLESEAT_DRIVER)) || !driver.m_AISV_Tribe)
			return;

		if (m_AISV_Autopilot && m_AISV_Autopilot.IsActive())
		{
			m_AISV_Autopilot.Drive(this, dt);
			return;
		}

		SetThrottle(0);
		SetSteering(0);
		SetBrake(1.0);
		if (GetSpeedometerAbsolute() < 3.0)
			SetHandbrake(1.0);
	}
};
