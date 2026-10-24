/**
 * AISurvivorsAutopilot.c
 *
 * Simple driving for a survivor sitting in the driver seat. Expansion AI can seat followers in their leader's car,
 * but an AI leader does not path while seated, so this plans a navmesh route (preferring roads) and steers along it.
 * Experimental - toggle with Vehicles.Enabled in the settings.
 */

class AISurvivorsAutopilot : Managed
{
	protected ref array<vector> m_Path = {};
	protected int m_Index;
	protected vector m_Destination;
	protected float m_StopDistance = 60.0;
	protected float m_CruiseKmh = 50.0;

	protected bool m_Active;
	protected bool m_Arrived;
	protected bool m_Failed;

	protected vector m_LastProgressPos;
	protected float m_LastProgressTime;
	protected float m_StuckDistance = 12.0;
	protected float m_StuckSeconds = 25.0;

	protected static ref PGFilter s_Filter;

	static PGFilter GetFilter()
	{
		if (!s_Filter)
		{
			s_Filter = new PGFilter();
			int include = PGPolyFlags.WALK | PGPolyFlags.DOOR;
			int exclude = PGPolyFlags.SWIM | PGPolyFlags.SWIM_SEA | PGPolyFlags.INSIDE | PGPolyFlags.LADDER | PGPolyFlags.CRAWL | PGPolyFlags.CROUCH | PGPolyFlags.DISABLED | PGPolyFlags.SPECIAL;
			s_Filter.SetFlags(include, exclude, PGPolyFlags.NONE);
			s_Filter.SetCost(PGAreaType.ROADWAY, 1.0);
			s_Filter.SetCost(PGAreaType.TERRAIN, 2.5);
			s_Filter.SetCost(PGAreaType.OBJECTS, 25.0);
			s_Filter.SetCost(PGAreaType.OBJECTS_NOFFCON, 25.0);
			s_Filter.SetCost(PGAreaType.TREE, 25.0);
			s_Filter.SetCost(PGAreaType.BUILDING, 50.0);
			s_Filter.SetCost(PGAreaType.ROADWAY_BUILDING, 50.0);
			s_Filter.SetCost(PGAreaType.FENCE_WALL, 50.0);
		}

		return s_Filter;
	}

	//! Plans the route in ~500 m chunks so the navmesh query stays small.
	bool Start(vector from, vector to, AISurvivorsVehicleSettings settings)
	{
		m_Path.Clear();
		m_Index = 0;
		m_Destination = to;
		m_Arrived = false;
		m_Failed = false;
		m_CruiseKmh = settings.CruiseSpeedKmh;
		m_StuckDistance = settings.StuckDistance;
		m_StuckSeconds = settings.StuckSeconds;

		AIWorld aiWorld = g_Game.GetWorld().GetAIWorld();
		if (!aiWorld)
			return false;

		vector segmentStart = from;
		float total = vector.Distance(from, to);
		int chunks = Math.Ceil(total / 500.0);
		if (chunks < 1)
			chunks = 1;

		for (int i = 1; i <= chunks; i++)
		{
			float t = i;
			t = t / chunks;
			vector segmentEnd = ExpansionStatic.GetSurfacePosition(from + (to - from) * t);

			TVectorArray chunkPath = {};
			if (aiWorld.FindPath(segmentStart, segmentEnd, GetFilter(), chunkPath) && chunkPath.Count() > 0)
			{
				foreach (vector p: chunkPath)
					m_Path.Insert(p);
				segmentStart = chunkPath[chunkPath.Count() - 1];
			}
			else
			{
				//! No navmesh route for this chunk - drive straight
				m_Path.Insert(segmentEnd);
				segmentStart = segmentEnd;
			}
		}

		Simplify();

		m_Active = m_Path.Count() > 0;
		m_LastProgressPos = from;
		m_LastProgressTime = g_Game.GetTickTime();

		return m_Active;
	}

	//! Drop points that are too close together for a car
	protected void Simplify()
	{
		array<vector> simplified = {};
		vector last;
		foreach (int idx, vector p: m_Path)
		{
			if (idx == 0 || vector.DistanceSq(last, p) > 64.0 || idx == m_Path.Count() - 1)
			{
				simplified.Insert(p);
				last = p;
			}
		}
		m_Path = simplified;
	}

	void Stop()
	{
		m_Active = false;
	}

	bool IsActive()
	{
		return m_Active;
	}

	bool HasArrived()
	{
		return m_Arrived;
	}

	bool HasFailed()
	{
		return m_Failed;
	}

	vector GetDestination()
	{
		return m_Destination;
	}

	//! Called every simulation frame from CarScript.OnInput while active
	void Drive(CarScript car, float dt)
	{
		if (!m_Active || !car)
			return;

		vector pos = car.GetPosition();
		float speed = car.GetSpeedometerAbsolute();

		//! Arrived?
		if (vector.Distance(pos, m_Destination) <= m_StopDistance || m_Index >= m_Path.Count())
		{
			Brake(car, speed);
			if (speed < 2.0)
			{
				m_Arrived = true;
				m_Active = false;
			}
			return;
		}

		//! Stuck?
		float now = g_Game.GetTickTime();
		if (vector.Distance(pos, m_LastProgressPos) > m_StuckDistance)
		{
			m_LastProgressPos = pos;
			m_LastProgressTime = now;
		}
		else if (now - m_LastProgressTime > m_StuckSeconds)
		{
			Brake(car, speed);
			m_Failed = true;
			m_Active = false;
			return;
		}

		//! Advance along the path
		while (m_Index < m_Path.Count() - 1)
		{
			vector wp = m_Path[m_Index];
			if (vector.DistanceSq(Vector(pos[0], 0, pos[2]), Vector(wp[0], 0, wp[2])) < 100.0)
				m_Index++;
			else
				break;
		}

		vector target = m_Path[m_Index];

		//! Look a little further ahead on straight sections
		if (m_Index + 1 < m_Path.Count() && vector.Distance(pos, target) < 20.0)
			target = m_Path[m_Index + 1];

		if (!car.EngineIsOn())
		{
			car.EngineStart();
			return;
		}

		float steering = Math3D.AngleFromPosition(pos, car.GetDirection(), target) / (Math.PI * 0.35);
		steering = Math.Clamp(steering, -1.0, 1.0);

		vector toTarget = vector.Direction(pos, target);
		toTarget[1] = 0;
		toTarget.Normalize();
		float facing = vector.Dot(car.GetDirection(), toTarget);

		float desiredSpeed = m_CruiseKmh * Math.Clamp(1.0 - Math.AbsFloat(steering) * 0.6, 0.3, 1.0);
		float distanceLeft = vector.Distance(pos, m_Destination);
		if (distanceLeft < 150.0)
			desiredSpeed = Math.Min(desiredSpeed, 20.0);

		float throttle = 0.0;
		float brake = 0.0;

		if (facing < -0.2 && speed < 8.0)
		{
			//! Target is behind us - three point turn
			ShiftReverse(car);
			throttle = 0.4;
			steering = -steering;
		}
		else
		{
			ShiftForward(car);

			if (speed < desiredSpeed)
				throttle = Math.Clamp((desiredSpeed - speed) / 15.0, 0.25, 0.85);
			else if (speed > desiredSpeed + 8.0)
				brake = 0.5;
		}

		car.SetSteering(steering);
		car.SetThrottle(throttle);
		car.SetBrake(brake);
		car.SetHandbrake(0);
	}

	protected void Brake(CarScript car, float speed)
	{
		car.SetThrottle(0);
		car.SetSteering(0);
		car.SetBrake(1.0);
		if (speed < 3.0)
			car.SetHandbrake(1.0);
	}

	protected void ShiftForward(CarScript car)
	{
		if (car.GearboxGetType() == CarGearboxType.AUTOMATIC)
		{
			if (car.GearboxGetMode() != CarAutomaticGearboxMode.D)
				car.ShiftTo(CarAutomaticGearboxMode.D);
			return;
		}

		int gear = car.GetGear();
		if (gear < CarGear.FIRST)
		{
			car.ShiftTo(CarGear.FIRST);
			return;
		}

		float rpm = car.EngineGetRPM();
		float redline = car.EngineGetRPMRedline();
		if (rpm > redline * 0.85 && gear < car.GetGearCount() - 1)
			car.ShiftUp();
		else if (rpm < redline * 0.35 && gear > CarGear.FIRST)
			car.ShiftDown();
	}

	protected void ShiftReverse(CarScript car)
	{
		if (car.GearboxGetType() == CarGearboxType.AUTOMATIC)
		{
			if (car.GearboxGetMode() != CarAutomaticGearboxMode.R)
				car.ShiftTo(CarAutomaticGearboxMode.R);
			return;
		}

		if (car.GetGear() != CarGear.REVERSE)
			car.ShiftTo(CarGear.REVERSE);
	}

	//! True if a car is in a state a survivor would drive it
	static bool IsDrivable(CarScript car)
	{
		if (!car || car.IsRuined() || car.IsDamageDestroyed())
			return false;

		for (int i = 0; i < car.CrewSize(); i++)
		{
			if (car.CrewMember(i))
				return false;
		}

		if (car.GetFluidFraction(CarFluid.FUEL) <= 0.05)
			return false;

		//! Vanilla 1.30 check: engine, fuel, battery and spark/glow plug
		if (car.CheckOperationalRequirements() != ECarOperationalState.OK)
			return false;

		int wheels;
		for (int w = 0; w < car.WheelCount(); w++)
		{
			EntityAI wheel = car.WheelGetEntity(w);
			if (wheel && !wheel.IsRuined())
				wheels++;
		}

		if (wheels < car.WheelCount())
			return false;

		//! Expansion vehicle locks (key system) - survivors don't steal locked cars
		ExpansionVehicle ev;
		if (ExpansionVehicle.Get(ev, car) && ev.IsLocked())
			return false;

		return true;
	}
}
