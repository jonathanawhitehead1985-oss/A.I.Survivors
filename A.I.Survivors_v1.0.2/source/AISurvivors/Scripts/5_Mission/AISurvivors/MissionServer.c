modded class MissionServer
{
	override void OnInit()
	{
		super.OnInit();

		AISurvivorsManager.Init();
	}

	override void OnMissionFinish()
	{
		AISurvivorsManager.Shutdown();

		super.OnMissionFinish();
	}
};
