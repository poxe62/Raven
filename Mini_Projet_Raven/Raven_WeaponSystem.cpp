#include "Raven_WeaponSystem.h"
#include "armory/Weapon_RocketLauncher.h"
#include "armory/Weapon_RailGun.h"
#include "armory/Weapon_ShotGun.h"
#include "armory/Weapon_Blaster.h"
#include "Raven_Bot.h"
#include "misc/utils.h"
#include "lua/Raven_Scriptor.h"
#include "Raven_Game.h"
#include "Raven_UserOptions.h"
#include "2D/transformations.h"
#include "fuzzy/FuzzyOperators.h"

using namespace std;
//------------------------- ctor ----------------------------------------------
//-----------------------------------------------------------------------------
Raven_WeaponSystem::Raven_WeaponSystem(Raven_Bot* owner,
									   double ReactionTime,
									   double AimAccuracy,
									   double AimPersistance):m_pOwner(owner),
									   m_dReactionTime(ReactionTime),
									   m_dAimAccuracy(AimAccuracy),
									   m_dAimPersistance(AimPersistance)
{
	Initialize();

	//setup the fuzzy module
	InitializeFuzzyModule();
}

//------------------------- dtor ----------------------------------------------
//-----------------------------------------------------------------------------
Raven_WeaponSystem::~Raven_WeaponSystem()
{
	for (unsigned int w=0; w<m_WeaponMap.size(); ++w)
	{
		delete m_WeaponMap[w];
	}
}

//------------------------------ Initialize -----------------------------------
//
//  initializes the weapons
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::Initialize()
{
	//delete any existing weapons
	WeaponMap::iterator curW;
	for (curW = m_WeaponMap.begin(); curW != m_WeaponMap.end(); ++curW)
	{
		delete curW->second;
	}

	m_WeaponMap.clear();

	//set up the container
	m_pCurrentWeapon = new Blaster(m_pOwner);

	m_WeaponMap[type_blaster]         = m_pCurrentWeapon;
	m_WeaponMap[type_shotgun]         = 0;
	m_WeaponMap[type_rail_gun]        = 0;
	m_WeaponMap[type_rocket_launcher] = 0;
}

//-------------------------------- SelectWeapon -------------------------------
//
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::SelectWeapon()
{ 
	//if a target is present use fuzzy logic to determine the most desirable 
	//weapon.
	if (m_pOwner->GetTargetSys()->isTargetPresent())
	{
		//calculate the distance to the target
		double DistToTarget = Vec2DDistance(m_pOwner->Pos(), m_pOwner->GetTargetSys()->GetTarget()->Pos());

		//for each weapon in the inventory calculate its desirability given the 
		//current situation. The most desirable weapon is selected
		double BestSoFar = MinDouble;

		WeaponMap::const_iterator curWeap;
		for (curWeap=m_WeaponMap.begin(); curWeap != m_WeaponMap.end(); ++curWeap)
		{
			//grab the desirability of this weapon (desirability is based upon
			//distance to target and ammo remaining)
			if (curWeap->second)
			{
				double score = curWeap->second->GetDesirability(DistToTarget);

				//if it is the most desirable so far select it
				if (score > BestSoFar)
				{
					BestSoFar = score;

					//place the weapon in the bot's hand.
					m_pCurrentWeapon = curWeap->second;
				}
			}
		}
	}

	else
	{
		m_pCurrentWeapon = m_WeaponMap[type_blaster];
	}
}

//--------------------  AddWeapon ------------------------------------------
//
//  this is called by a weapon affector and will add a weapon of the specified
//  type to the bot's inventory.
//
//  if the bot already has a weapon of this type then only the ammo is added
//-----------------------------------------------------------------------------
void  Raven_WeaponSystem::AddWeapon(unsigned int weapon_type)
{
	//create an instance of this weapon
	Raven_Weapon* w = 0;

	switch(weapon_type)
	{
	case type_rail_gun:

		w = new RailGun(m_pOwner); break;

	case type_shotgun:

		w = new ShotGun(m_pOwner); break;

	case type_rocket_launcher:

		w = new RocketLauncher(m_pOwner); break;

	}//end switch


	//if the bot already holds a weapon of this type, just add its ammo
	Raven_Weapon* present = GetWeaponFromInventory(weapon_type);

	if (present)
	{
		present->IncrementRounds(w->NumRoundsRemaining());

		delete w;
	}

	//if not already holding, add to inventory
	else
	{
		m_WeaponMap[weapon_type] = w;
	}
}


//------------------------- GetWeaponFromInventory -------------------------------
//
//  returns a pointer to any matching weapon.
//
//  returns a null pointer if the weapon is not present
//-----------------------------------------------------------------------------
Raven_Weapon* Raven_WeaponSystem::GetWeaponFromInventory(int weapon_type)
{
	return m_WeaponMap[weapon_type];
}

//----------------------- ChangeWeapon ----------------------------------------
void Raven_WeaponSystem::ChangeWeapon(unsigned int type)
{
	Raven_Weapon* w = GetWeaponFromInventory(type);

	if (w) m_pCurrentWeapon = w;
}

//--------------------------- TakeAimAndShoot ---------------------------------
//
//  this method aims the bots current weapon at the target (if there is a
//  target) and, if aimed correctly, fires a round
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::TakeAimAndShoot()
{
	//aim the weapon only if the current target is shootable or if it has only
	//very recently gone out of view (this latter condition is to ensure the 
	//weapon is aimed at the target even if it temporarily dodges behind a wall
	//or other cover)
	if (m_pOwner->GetTargetSys()->isTargetShootable() ||
		(m_pOwner->GetTargetSys()->GetTimeTargetHasBeenOutOfView() < 
		m_dAimPersistance) )
	{
		//the position the weapon will be aimed at
		Vector2D AimingPos = m_pOwner->GetTargetBot()->Pos();

		//if the current weapon is not an instant hit type gun the target position
		//must be adjusted to take into account the predicted movement of the 
		//target
		if (GetCurrentWeapon()->GetType() == type_rocket_launcher ||
			GetCurrentWeapon()->GetType() == type_blaster)
		{
			AimingPos = PredictFuturePositionOfTarget();

			//if the weapon is aimed correctly, there is line of sight between the
			//bot and the aiming position and it has been in view for a period longer ///
			//than the bot's reaction time, shoot the weapon
			if ( m_pOwner->RotateFacingTowardPosition(AimingPos) &&
				(m_pOwner->GetTargetSys()->GetTimeTargetHasBeenVisible() >
				m_dReactionTime) &&
				m_pOwner->hasLOSto(AimingPos) )
			{
				AddNoiseToAim(AimingPos);

				GetCurrentWeapon()->ShootAt(AimingPos);
			}
		}

		//no need to predict movement, aim directly at target
		else
		{
			//if the weapon is aimed correctly and it has been in view for a period
			//longer than the bot's reaction time, shoot the weapon
			if ( m_pOwner->RotateFacingTowardPosition(AimingPos) &&
				(m_pOwner->GetTargetSys()->GetTimeTargetHasBeenVisible() >
				m_dReactionTime) )
			{
				AddNoiseToAim(AimingPos);

				GetCurrentWeapon()->ShootAt(AimingPos);
			}
		}

	}

	//no target to shoot at so rotate facing to be parallel with the bot's
	//heading direction
	else
	{
		m_pOwner->RotateFacingTowardPosition(m_pOwner->Pos()+ m_pOwner->Heading());
	}
}

//---------------------------- AddNoiseToAim ----------------------------------
//
//  adds a random deviation to the firing angle not greater than m_dAimAccuracy ////////*/////////
//  rads
//-----------------------------------------------------------------------------

double Raven_WeaponSystem::AddFuzzyfiVar(Vector2D& AimingPos)
{

	Vector2D toPos = AimingPos - m_pOwner->Pos();
	int visible = m_pOwner->GetTargetSys()->GetTimeTargetHasBeenVisible() - m_dReactionTime;


	//fuzzyfy speed, pos, time while target is visible
	rand_FuzzyModule.Fuzzify("speed", m_pOwner->GetTargetBot()->MaxSpeed());
	rand_FuzzyModule.Fuzzify("ToPos", toPos.Length());
	rand_FuzzyModule.Fuzzify("targetVisible", visible);

	return rand_FuzzyModule.DeFuzzify("Aim", FuzzyModule::max_av);
}

void Raven_WeaponSystem::AddNoiseToAim(Vector2D& AimingPos)
{
	Vector2D toPos = AimingPos - m_pOwner->Pos();

	Vec2DRotateAroundOrigin(toPos, RandInRange(-m_dAimAccuracy, m_dAimAccuracy));

	AddFuzzyfiVar(AimingPos);
}

void Raven_WeaponSystem::InitializeFuzzyModule(){

	FuzzyVariable& speed = rand_FuzzyModule.CreateFLV("speed");
	FzSet& Speed_Hight = speed.AddLeftShoulderSet("Speed_Hight", 0.6, 0.8, 1);
	FzSet& Speed_Medium = speed.AddLeftShoulderSet("Speed_Medium", 0.3, 0.5, 0.7);
	FzSet& Speed_Low = speed.AddLeftShoulderSet("Speed_Low", 0, 0.2, 0.4);

	FuzzyVariable& ToPos = rand_FuzzyModule.CreateFLV("ToPos");
	FzSet& ToPos_Far = ToPos.AddLeftShoulderSet("ToPos_Far", 300, 500, 2000);
	FzSet& ToPos_Medium = ToPos.AddLeftShoulderSet("ToPos_Medium", 100, 250, 400);
	FzSet& ToPos_Close = ToPos.AddLeftShoulderSet("ToPos_Close", 0, 75, 150);

	FuzzyVariable& targetVisible = rand_FuzzyModule.CreateFLV("targetVisible");
	FzSet& Target_Visible = targetVisible.AddLeftShoulderSet("Target_Visible", 0.6, 1, 200);
	FzSet& Target_Medium_Visible = targetVisible.AddLeftShoulderSet("Target_Medium_Visible", 0.3, 0.5, 1);
	FzSet& Target_Not_Visible = targetVisible.AddLeftShoulderSet("Target_Not_Visible", -1, 0.2, 0.5);

	FuzzyVariable& Aim = rand_FuzzyModule.CreateFLV("Aim");
	FzSet& Aim_Good = Aim.AddLeftShoulderSet("Aim_Good", 0.7, 0.85, 1);
	FzSet& Aim_Medium_Good = Aim.AddLeftShoulderSet("Aim_Medium_Good", 0.45, 0.65, 0.75);
	FzSet& Aim_Medium_Bad = Aim.AddLeftShoulderSet("Aim_Medium_Bad", 0.20, 0.35, 0.50);
	FzSet& Aim_Bad = Aim.AddLeftShoulderSet("Aim_Bad", 0, 0.15, 0.25);

	rand_FuzzyModule.AddRule(FzAND(Speed_Hight, ToPos_Far, Target_Visible), Aim_Medium_Bad);
	rand_FuzzyModule.AddRule(FzAND(Speed_Hight, ToPos_Medium, Target_Visible), Aim_Medium_Good);
	rand_FuzzyModule.AddRule(FzAND(Speed_Hight, ToPos_Close, Target_Visible), Aim_Medium_Good);

	rand_FuzzyModule.AddRule(FzAND(Speed_Medium, ToPos_Far, Target_Visible), Aim_Medium_Bad);
	rand_FuzzyModule.AddRule(FzAND(Speed_Medium, ToPos_Medium, Target_Visible), Aim_Medium_Good);
	rand_FuzzyModule.AddRule(FzAND(Speed_Medium, ToPos_Close, Target_Visible), Aim_Good);

	rand_FuzzyModule.AddRule(FzAND(Speed_Low, ToPos_Far, Target_Visible), Aim_Medium_Good);
	rand_FuzzyModule.AddRule(FzAND(Speed_Low, ToPos_Medium, Target_Visible), Aim_Good);
	rand_FuzzyModule.AddRule(FzAND(Speed_Low, ToPos_Close, Target_Visible), Aim_Good);

	rand_FuzzyModule.AddRule(FzAND(Speed_Hight, ToPos_Far, Target_Medium_Visible), Aim_Bad);
	rand_FuzzyModule.AddRule(FzAND(Speed_Hight, ToPos_Medium, Target_Medium_Visible), Aim_Medium_Bad);
	rand_FuzzyModule.AddRule(FzAND(Speed_Hight, ToPos_Close, Target_Medium_Visible), Aim_Medium_Bad);

	rand_FuzzyModule.AddRule(FzAND(Speed_Medium, ToPos_Far, Target_Medium_Visible), Aim_Medium_Bad);
	rand_FuzzyModule.AddRule(FzAND(Speed_Medium, ToPos_Medium, Target_Medium_Visible), Aim_Medium_Good);
	rand_FuzzyModule.AddRule(FzAND(Speed_Medium, ToPos_Close, Target_Medium_Visible), Aim_Medium_Good);

	rand_FuzzyModule.AddRule(FzAND(Speed_Low, ToPos_Far, Target_Medium_Visible), Aim_Medium_Good);
	rand_FuzzyModule.AddRule(FzAND(Speed_Low, ToPos_Medium, Target_Medium_Visible), Aim_Medium_Good);
	rand_FuzzyModule.AddRule(FzAND(Speed_Low, ToPos_Close, Target_Medium_Visible), Aim_Medium_Good);

	rand_FuzzyModule.AddRule(FzAND(Speed_Hight, ToPos_Far, Target_Not_Visible), Aim_Bad);
	rand_FuzzyModule.AddRule(FzAND(Speed_Hight, ToPos_Medium, Target_Not_Visible), Aim_Bad);
	rand_FuzzyModule.AddRule(FzAND(Speed_Hight, ToPos_Close, Target_Not_Visible), Aim_Bad);

	rand_FuzzyModule.AddRule(FzAND(Speed_Medium, ToPos_Far, Target_Not_Visible), Aim_Bad);
	rand_FuzzyModule.AddRule(FzAND(Speed_Medium, ToPos_Medium, Target_Not_Visible), Aim_Bad);
	rand_FuzzyModule.AddRule(FzAND(Speed_Medium, ToPos_Close, Target_Not_Visible), Aim_Medium_Bad);

	rand_FuzzyModule.AddRule(FzAND(Speed_Low, ToPos_Far, Target_Not_Visible), Aim_Bad);
	rand_FuzzyModule.AddRule(FzAND(Speed_Low, ToPos_Medium, Target_Not_Visible), Aim_Medium_Bad);
	rand_FuzzyModule.AddRule(FzAND(Speed_Low, ToPos_Close, Target_Not_Visible), Aim_Medium_Bad);
}

//-------------------------- PredictFuturePositionOfTarget --------------------
//
//  predicts where the target will be located in the time it takes for a
//  projectile to reach it. This uses a similar logic to the Pursuit steering
//  behavior.
//-----------------------------------------------------------------------------
Vector2D Raven_WeaponSystem::PredictFuturePositionOfTarget()const
{
	double MaxSpeed = GetCurrentWeapon()->GetMaxProjectileSpeed();

	//if the target is ahead and facing the agent shoot at its current pos
	Vector2D ToEnemy = m_pOwner->GetTargetBot()->Pos() - m_pOwner->Pos();

	//the lookahead time is proportional to the distance between the enemy
	//and the pursuer; and is inversely proportional to the sum of the
	//agent's velocities
	double LookAheadTime = ToEnemy.Length() / 
		(MaxSpeed + m_pOwner->GetTargetBot()->MaxSpeed());

	//return the predicted future position of the enemy
	return m_pOwner->GetTargetBot()->Pos() + 
		m_pOwner->GetTargetBot()->Velocity() * LookAheadTime;
}


//------------------ GetAmmoRemainingForWeapon --------------------------------
//
//  returns the amount of ammo remaining for the specified weapon. Return zero
//  if the weapon is not present
//-----------------------------------------------------------------------------
int Raven_WeaponSystem::GetAmmoRemainingForWeapon(unsigned int weapon_type)
{
	if (m_WeaponMap[weapon_type])
	{
		return m_WeaponMap[weapon_type]->NumRoundsRemaining();
	}

	return 0;
}

//---------------------------- ShootAt ----------------------------------------
//
//  shoots the current weapon at the given position
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::ShootAt(Vector2D pos)const
{
	GetCurrentWeapon()->ShootAt(pos);
}

//-------------------------- RenderCurrentWeapon ------------------------------
//-----------------------------------------------------------------------------
void Raven_WeaponSystem::RenderCurrentWeapon()const
{
	GetCurrentWeapon()->Render();
}

void Raven_WeaponSystem::RenderDesirabilities()const
{
	Vector2D p = m_pOwner->Pos();

	int num = 0;

	WeaponMap::const_iterator curWeap;
	for (curWeap=m_WeaponMap.begin(); curWeap != m_WeaponMap.end(); ++curWeap)
	{
		if (curWeap->second) num++;
	}

	int offset = 15 * num;

	for (curWeap=m_WeaponMap.begin(); curWeap != m_WeaponMap.end(); ++curWeap)
	{
		if (curWeap->second)
		{
			double score = curWeap->second->GetLastDesirabilityScore();
			std::string type = GetNameOfType(curWeap->second->GetType());

			gdi->TextAtPos(p.x+10.0, p.y-offset, ttos(score) + " " + type);

			offset+=15;
		}
	}
}
