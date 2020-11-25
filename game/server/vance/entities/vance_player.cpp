//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:        Player for Vance.
//
//=============================================================================//

#include "cbase.h"
#include "hl2_player.h"
#include "ai_speech.h"
#include "ai_playerally.h"
#include "entities/vance_viewmodel.h"
#include "anim/singleplayer_animstate.h"
#include "datacache/imdlcache.h"
#include "in_buttons.h"
#include "trains.h"
#include "weapon_physcannon.h"
#include "gamestats.h"
#include "rumble_shared.h"
#include "npc_barnacle.h"
#include "globalstate.h"
#include "entities/vance_player.h"
#include "vance_shareddefs.h"
#include "iservervehicle.h"
#include "vehicle_viewblend_shared.h"
#include "in_buttons.h"
#include "vehicle_viewblend_shared.h"
#include "gamemovement.h"
#include "in_buttons.h"
#include <stdarg.h>
#include "movevars_shared.h"
#include "engine/IEngineTrace.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "decals.h"
#include "coordsize.h"
#include "rumble_shared.h"
#include "interpolatortypes.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"
#include <ammodef.h>

/*
	TODO: Make medkits 'fade out' bleeding by slowing down and weakening damage
	TODO: Improve bleeding code, it should be a damage type
	TODO: Implement tourniquets
*/

extern ConVar player_showpredictedposition;
extern ConVar player_showpredictedposition_timestep;

extern ConVar sv_autojump;
extern ConVar sv_stickysprint;
extern ConVar sv_infinite_aux_power;

extern ConVar hl2_walkspeed;
extern ConVar hl2_normspeed;
extern ConVar hl2_sprintspeed;

extern int gEvilImpulse101;

ConVar sk_max_tourniquets("sk_max_tourniquets", "3", FCVAR_CHEAT);

ConVar sk_bleed_chance("sk_bleed_chance", "5", FCVAR_CHEAT, "The chance (in percentage) that the Player will bleed upon taking damage.");
ConVar sk_bleed_chance_increment("sk_bleed_chance_increment", "0.5", FCVAR_CHEAT, "The amount of chance the Player will bleed will increment by when shot.");
ConVar sk_bleed_chance_decay_start_time("sk_bleed_chance_decay_start_time", "10", FCVAR_CHEAT, "How long after being shot the bleed chance will start to decay.");
ConVar sk_bleed_chance_decay_rate("sk_bleed_chance_decay_rate", "0.5", FCVAR_CHEAT);

ConVar sk_bleed_lifetime("sk_stim_regen_lifetime", "5", FCVAR_CHEAT);
ConVar sk_bleed_dmg_per_interval("sk_bleed_dmg_per_interval", "5", FCVAR_CHEAT);
ConVar sk_bleed_dmg_interval("sk_bleed_dmg_interval", "1", FCVAR_CHEAT);

ConVar sk_stim_regen_lifetime("sk_stim_regen_lifetime", "5", FCVAR_CHEAT);
ConVar sk_stim_health_per_interval("sk_stim_health_per_sec", "5", FCVAR_CHEAT);
ConVar sk_stim_heal_interval("sk_stim_heal_interval", "1", FCVAR_CHEAT);

#define	VANCE_WALK_SPEED hl2_normspeed.GetFloat()
#define	VANCE_SPRINT_SPEED hl2_sprintspeed.GetFloat()

ConVar hl2_runspeed("hl2_runspeed", "240");
#define HL2_RUN_SPEED hl2_runspeed.GetFloat()

#define	FLASH_DRAIN_TIME	 1.1111	// 100 units / 90 secs
#define	FLASH_CHARGE_TIME	 50.0f	// 100 units / 2 secs

#define SUITPOWER_CHARGE_RATE	12.5	// 100 units in 8 seconds

// Divide everything by half for now.
#define	FLASH_DRAIN_TIME_SUITLESS	2.2222f		// 100 units / 45 secs
#define	FLASH_CHARGE_TIME_SUITLESS	25.0f		// 100 units / 4 secs

ConVar vance_climb_speed("vance_climb_speed", "0.025", FCVAR_CHEAT);
ConVar vance_climb_checkray_count("vance_climb_checkray_count", "5", FCVAR_CHEAT);
ConVar vance_climb_checkray_dist("vance_climb_checkray_dist", "64", FCVAR_CHEAT);
ConVar vance_climb_debug("vance_climb_debug", "0");
#define CLIMB_TRACE_DIST		(vance_climb_checkray_dist.GetFloat())
#define CLIMB_LERPSPEED			vance_climb_speed.GetFloat();

ConVar vance_kick_time_adjust("kick_time_adjust", "0.1", FCVAR_CHEAT);
ConVar vance_kick_range("kick_range", "100", FCVAR_CHEAT);
ConVar vance_kick_firerate("kick_firerate", "1", FCVAR_CHEAT);

ConVar vance_kick_damage_mult_min("kick_damage_mult_min", "1", FCVAR_CHEAT);
ConVar vance_kick_damage_mult_max("kick_damage_mult_max", "1", FCVAR_CHEAT);
ConVar vance_kick_force_mult_min("kick_force_mult_min", "1", FCVAR_CHEAT);
ConVar vance_kick_force_mult_max("kick_force_mult_max", "1", FCVAR_CHEAT);

#define BLUDGEON_HULL_DIM		16
static const Vector g_bludgeonMins(-BLUDGEON_HULL_DIM, -BLUDGEON_HULL_DIM, -BLUDGEON_HULL_DIM);
static const Vector g_bludgeonMaxs(BLUDGEON_HULL_DIM, BLUDGEON_HULL_DIM, BLUDGEON_HULL_DIM);

LINK_ENTITY_TO_CLASS( player, CVancePlayer );
PRECACHE_REGISTER( player );

BEGIN_DATADESC( CVancePlayer )
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CVancePlayer, DT_Vance_Player )
	SendPropFloat(SENDINFO(m_flKickAnimLength)),
END_SEND_TABLE()

static const char *s_pScriptLockContext = "ScriptLockContext";

static void Cmd_UseTourniquet()
{
	CVancePlayer *pPlayer = (CVancePlayer *)UTIL_GetCommandClient();
	if (pPlayer)
		pPlayer->UseTourniquet();
}
ConCommand use_tourniquet("use_tourniquet", Cmd_UseTourniquet, "The Player uses an available tourniquet to stop the effects of bleeding.", FCVAR_CLIENTCMD_CAN_EXECUTE);

static void Cmd_InjectStim()
{
	CVancePlayer *pPlayer = (CVancePlayer *)UTIL_GetCommandClient();
	if (pPlayer)
		pPlayer->InjectStim();
}
ConCommand inject_stim("inject_stim", Cmd_InjectStim, "The Player injects an available stimulant to regenerate health.", FCVAR_CLIENTCMD_CAN_EXECUTE);

CVancePlayer::CVancePlayer()
{
	// Here we create and init the player animation state.
	m_pPlayerAnimState = CreatePlayerAnimationState(this);
	m_angEyeAngles.Init();

	// Code originally written by Valve.
	m_nNumMissPositions = 0;
	m_pPlayerAISquad = 0;
	m_bSprintEnabled = true;

	m_flNextSprint = 0.0f;

	m_flBleedChance = sk_bleed_chance.GetFloat();
}

CVancePlayer::~CVancePlayer()
{
	// Clears the animation state.
	if (m_pPlayerAnimState != NULL)
	{
		m_pPlayerAnimState->Release();
		m_pPlayerAnimState = NULL;
	}
}

void CVancePlayer::Precache()
{
	BaseClass::Precache();

	PrecacheModel(P_PLAYER_ALYX);
	PrecacheModel(P_PLAYER_HEV);

	PrecacheModel(C_ARMS_ALYX);
	PrecacheModel(C_ARMS_HEV);

	PrecacheModel(V_KICK_ALYX); //SMOD KICK STUFF!
	PrecacheModel(V_KICK_HEV);

	PrecacheModel("models/weapons/v_stim.mdl");

	PrecacheScriptSound("HL2Player.KickHit");
	PrecacheScriptSound("HL2Player.KickMiss");

	PrecacheScriptSound("HL2Player.AmmoPickup_Suitless");
	PrecacheScriptSound("HL2Player.PickupWeapon_Suitless");
	PrecacheScriptSound("HL2Player.FlashLightOn_Suitless");
	PrecacheScriptSound("HL2Player.FlashLightOff_Suitless");

	PrecacheScriptSound("AlyxPlayer.PainHeavy");

	PrecacheScriptSound("AlyxPlayer.SprintPain");
	PrecacheScriptSound("AlyxPlayer.Sprint");

	PrecacheScriptSound("AlyxPlayer.Jump");
	PrecacheScriptSound("AlyxPlayer.JumpGear");
	PrecacheScriptSound("AlyxPlayer.Land");

	PrecacheScriptSound("AlyxPlayer.Die");
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : iImpulse - 
//-----------------------------------------------------------------------------
void CVancePlayer::CheatImpulseCommands(int iImpulse)
{
	switch (iImpulse)
	{
	case 101:
		gEvilImpulse101 = true;

		EquipSuit(false);

		// Give the player everything!
		CBaseCombatCharacter::GiveAmmo(255, "Pistol");
		CBaseCombatCharacter::GiveAmmo(255, "AR2");
		CBaseCombatCharacter::GiveAmmo(5, "AR2AltFire");
		CBaseCombatCharacter::GiveAmmo(255, "SMG1");
		CBaseCombatCharacter::GiveAmmo(255, "Buckshot");
		CBaseCombatCharacter::GiveAmmo(3, "smg1_grenade");
		CBaseCombatCharacter::GiveAmmo(3, "rpg_round");
		CBaseCombatCharacter::GiveAmmo(5, "grenade");
		CBaseCombatCharacter::GiveAmmo(32, "357");
		CBaseCombatCharacter::GiveAmmo(16, "XBowBolt");
		CBaseCombatCharacter::GiveAmmo(5, "Hopwire");
		GiveNamedItem("weapon_smg1");
		GiveNamedItem("weapon_frag");
		GiveNamedItem("weapon_crowbar");
		GiveNamedItem("weapon_resistancegun");
		GiveNamedItem("weapon_ar2");
		GiveNamedItem("weapon_shotgun");
		GiveNamedItem("weapon_physcannon");
		GiveNamedItem("weapon_bugbait");
		GiveNamedItem("weapon_rpg");
		GiveNamedItem("weapon_357");
		GiveNamedItem("weapon_crossbow");
		if (GetHealth() < 100)
		{
			TakeHealth(25, DMG_GENERIC);
		}

		gEvilImpulse101 = false;

		break;

	default:
		BaseClass::CheatImpulseCommands(iImpulse);
	}
}

void CVancePlayer::EquipSuit(bool bPlayEffects)
{
	MDLCACHE_CRITICAL_SECTION();
	bool bHadSuit = IsSuitEquipped();

	BaseClass::EquipSuit();

	m_HL2Local.m_bDisplayReticle = true;

	// Force redeploy weapon to update the arm model.
	if (!bHadSuit && (GetActiveWeapon() && !IsInAVehicle()))
	{
		PhysCannonForceDrop(GetActiveWeapon(), NULL);
		GetActiveWeapon()->Deploy();
	}

	// Update that bod.
	if (IsSuitEquipped() != bHadSuit)
	{
		SetModel(GetPlayerWorldModel());

		CBaseViewModel* pLeg = GetViewModel(VM_LEGS);
		if (pLeg)
			pLeg->SetWeaponModel(GetLegsViewModel(), NULL);
	}

	if (bPlayEffects)
	{
		StartAdmireGlovesAnimation();
	}
}

void CVancePlayer::RemoveSuit(void)
{
	bool bHadSuit = IsSuitEquipped();

	BaseClass::RemoveSuit();

	m_HL2Local.m_bDisplayReticle = false;

	// Force redeploy weapon to update the arm model.
	if (bHadSuit && (GetActiveWeapon() && !IsInAVehicle()))
	{
		PhysCannonForceDrop(GetActiveWeapon(), NULL);
		GetActiveWeapon()->Deploy();
	}

	// Update that bod.
	if (IsSuitEquipped() != bHadSuit)
	{
		SetModel(GetPlayerWorldModel());

		CBaseViewModel* pLeg = GetViewModel(VM_LEGS);
		if (pLeg)
			pLeg->SetWeaponModel(GetLegsViewModel(), NULL);
	}
}

void CVancePlayer::CreateViewModel(int iViewModel /*= 0*/)
{
	Assert(iViewModel >= 0 && iViewModel < MAX_VIEWMODELS);

	if (GetViewModel(iViewModel))
		return;

	CVanceViewModel *pViewModel = (CVanceViewModel *)CreateEntityByName("vance_viewmodel");
	if (pViewModel)
	{
		pViewModel->SetAbsOrigin(GetAbsOrigin());
		pViewModel->SetOwner(this);
		pViewModel->SetIndex(iViewModel);
		DispatchSpawn(pViewModel);
		pViewModel->FollowEntity(this, false);
		pViewModel->SetOwnerEntity(this);

		m_hViewModel.Set(iViewModel, pViewModel);
	}
}

void CVancePlayer::HandleSpeedChanges(void)
{
	int buttonsChanged = m_afButtonPressed | m_afButtonReleased;

	bool bCanSprint = CanSprint();
	bool bIsSprinting = IsSprinting();
	bool bWantSprint = (bCanSprint && (m_nButtons & IN_SPEED));
	if (bIsSprinting != bWantSprint && (buttonsChanged & IN_SPEED))
	{
		// If someone wants to sprint, make sure they've pressed the button to do so. We want to prevent the
		// case where a player can hold down the sprint key and burn tiny bursts of sprint as the suit recharges
		// We want a full debounce of the key to resume sprinting after the suit is completely drained
		if (bWantSprint)
		{
			if (sv_stickysprint.GetBool())
			{
				StartAutoSprint();
			}
			else
			{
				StartSprinting();
			}
		}
		else
		{
			if (!sv_stickysprint.GetBool())
			{
				StopSprinting();
			}

			// Reset key, so it will be activated post whatever is suppressing it.
			m_nButtons &= ~IN_SPEED;
		}
	}

	bool bIsWalking = IsWalking();
	bool bWantWalking;
	if (IsSuitEquipped())
	{
		bWantWalking = (m_nButtons & IN_WALK) && !IsSprinting() && !(m_nButtons & IN_DUCK);
	}
	else
	{
		bWantWalking = !IsSprinting() && !(m_nButtons & IN_DUCK);
	}

	if (bIsWalking != bWantWalking)
	{
		if (bWantWalking)
		{
			StartWalking();
		}
		else
		{
			StopWalking();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Allow pre-frame adjustments on the player
//-----------------------------------------------------------------------------
void CVancePlayer::PreThink(void)
{
	if (player_showpredictedposition.GetBool())
	{
		Vector	predPos;

		UTIL_PredictedPosition(this, player_showpredictedposition_timestep.GetFloat(), &predPos);

		NDebugOverlay::Box(predPos, NAI_Hull::Mins(GetHullType()), NAI_Hull::Maxs(GetHullType()), 0, 255, 0, 0, 0.01f);
		NDebugOverlay::Line(GetAbsOrigin(), predPos, 0, 255, 0, 0, 0.01f);
	}

	if (m_hLocatorTargetEntity != NULL)
	{
		// Keep track of the entity here, the client will pick up the rest of the work
		m_HL2Local.m_vecLocatorOrigin = m_hLocatorTargetEntity->WorldSpaceCenter();
	}
	else
	{
		m_HL2Local.m_vecLocatorOrigin = vec3_invalid; // This tells the client we have no locator target.
	}

	// Riding a vehicle?
	if (IsInAVehicle())
	{
		VPROF("CHL2_Player::PreThink-Vehicle");
		// make sure we update the client, check for timed damage and update suit even if we are in a vehicle
		UpdateClientData();
		CheckTimeBasedDamage();

		// Allow the suit to recharge when in the vehicle.
		SuitPower_Update();
		CheckSuitUpdate();
		CheckSuitZoom();

		WaterMove();
		return;
	}

	// This is an experiment of mine- autojumping! 
	// only affects you if sv_autojump is nonzero.
	if ((GetFlags() & FL_ONGROUND) && sv_autojump.GetFloat() != 0)
	{
		VPROF("CHL2_Player::PreThink-Autojump");
		// check autojump
		Vector vecCheckDir;

		vecCheckDir = GetAbsVelocity();

		float flVelocity = VectorNormalize(vecCheckDir);

		if (flVelocity > 200)
		{
			// Going fast enough to autojump
			vecCheckDir = WorldSpaceCenter() + vecCheckDir * 34 - Vector(0, 0, 16);

			trace_t tr;

			UTIL_TraceHull(WorldSpaceCenter() - Vector(0, 0, 16), vecCheckDir, NAI_Hull::Mins(HULL_TINY_CENTERED), NAI_Hull::Maxs(HULL_TINY_CENTERED), MASK_PLAYERSOLID, this, COLLISION_GROUP_PLAYER, &tr);

			//NDebugOverlay::Line( tr.startpos, tr.endpos, 0,255,0, true, 10 );

			if (tr.fraction == 1.0 && !tr.startsolid)
			{
				// Now trace down!
				UTIL_TraceLine(vecCheckDir, vecCheckDir - Vector(0, 0, 64), MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &tr);

				//NDebugOverlay::Line( tr.startpos, tr.endpos, 0,255,0, true, 10 );

				if (tr.fraction == 1.0 && !tr.startsolid)
				{
					// !!!HACKHACK
					// I KNOW, I KNOW, this is definitely not the right way to do this,
					// but I'm prototyping! (sjb)
					Vector vecNewVelocity = GetAbsVelocity();
					vecNewVelocity.z += 250;
					SetAbsVelocity(vecNewVelocity);
				}
			}
		}
	}

	// Manually force us to sprint again if sprint is interrupted.
	if (m_flNextSprint != 0.0f && m_flNextSprint < gpGlobals->curtime)
	{
		m_flNextSprint = 0.0f;
		if (m_nButtons & IN_SPEED)
		{
			m_afButtonPressed |= IN_SPEED;
		}
	}

	// Manually force us to sprint again if sprint is interupted
	if (m_flNextWalk != 0.0f && m_flNextWalk < gpGlobals->curtime)
	{
		m_flNextWalk = 0.0f;
	}

	VPROF_SCOPE_BEGIN("CHL2_Player::PreThink-Speed");
	HandleSpeedChanges();
	HandleArmorReduction();

	if (sv_stickysprint.GetBool() && m_bIsAutoSprinting)
	{
		// If we're ducked and not in the air
		if (IsDucked() && GetGroundEntity() != NULL)
		{
			StopSprinting();
		}
		// Stop sprinting if the player lets off the stick for a moment.
		else if (GetStickDist() == 0.0f)
		{
			if (gpGlobals->curtime > m_fAutoSprintMinTime)
			{
				StopSprinting();
			}
		}
		else
		{
			// Stop sprinting one half second after the player stops inputting with the move stick.
			m_fAutoSprintMinTime = gpGlobals->curtime + 0.5f;
		}
	}
	else if (IsSprinting())
	{
		// Disable sprint while ducked unless we're in the air (jumping)
		if (IsDucked() && (GetGroundEntity() != NULL))
		{
			StopSprinting();
		}
	}

	VPROF_SCOPE_END();

	if (g_fGameOver || IsPlayerLockedInPlace())
		return;         // finale

	VPROF_SCOPE_BEGIN("CHL2_Player::PreThink-ItemPreFrame");
	ItemPreFrame();
	VPROF_SCOPE_END();

	VPROF_SCOPE_BEGIN("CHL2_Player::PreThink-WaterMove");
	WaterMove();
	VPROF_SCOPE_END();

	if (g_pGameRules && g_pGameRules->FAllowFlashlight())
		m_Local.m_iHideHUD &= ~HIDEHUD_FLASHLIGHT;
	else
		m_Local.m_iHideHUD |= HIDEHUD_FLASHLIGHT;


	VPROF_SCOPE_BEGIN("CHL2_Player::PreThink-CommanderUpdate");
	CommanderUpdate();
	VPROF_SCOPE_END();

	// Operate suit accessories and manage power consumption/charge
	VPROF_SCOPE_BEGIN("CHL2_Player::PreThink-SuitPower_Update");
	SuitPower_Update();
	VPROF_SCOPE_END();

	// checks if new client data (for HUD and view control) needs to be sent to the client
	VPROF_SCOPE_BEGIN("CHL2_Player::PreThink-UpdateClientData");
	UpdateClientData();
	VPROF_SCOPE_END();

	VPROF_SCOPE_BEGIN("CHL2_Player::PreThink-CheckTimeBasedDamage");
	CheckTimeBasedDamage();
	VPROF_SCOPE_END();

	VPROF_SCOPE_BEGIN("CHL2_Player::PreThink-CheckSuitUpdate");
	CheckSuitUpdate();
	VPROF_SCOPE_END();

	VPROF_SCOPE_BEGIN("CHL2_Player::PreThink-CheckSuitZoom");
	CheckSuitZoom();
	VPROF_SCOPE_END();

	if (m_lifeState >= LIFE_DYING)
	{
		PlayerDeathThink();
		return;
	}

#ifdef HL2_EPISODIC
	CheckFlashlight();
#endif	// HL2_EPISODIC

	// So the correct flags get sent to client asap.
	//
	if (m_afPhysicsFlags & PFLAG_DIROVERRIDE)
		AddFlag(FL_ONTRAIN);
	else
		RemoveFlag(FL_ONTRAIN);

	// Train speed control
	if (m_afPhysicsFlags & PFLAG_DIROVERRIDE)
	{
		CBaseEntity* pTrain = GetGroundEntity();
		float vel;

		if (pTrain)
		{
			if (!(pTrain->ObjectCaps() & FCAP_DIRECTIONAL_USE))
				pTrain = NULL;
		}

		if (!pTrain)
		{
			if (GetActiveWeapon() && (GetActiveWeapon()->ObjectCaps() & FCAP_DIRECTIONAL_USE))
			{
				m_iTrain = TRAIN_ACTIVE | TRAIN_NEW;

				if (m_nButtons & IN_FORWARD)
				{
					m_iTrain |= TRAIN_FAST;
				}
				else if (m_nButtons & IN_BACK)
				{
					m_iTrain |= TRAIN_BACK;
				}
				else
				{
					m_iTrain |= TRAIN_NEUTRAL;
				}
				return;
			}
			else
			{
				trace_t trainTrace;
				// Maybe this is on the other side of a level transition
				UTIL_TraceLine(GetAbsOrigin(), GetAbsOrigin() + Vector(0, 0, -38),
					MASK_PLAYERSOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &trainTrace);

				if (trainTrace.fraction != 1.0 && trainTrace.m_pEnt)
					pTrain = trainTrace.m_pEnt;


				if (!pTrain || !(pTrain->ObjectCaps() & FCAP_DIRECTIONAL_USE) || !pTrain->OnControls(this))
				{
					//					Warning( "In train mode with no train!\n" );
					m_afPhysicsFlags &= ~PFLAG_DIROVERRIDE;
					m_iTrain = TRAIN_NEW | TRAIN_OFF;
					return;
				}
			}
		}
		else if (!(GetFlags() & FL_ONGROUND) || pTrain->HasSpawnFlags(SF_TRACKTRAIN_NOCONTROL) || (m_nButtons & (IN_MOVELEFT | IN_MOVERIGHT)))
		{
			// Turn off the train if you jump, strafe, or the train controls go dead
			m_afPhysicsFlags &= ~PFLAG_DIROVERRIDE;
			m_iTrain = TRAIN_NEW | TRAIN_OFF;
			return;
		}

		SetAbsVelocity(vec3_origin);
		vel = 0;
		if (m_afButtonPressed & IN_FORWARD)
		{
			vel = 1;
			pTrain->Use(this, this, USE_SET, (float)vel);
		}
		else if (m_afButtonPressed & IN_BACK)
		{
			vel = -1;
			pTrain->Use(this, this, USE_SET, (float)vel);
		}

		if (vel)
		{
			m_iTrain = TrainSpeed(pTrain->m_flSpeed, ((CFuncTrackTrain*)pTrain)->GetMaxSpeed());
			m_iTrain |= TRAIN_ACTIVE | TRAIN_NEW;
		}
	}
	else if (m_iTrain & TRAIN_ACTIVE)
	{
		m_iTrain = TRAIN_NEW; // turn off train
	}

	//
	// If we're not on the ground, we're falling. Update our falling velocity.
	//
	if (!(GetFlags() & FL_ONGROUND))
	{
		m_Local.m_flFallVelocity = -GetAbsVelocity().z;
	}

	if (m_afPhysicsFlags & PFLAG_ONBARNACLE)
	{
		bool bOnBarnacle = false;
		CNPC_Barnacle* pBarnacle = NULL;
		do
		{
			// FIXME: Not a good or fast solution, but maybe it will catch the bug!
			pBarnacle = (CNPC_Barnacle*)gEntList.FindEntityByClassname(pBarnacle, "npc_barnacle");
			if (pBarnacle)
			{
				if (pBarnacle->GetEnemy() == this)
				{
					bOnBarnacle = true;
				}
			}
		} while (pBarnacle);

		if (!bOnBarnacle)
		{
			Warning("Attached to barnacle?\n");
			Assert(0);
			m_afPhysicsFlags &= ~PFLAG_ONBARNACLE;
		}
		else
		{
			SetAbsVelocity(vec3_origin);
		}
	}
	// StudioFrameAdvance( );//!!!HACKHACK!!! Can't be hit by traceline when not animating?

	// Update weapon's ready status
	UpdateWeaponPosture();

	if (m_nButtons & IN_ZOOM)
	{
		//FIXME: Held weapons like the grenade get sad when this happens
		CBaseCombatWeapon *pWep = GetActiveWeapon();
		if (!m_hUseEntity || (pWep && pWep->IsWeaponVisible()))
			m_nButtons &= ~(IN_ATTACK | IN_ATTACK2);
	}
}

bool CVancePlayer::Weapon_CanUse(CBaseCombatWeapon* pWeapon)
{
	if (pWeapon->ClassMatches("weapon_pistol"))
	{
		if (GiveAmmo(50, GetAmmoDef()->Index("Pistol"), false))
			UTIL_Remove(pWeapon);
		return false;
	}

	return BaseClass::Weapon_CanUse(pWeapon);
}

int CVancePlayer::OnTakeDamage(const CTakeDamageInfo &inputInfo)
{
	int bitsDamage = inputInfo.GetDamageType();
	float flHealthPrev = m_iHealth;

	CTakeDamageInfo info = inputInfo;

	if (m_debugOverlays & OVERLAY_BUDDHA_MODE && (m_iHealth - info.GetDamage()) <= 0)
	{
		m_iHealth = 1;
		return 0;
	}

	IServerVehicle *pVehicle = GetVehicle();
	if (pVehicle)
	{
		// Let the vehicle decide if we should take this damage or not
		if (pVehicle->PassengerShouldReceiveDamage(info) == false)
			return 0;
	}

	// Early out if there's no damage
	if (!info.GetDamage() || !IsAlive() || !g_pGameRules->FPlayerCanTakeDamage(this, info.GetAttacker(), inputInfo) || GetFlags() & FL_GODMODE)
		return 0;


	float flBonus = 0.2;
	float flRatio = 1.0;

	if ((info.GetDamageType() & DMG_BLAST) && g_pGameRules->IsMultiplayer())
		flBonus *= 2; // blasts damage armor more.

	// keep track of amount of damage last sustained
	m_lastDamageAmount = info.GetDamage();

	// Armor.
	int armorValue = m_PlayerInfo.GetArmorValue();
	if (armorValue && !(info.GetDamageType() & (DMG_FALL | DMG_DROWN | DMG_POISON | DMG_RADIATION)))
	{
		float flNew = info.GetDamage() * flRatio;
		float flArmor = (info.GetDamage() - flNew) * flBonus;
		if (flArmor < 1.0)
			flArmor = 1.0;

		// Does this use more armor than we have?
		if (flArmor > armorValue)
		{
			flArmor = armorValue;
			flArmor *= (1 / flBonus);
			flNew = info.GetDamage() - flArmor;
			m_DmgSave = armorValue;
			SetArmorValue(0);
		}
		else
		{
			m_DmgSave = flArmor;
			SetArmorValue(armorValue - flArmor);
		}

		info.SetDamage(flNew);
	}

	// Call up to the base class
	int fTookDamage = CBaseCombatCharacter::OnTakeDamage(info);

	// Early out if the base class took no damage
	if (!fTookDamage)
		return 0;

	// add to the damage total for clients, which will be sent as a single
	// message at the end of the frame
	// todo: remove after combining shotgun blasts?
	if (info.GetInflictor() && info.GetInflictor()->edict())
		m_DmgOrigin = info.GetInflictor()->GetAbsOrigin();

	m_DmgTake += (int)info.GetDamage();

	// Reset damage time countdown for each type of time based damage player just sustained
	for (int i = 0; i < CDMG_TIMEBASED; i++)
	{
		// Make sure the damage type is really time-based.
		// This is kind of hacky but necessary until we setup DamageType as an enum.
		int iDamage = (DMG_PARALYZE << i);
		if ((info.GetDamageType() & iDamage) && g_pGameRules->Damage_IsTimeBased(iDamage))
			m_rgbTimeBasedDamage[i] = 0;
	}

	// Display any effect associate with this damage type
	DamageEffect(info.GetDamage(), bitsDamage);

	// Severity of the damage.
	bool bTrivial = (m_iHealth > 75 || m_lastDamageAmount < 5);
	bool bMajor = (m_lastDamageAmount > 25);
	bool bHeavy = (m_iHealth < 50);
	bool bCritical = (m_iHealth < 30);

	// Don't make pain sounds for a pinch of damage
	if (!bTrivial && m_flNextPainSound < gpGlobals->curtime)
	{
		if(bHeavy)
			EmitSound("AlyxPlayer.PainHeavy");
		else
			EmitSound("Player.Death"); // should be "Player.Pain" or something, bad name...

		m_flNextPainSound = gpGlobals->curtime + RandomFloat(3.0f, 5.0f);
	}

	m_bitsDamageType |= bitsDamage; // Save this so we can report it to the client
	m_bitsHUDDamage = -1;  // make sure the damage bits get resent

	#define MASK_CANBLEED (DMG_SLASH|DMG_FALL|DMG_BULLET|DMG_CLUB)

	bool bFound = true;
	if (bitsDamage & MASK_CANBLEED)
	{
		m_flLastDamage = gpGlobals->curtime;

		int flChance = random->RandomInt(0, 100);
		if (flChance <= m_flBleedChance)
		{
			Bleed();
			SetSuitUpdate("!HEV_DMG6", false, SUIT_NEXT_IN_30SEC);	// blood loss detected
			bitsDamage &= ~MASK_CANBLEED;
		}
		else
		{
			m_flBleedChance += sk_bleed_chance_increment.GetFloat();
		}
	}

	while ((!bTrivial || g_pGameRules->Damage_IsTimeBased(bitsDamage)) && bFound && bitsDamage)
	{
		bFound = false;

		if (bitsDamage & DMG_BLEED)
		{
			bitsDamage &= ~DMG_BLEED;
			bFound = true;
		}
		if (bitsDamage & DMG_CRUSH)
		{
			if (bMajor)
				SetSuitUpdate("!HEV_DMG5", false, SUIT_NEXT_IN_30SEC);	// major fracture
			else
				SetSuitUpdate("!HEV_DMG4", false, SUIT_NEXT_IN_30SEC);	// minor fracture

			bitsDamage &= ~DMG_CRUSH;
			bFound = true;
		}
		if (bitsDamage & DMG_SLASH)
		{
			if (bMajor)
				SetSuitUpdate("!HEV_DMG1", false, SUIT_NEXT_IN_30SEC);	// major laceration
			else
				SetSuitUpdate("!HEV_DMG0", false, SUIT_NEXT_IN_30SEC);	// minor laceration
		}
		if (bitsDamage & DMG_FALL)
		{
			if (bMajor)
				SetSuitUpdate("!HEV_DMG5", false, SUIT_NEXT_IN_30SEC);	// major fracture
			else
				SetSuitUpdate("!HEV_DMG4", false, SUIT_NEXT_IN_30SEC);	// minor fracture
		}
		if (bitsDamage & DMG_CLUB)
		{
			if (bMajor)
				SetSuitUpdate("!HEV_DMG4", false, SUIT_NEXT_IN_30SEC);	// minor fracture
		}
		if (bitsDamage & DMG_SLASH)
		{
			if (bMajor)
				SetSuitUpdate("!HEV_DMG1", false, SUIT_NEXT_IN_30SEC);	// major laceration
			else
				SetSuitUpdate("!HEV_DMG0", false, SUIT_NEXT_IN_30SEC);	// minor laceration

			bitsDamage &= ~DMG_SLASH;
			bFound = true;
		}
		if (bitsDamage & DMG_SONIC)
		{
			if (bMajor)
				SetSuitUpdate("!HEV_DMG2", false, SUIT_NEXT_IN_1MIN);	// internal bleeding
			bitsDamage &= ~DMG_SONIC;
			bFound = true;
		}
		if (bitsDamage & (DMG_POISON | DMG_PARALYZE) && IsSuitEquipped()
			&& g_pGameRules->Damage_IsTimeBased(bitsDamage))
		{
			if (bitsDamage & DMG_POISON)
			{
				m_nPoisonDmg += info.GetDamage();
				m_tbdPrev = gpGlobals->curtime;
				m_rgbTimeBasedDamage[itbd_PoisonRecover] = 0;
			}

			SetSuitUpdate("!HEV_DMG3", false, SUIT_NEXT_IN_1MIN);	// blood toxins detected
			bitsDamage &= ~(DMG_POISON | DMG_PARALYZE);
			bFound = true;
		}
		if (bitsDamage & DMG_ACID)
		{
			SetSuitUpdate("!HEV_DET1", false, SUIT_NEXT_IN_1MIN);	// hazardous chemicals detected
			bitsDamage &= ~DMG_ACID;
			bFound = true;
		}
		if (bitsDamage & DMG_NERVEGAS)
		{
			SetSuitUpdate("!HEV_DET0", false, SUIT_NEXT_IN_1MIN);	// biohazard detected
			bitsDamage &= ~DMG_NERVEGAS;
			bFound = true;
		}
		if (bitsDamage & DMG_RADIATION)
		{
			SetSuitUpdate("!HEV_DET2", false, SUIT_NEXT_IN_1MIN);	// radiation detected
			bitsDamage &= ~DMG_RADIATION;
			bFound = true;
		}
		if (bitsDamage & DMG_SHOCK)
		{
			bitsDamage &= ~DMG_SHOCK;
			bFound = true;
		}
	}

	float flPunch = -2;

	if (info.GetAttacker() && !FInViewCone(info.GetAttacker()))
	{
		if (info.GetDamage() > 10.0f)
			flPunch = -10;
		else
			flPunch = RandomFloat(-5, -7);
	}

	m_Local.m_vecPunchAngle.SetX(flPunch);

	if (!bTrivial && bMajor && flHealthPrev >= 75)
	{
		// first time we take major damage...
		// turn automedic on if not on
		SetSuitUpdate("!HEV_MED1", false, SUIT_NEXT_IN_30MIN);	// automedic on

		// give morphine shot if not given recently
		SetSuitUpdate("!HEV_HEAL7", false, SUIT_NEXT_IN_30MIN);	// morphine shot
	}

	if (!bTrivial && bCritical && flHealthPrev < 75)
	{
		// already took major damage, now it's critical...
		if (m_iHealth < 6)
			SetSuitUpdate("!HEV_HLTH3", false, SUIT_NEXT_IN_10MIN);	// near death
		else if (m_iHealth < 20)
			SetSuitUpdate("!HEV_HLTH2", false, SUIT_NEXT_IN_10MIN);	// health critical

		// give critical health warnings
		if (!random->RandomInt(0, 3) && flHealthPrev < 50)
			SetSuitUpdate("!HEV_DMG7", false, SUIT_NEXT_IN_5MIN); //seek medical attention
	}

	// if we're taking time based damage, warn about its continuing effects
	if (g_pGameRules->Damage_IsTimeBased(info.GetDamageType()) && flHealthPrev < 75)
	{
		if (flHealthPrev < 50)
		{
			if (!random->RandomInt(0, 3))
				SetSuitUpdate("!HEV_DMG7", false, SUIT_NEXT_IN_5MIN); //seek medical attention
		}
		else
		{
			SetSuitUpdate("!HEV_HLTH1", false, SUIT_NEXT_IN_10MIN);	// health dropping
		}
	}

	// Do special explosion damage effect
	if (bitsDamage & DMG_BLAST)
		OnDamagedByExplosion(info);

	return 1;
}

void CVancePlayer::Heal(int health)
{
	SetHealth(clamp(GetHealth() + health, 0, GetMaxHealth()));
}

static void Cmd_Heal(const CCommand &args)
{
	if (args.ArgC() < 1 || FStrEq(args.Arg(1), ""))
	{
		Msg("Usage: heal [amount of damage you want to heal]\n");
		return;
	}

	CVancePlayer *pPlayer = (CVancePlayer *)UTIL_GetLocalPlayer();
	if (pPlayer)
		pPlayer->Heal(V_atoi(args.Arg(1)));
}
ConCommand heal("heal", Cmd_Heal, "Makes the executing Player heal damage by a certain amount.", FCVAR_CHEAT);

void CVancePlayer::Damage(int damage)
{
	CTakeDamageInfo info;
	info.SetInflictor(this);
	info.SetDamage(damage);
	info.SetDamageType(DMG_DIRECT);

	TakeDamage(info);
}

static void Cmd_Damage(const CCommand &args)
{
	if (args.ArgC() < 1 || FStrEq(args.Arg(1), ""))
	{
		Msg("Usage: damage [amount of damage you want to take]\n");
		return;
	}

	CVancePlayer *pPlayer = (CVancePlayer *)UTIL_GetLocalPlayer();
	if (pPlayer)
		pPlayer->Damage(V_atoi(args.Arg(1)));
}
ConCommand damage("damage", Cmd_Damage, "Makes the executing Player take damage by a certain amount.", FCVAR_CHEAT);

static void Cmd_Bleed(const CCommand &args)
{
	CVancePlayer *pPlayer = (CVancePlayer *)UTIL_GetLocalPlayer();
	if (pPlayer)
		pPlayer->Bleed();
}
ConCommand bleed("bleed", Cmd_Bleed, "Makes the executing Player start bleeding.", FCVAR_CHEAT);

void CVancePlayer::Bleed()
{
	m_bBleed = true;
	m_flNextBleedTime = gpGlobals->curtime + sk_bleed_dmg_interval.GetFloat();
	m_flBleedEndTime = gpGlobals->curtime + sk_bleed_dmg_interval.GetFloat() + sk_bleed_lifetime.GetFloat();
}

void CVancePlayer::UseTourniquet()
{
	if (NumTourniquets() != 0 && IsBleeding())
		m_bBleed = false;
}

void CVancePlayer::InjectStim()
{
	if (m_bBusyInAnim)
		return;

	CVanceViewModel *pViewModel = (CVanceViewModel *)GetViewModel();
	if (pViewModel)
	{
		pViewModel->SetWeaponModel("models/weapons/v_stim.mdl", NULL);

		int	idealSequence = pViewModel->LookupSequence("inject");
		if (idealSequence >= 0)
		{
			pViewModel->SendViewModelMatchingSequence(idealSequence);
			m_bShouldRegenerate = true;
			m_flNextHealTime = gpGlobals->curtime + sk_stim_heal_interval.GetFloat();
			m_flRegenerateEndTime = gpGlobals->curtime + sk_stim_heal_interval.GetFloat() + sk_stim_regen_lifetime.GetFloat();

			SetBusy(gpGlobals->curtime + SequenceDuration(idealSequence));
		}
	}
}

// might be better to use PreThink for this
void CVancePlayer::PostThink(void)
{
	BaseClass::PostThink();

	if (gpGlobals->curtime <= m_flNextBleedChanceDecay && 
		gpGlobals->curtime - m_flLastDamage >= sk_bleed_chance_decay_start_time.GetFloat())
	{
		m_flBleedChance = clamp(m_flBleedChance - sk_bleed_chance_increment.GetFloat(), 5, 100);
		m_flNextBleedChanceDecay = gpGlobals->curtime + sk_bleed_chance_decay_rate.GetFloat();
	}

	if (m_bBleed)
	{
		if (gpGlobals->curtime >= m_flBleedEndTime)
		{
			m_bBleed = false;
		}
		else if (gpGlobals->curtime >= m_flNextBleedTime)
		{
			EmitSound("HL2_Player.UseDeny");
			Damage(sk_bleed_dmg_per_interval.GetFloat());
			m_flNextBleedTime = gpGlobals->curtime + sk_bleed_dmg_interval.GetFloat();
		}
	}

	if (m_bShouldRegenerate)
	{
		if (gpGlobals->curtime >= m_flRegenerateEndTime)
		{
			m_bShouldRegenerate = false;
		}
		else if (gpGlobals->curtime >= m_flNextHealTime)
		{
			Heal(sk_stim_health_per_interval.GetInt());
			m_flNextHealTime = gpGlobals->curtime + sk_stim_heal_interval.GetFloat();
		}
	}

	// Assign our speed to this variable
	float playerSpeed = GetLocalVelocity().Length2D();

	// If sprinting and shot(s) fired, slow us down as a penalty.
	if (playerSpeed >= 300 /*&& GetActiveWeapon() != NULL*/)
	{
		if (m_nButtons & (IN_ATTACK | IN_ATTACK2))
		{
			StopSprinting();
			m_flNextSprint = gpGlobals->curtime + 1.0f;
		}
	}

	if (gpGlobals->curtime >= m_flBusyAnimEndTime)
	{
		m_bBusyInAnim = false;
	}
	else if(m_bBusyInAnim)
	{
		return;
	}

	CBaseViewModel* vm = GetViewModel(VM_LEGS);

	Activity curActivity = GetActivity();
	if (curActivity == ACT_VM_WALK ||
		curActivity == ACT_VM_SPRINT)
	{
		float playbackSpeed = (((int)playerSpeed ^ 2) / 10) / 320; // This curve function should be tweaked
		GetViewModel()->SetPlaybackRate(playbackSpeed);
	}
	else
	{
		GetViewModel()->SetPlaybackRate(1.0f);
	}

	if (m_afButtonReleased & IN_ATTACK3 && m_flNextKick < gpGlobals->curtime)
	{
		if (vm)
		{
			bool bIsInAir = GetGroundEntity() == NULL;
			int	idealSequence = bIsInAir ? vm->SelectWeightedSequence(ACT_VM_SECONDARYATTACK) : vm->SelectWeightedSequence(ACT_VM_PRIMARYATTACK);
			if (idealSequence >= 0)
			{
				vm->SendViewModelMatchingSequence(idealSequence);
				m_flNextKickAttack = gpGlobals->curtime + vm->SequenceDuration(idealSequence) - 0.5f;
				m_flKickAnimLength = gpGlobals->curtime + vm->SequenceDuration(idealSequence) - 0.1f;
				m_flNextKick = gpGlobals->curtime + vance_kick_firerate.GetFloat();

				StopSprinting();
				StartWalking();
				m_flNextSprint = gpGlobals->curtime + 1.0f;
				m_flNextWalk = gpGlobals->curtime + 1.0f;
			}
		}

		CBaseCombatWeapon* pActiveWeapon = GetActiveWeapon();
		if (pActiveWeapon)
		{
			int	idealSequence = GetActiveWeapon()->SelectWeightedSequence(ACT_VM_KICK);
			if (idealSequence >= 0)
			{
				pActiveWeapon->SendWeaponAnim(ACT_VM_KICK);
			}
		}

		QAngle recoil = QAngle(random->RandomFloat(1.0f, 2.0f), 0, 0);
		ViewPunch(recoil);

		SetKickTime(vm);
		m_bIsKicking = true;
	}

	if (m_flKickTime < gpGlobals->curtime && m_bIsKicking)
	{
		KickAttack();
		m_bIsKicking = false;
	}

	if (vm && m_flKickAnimLength < gpGlobals->curtime)
	{
		int	idealSequence = vm->SelectWeightedSequence(ACT_VM_IDLE);
		if (idealSequence >= 0)
		{
			vm->SendViewModelMatchingSequence(idealSequence);
		}
	}

	m_angEyeAngles = EyeAngles();

	QAngle angles = GetLocalAngles();
	angles[PITCH] = 0;
	SetLocalAngles(angles);

	switch (m_ParkourAction)
	{
	case ACTION_SLIDE:
		HandleSlide();
		//SetNextThink(gpGlobals->curtime + 0.05f);
		break;
	case ACTION_VAULT:
		HandleVault();
		//SetNextThink(gpGlobals->curtime + 0.05f);
		break;
	case ACTION_CLIMB:
		HandleLedgeClimb();
		//SetNextThink(gpGlobals->curtime + 0.05f);
		break;
	default:
		break;
	}

	m_pPlayerAnimState->Update();
}

void CVancePlayer::StartAdmireGlovesAnimation(void)
{
	MDLCACHE_CRITICAL_SECTION();

	CVanceViewModel *vm = (CVanceViewModel *)(GetViewModel());
	if (vm && !GetActiveWeapon())
	{
		vm->SetWeaponModel(GetArmsViewModel(), NULL);
		ShowViewModel(true);

		int	idealSequence = vm->LookupSequence("seq_admire");
		if (idealSequence >= 0)
		{
			vm->SendViewModelMatchingSequence(idealSequence);
			m_flAdmireGlovesAnimTime = gpGlobals->curtime + vm->SequenceDuration(idealSequence);
		}
	}
}

void CVancePlayer::Spawn(void)
{
	BaseClass::Spawn();

	Precache();

	SetModel(GetPlayerWorldModel());

//	CreateViewModel();
	CreateViewModel(VM_LEGS);
	CBaseViewModel *pLeg = GetViewModel(VM_LEGS);
	if (pLeg)
		pLeg->SetWeaponModel(GetLegsViewModel(), NULL);

	CreateViewModel(VM_STIM);
	CBaseViewModel *pStim = GetViewModel(VM_STIM);
	if (pStim)
		pStim->SetWeaponModel("models/weapons/v_stim.mdl", NULL);

	SetTouch(&CVancePlayer::Touch);
	SetThink(&CVancePlayer::Think);
	SetNextThink(gpGlobals->curtime + 0.05f);
}

//-----------------------------------------------------------------------------
// Purpose: Returns whether or not we are allowed to sprint now.
//-----------------------------------------------------------------------------
bool CVancePlayer::CanSprint()
{
	if (m_flNextSprint >= gpGlobals->curtime)
		return false;

	return (m_bSprintEnabled &&										// Only if sprint is enabled
		!(m_Local.m_bDucked && !m_Local.m_bDucking) &&			// Nor if we're ducking
		(GetWaterLevel() != 3) &&									// Certainly not underwater
		(GlobalEntity_GetState("suit_no_sprint") != GLOBAL_ON));	// Out of the question without the sprint module
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CVancePlayer::StartSprinting(void)
{
	CPASAttenuationFilter filter(this);
	filter.UsePredictionRules();
	if (GetHealth() < 50)
	{
		EmitSound(filter, entindex(), "AlyxPlayer.SprintPain");
	}
	else
	{
		EmitSound(filter, entindex(), "AlyxPlayer.Sprint");
	}
	if (IsSuitEquipped() && m_HL2Local.m_flSuitPower < 10)
	{
		// Don't sprint unless there's a reasonable
		// amount of suit power.

		// debounce the button for sound playing
		if (m_afButtonPressed & IN_SPEED)
		{
			CPASAttenuationFilter filter(this);
			filter.UsePredictionRules();
			EmitSound(filter, entindex(), "HL2Player.SprintNoPower");
		}

		return;
	}

	SetMaxSpeed(VANCE_SPRINT_SPEED);

	m_fIsSprinting = true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CVancePlayer::StopSprinting(void)
{
	SetMaxSpeed(VANCE_WALK_SPEED);

	m_fIsSprinting = false;

	StopSound("AlyxPlayer.SprintPain");
	StopSound("AlyxPlayer.Sprint");

	if (sv_stickysprint.GetBool())
	{
		m_bIsAutoSprinting = false;
		m_fAutoSprintMinTime = 0.0f;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CVancePlayer::StartWalking(void)
{
	m_fIsWalking = true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CVancePlayer::StopWalking(void)
{
	if (!IsSprinting())
	{
		if (!PhysCannonGetHeldEntity(GetActiveWeapon()))
		{
			SetMaxSpeed(VANCE_WALK_SPEED);
		}

		m_fIsWalking = false;
	}
}

extern CSuitPowerDevice SuitDeviceFlashlight;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CVancePlayer::SuitPower_Update(void)
{
	if (SuitPower_ShouldRecharge())
	{
		SuitPower_Charge(SUITPOWER_CHARGE_RATE * gpGlobals->frametime);
	}
	else if (m_HL2Local.m_bitsActiveDevices)
	{
		float flPowerLoad = m_flSuitPowerLoad;

		if (SuitPower_IsDeviceActive(SuitDeviceFlashlight))
		{
			float factor;

			factor = 1.0f / m_flFlashlightPowerDrainScale;

			flPowerLoad -= (SuitDeviceFlashlight.GetDeviceDrainRate() * (1.0f - factor));
		}

		if (!SuitPower_Drain(flPowerLoad * gpGlobals->frametime))
		{
			// TURN OFF ALL DEVICES!!
			if (IsSprinting())
			{
				StopSprinting();
			}
		}
	}
}

extern ConVar sk_battery;
ConVar sk_battery_flashlight("sk_battery_flashlight", "0");

bool CVancePlayer::ApplyBattery(float powerMultiplier, bool bFlashlightPower /*= false*/)
{
	if (bFlashlightPower)
	{
		m_HL2Local.m_flFlashBattery += sk_battery_flashlight.GetFloat() * powerMultiplier;

		CPASAttenuationFilter filter(this, "ItemBattery.Touch_Suitless");
		EmitSound(filter, entindex(), "ItemBattery.Touch_Suitless");

		return true;
	}

	const float MAX_NORMAL_BATTERY = 100;
	if (IsSuitEquipped() && ArmorValue() < MAX_NORMAL_BATTERY)
	{
		int pct;
		char szcharge[64];

		IncrementArmorValue(sk_battery.GetFloat() * powerMultiplier, MAX_NORMAL_BATTERY);

		CPASAttenuationFilter filter(this, "ItemBattery.Touch");
		EmitSound(filter, entindex(), "ItemBattery.Touch");

		CSingleUserRecipientFilter user(this);
		user.MakeReliable();

		UserMessageBegin(user, "ItemPickup");
		WRITE_STRING("item_battery");
		MessageEnd();


		// Suit reports new power level
		// For some reason this wasn't working in release build -- round it.
		pct = (int)((float)(ArmorValue() * 100.0f) * (1.0f / MAX_NORMAL_BATTERY) + 0.5f);
		pct = (pct / 5);
		if (pct > 0)
			pct--;

		Q_snprintf(szcharge, sizeof(szcharge), "!HEV_%1dP", pct);

		//UTIL_EmitSoundSuit(edict(), szcharge);
		//SetSuitUpdate(szcharge, FALSE, SUIT_NEXT_IN_30SEC);
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CVancePlayer::FlashlightTurnOn(void)
{
	if (m_bFlashlightDisabled)
		return;

	if (!IsSuitEquipped())
	{
		// Turn the flashlight off if we don't have the 'flashlight' attachment.
		CBaseViewModel* pVM = GetViewModel();
		if (!GetActiveWeapon() || (pVM && !pVM->LookupAttachment("flashlight")))
			return;

		EmitSound("HL2Player.FlashLightOn_Suitless");
	}
	else
	{
		EmitSound("HL2Player.FlashLightOn");
	}

	AddEffects(EF_DIMLIGHT);

	variant_t flashlighton;
	flashlighton.SetFloat(m_HL2Local.m_flSuitPower / 100.0f);
	FirePlayerProxyOutput("OnFlashlightOn", flashlighton, this, this);
}

// Dear lord, why must you make me do this the wrong way?
ConVar vance_kick_meleedamageforce("kick_meleedamageforce", "2", FCVAR_ARCHIVE, "The default throw force of kick without player velocity.");
ConVar vance_kick_powerscale("kick_powerscale", "1", FCVAR_ARCHIVE, "The default damage of kick without player velocity.");

void CVancePlayer::Hit(trace_t& traceHit, Activity nHitActivity, bool bIsSecondary)
{
	CBasePlayer* pPlayer = this;

	// Make sound for the AI
	CSoundEnt::InsertSound(SOUND_BULLET_IMPACT, traceHit.endpos, 400, 0.2f, pPlayer);

	// This isn't great, but it's something for when the crowbar hits.
	pPlayer->RumbleEffect(RUMBLE_AR2, 0, RUMBLE_FLAG_RESTART);

	CBaseEntity* pHitEntity = traceHit.m_pEnt;

	float flDamageMult = RemapValClamped(GetLocalVelocity().Length2D(), 0, hl2_sprintspeed.GetFloat(), vance_kick_damage_mult_min.GetFloat(), vance_kick_damage_mult_max.GetFloat());
	float flForceMult = RemapValClamped(GetLocalVelocity().Length2D(), 0, hl2_sprintspeed.GetFloat(), vance_kick_force_mult_min.GetFloat(), vance_kick_force_mult_max.GetFloat());

	//Apply damage to a hit target
	if (pHitEntity)
	{
		Vector hitDirection;
		pPlayer->EyeVectors(&hitDirection, NULL, NULL);
		VectorNormalize(hitDirection);

		CTakeDamageInfo info(pPlayer, pPlayer, 20 * vance_kick_powerscale.GetFloat() * flDamageMult, DMG_KICK);

		if (pPlayer && pHitEntity->IsNPC())
		{
			// If bonking an NPC, adjust damage.
			info.AdjustPlayerDamageInflictedForSkillLevel();
		}

		CalculateMeleeDamageForce(&info, hitDirection, traceHit.endpos);
		info.SetDamageForce(info.GetDamageForce() * vance_kick_meleedamageforce.GetFloat() * flForceMult);

		pHitEntity->DispatchTraceAttack(info, hitDirection, &traceHit);
		ApplyMultiDamage();

		// Now hit all triggers along the ray that... 
		TraceAttackToTriggers(info, traceHit.startpos, traceHit.endpos, hitDirection);

		if (ToBaseCombatCharacter(pHitEntity))
		{
			gamestats->Event_WeaponHit(pPlayer, !bIsSecondary, GetClassname(), info);
		}
	}

	// Apply an impact effect
	//ImpactEffect(traceHit);
}

void CVancePlayer::SetKickTime(CBaseViewModel* pViewModel)
{
	m_flKickTime = ((pViewModel ? pViewModel->SequenceDuration() : 0) / 2) / 2 + gpGlobals->curtime + vance_kick_time_adjust.GetFloat();
}

void CVancePlayer::KickAttack(void)
{
	MDLCACHE_CRITICAL_SECTION();

	trace_t traceHit;
	Vector vecDirection;
	//int kick_maxrange = 120;
	AngleVectors(QAngle(EyeAngles().x, EyeAngles().y, EyeAngles().z), &vecDirection);

	//vm->SendViewModelMatchingSequence(idealSequence);

	// Try a ray
	CBasePlayer* pOwner = this;
	if (!pOwner)
		return;

	pOwner->RumbleEffect(RUMBLE_CROWBAR_SWING, 0, RUMBLE_FLAG_RESTART);

	Vector swingStart = Weapon_ShootPosition();
	Vector forward;

	float flDamageMult = RemapValClamped(GetLocalVelocity().Length2D(), 0, hl2_sprintspeed.GetFloat(), vance_kick_damage_mult_min.GetFloat(), vance_kick_damage_mult_max.GetFloat());
	float flForceMult = RemapValClamped(GetLocalVelocity().Length2D(), 0, hl2_sprintspeed.GetFloat(), vance_kick_force_mult_min.GetFloat(), vance_kick_force_mult_max.GetFloat());

	forward = pOwner->GetAutoaimVector(AUTOAIM_SCALE_DEFAULT, 64);

	Vector swingEnd = swingStart + forward * vance_kick_range.GetFloat();
	UTIL_TraceLine(swingStart, swingEnd, MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &traceHit);
	//Activity nHitActivity = ACT_VM_PRIMARYATTACK;

	// Like bullets, bludgeon traces have to trace against triggers.
	CTakeDamageInfo triggerInfo(this, this, 20 * vance_kick_powerscale.GetFloat() * flDamageMult, DMG_KICK);
	triggerInfo.SetDamagePosition(traceHit.startpos);
	triggerInfo.SetDamageForce(triggerInfo.GetDamageForce() * vance_kick_meleedamageforce.GetFloat() * flForceMult);
	TraceAttackToTriggers(triggerInfo, traceHit.startpos, traceHit.endpos, forward);

	if (traceHit.fraction == 1.0)
	{
		float bludgeonHullRadius = 1.732f * BLUDGEON_HULL_DIM;  // Hull is +/- 16, so use cuberoot of 2 to determine how big the hull is from center to the corner point
		swingEnd -= forward * bludgeonHullRadius;	// Back off by hull "radius"

		UTIL_TraceHull(swingStart, swingEnd, g_bludgeonMins, g_bludgeonMaxs, MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &traceHit);
		if (traceHit.fraction < 1.0 && traceHit.m_pEnt)
		{
			Vector vecToTarget = traceHit.m_pEnt->GetAbsOrigin() - swingStart;
			VectorNormalize(vecToTarget);

			float dot = vecToTarget.Dot(forward);

			// YWB:  Make sure they are sort of facing the guy at least...
			if (dot < 0.70721f)
			{
				// Force amiss
				traceHit.fraction = 1.0f;
			}
		}
	}

	if (traceHit.DidHit())
	{
		EmitSound("HL2Player.KickHit");
		UTIL_ScreenShake(GetAbsOrigin(), 4.0f, 10.0f, 0.25f, 1000, SHAKE_START, false);
	}
	else
	{
		EmitSound("HL2Player.KickMiss");
	}

	QAngle	recoil = QAngle(random->RandomFloat(-1.0f, -2.0f), 0, random->RandomFloat(-2.0f, 2.0f));
	ViewPunch(recoil);

	gamestats->Event_WeaponFired(pOwner, false, GetClassname());

	if (traceHit.fraction != 1.0f)
	{
		Hit(traceHit, ACT_VM_PRIMARYATTACK, false);
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : void CBasePlayer::PlayerUse
//-----------------------------------------------------------------------------
void CVancePlayer::PlayerUse(void)
{
	// Was use pressed or released?
	if (!((m_nButtons | m_afButtonPressed | m_afButtonReleased) & IN_USE))
		return;

	if (m_afButtonPressed & IN_USE)
	{
		// Currently using a latched entity?
		if (ClearUseEntity())
		{
			return;
		}
		else
		{
			if (m_afPhysicsFlags & PFLAG_DIROVERRIDE)
			{
				m_afPhysicsFlags &= ~PFLAG_DIROVERRIDE;
				m_iTrain = TRAIN_NEW | TRAIN_OFF;
				return;
			}
			else
			{	// Start controlling the train!
				CBaseEntity* pTrain = GetGroundEntity();
				if (pTrain && !(m_nButtons & IN_JUMP) && (GetFlags() & FL_ONGROUND) && (pTrain->ObjectCaps() & FCAP_DIRECTIONAL_USE) && pTrain->OnControls(this))
				{
					m_afPhysicsFlags |= PFLAG_DIROVERRIDE;
					m_iTrain = TrainSpeed(pTrain->m_flSpeed, ((CFuncTrackTrain*)pTrain)->GetMaxSpeed());
					m_iTrain |= TRAIN_NEW;
					EmitSound("HL2Player.TrainUse");
					return;
				}
			}
		}

		// Tracker 3926:  We can't +USE something if we're climbing a ladder
		if (GetMoveType() == MOVETYPE_LADDER)
		{
			return;
		}
	}

	if (m_flTimeUseSuspended > gpGlobals->curtime)
	{
		// Something has temporarily stopped us being able to USE things.
		// Obviously, this should be used very carefully.(sjb)
		return;
	}

	CBaseEntity* pUseEntity = FindUseEntity();

	bool usedSomething = false;

	// Found an object
	if (pUseEntity)
	{
		//!!!UNDONE: traceline here to prevent +USEing buttons through walls			
		int caps = pUseEntity->ObjectCaps();
		variant_t emptyVariant;

		if (IsSuitEquipped() && (m_afButtonPressed & IN_USE))
		{
			// Robin: Don't play sounds for NPCs, because NPCs will allow respond with speech.
			if (!pUseEntity->MyNPCPointer())
			{
				EmitSound("HL2Player.Use");
			}
		}

		if (((m_nButtons & IN_USE) && (caps & FCAP_CONTINUOUS_USE)) ||
			((m_afButtonPressed & IN_USE) && (caps & (FCAP_IMPULSE_USE | FCAP_ONOFF_USE))))
		{
			if (caps & FCAP_CONTINUOUS_USE)
				m_afPhysicsFlags |= PFLAG_USING;

			pUseEntity->AcceptInput("Use", this, this, emptyVariant, USE_TOGGLE);

			usedSomething = true;
		}
		// UNDONE: Send different USE codes for ON/OFF.  Cache last ONOFF_USE object to send 'off' if you turn away
		else if ((m_afButtonReleased & IN_USE) && (pUseEntity->ObjectCaps() & FCAP_ONOFF_USE))	// BUGBUG This is an "off" use
		{
			pUseEntity->AcceptInput("Use", this, this, emptyVariant, USE_TOGGLE);

			usedSomething = true;
		}
	}
	else if (m_afButtonPressed & IN_USE)
	{
		// Signal that we want to play the deny sound, unless the user is +USEing on a ladder!
		// The sound is emitted in ItemPostFrame, since that occurs after GameMovement::ProcessMove which
		// lets the ladder code unset this flag.
		m_bPlayUseDenySound = true;
	}

	// Debounce the use key
	if (usedSomething && pUseEntity)
	{
		m_Local.m_nOldButtons |= IN_USE;
		m_afButtonPressed &= ~IN_USE;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CVancePlayer::UpdateClientData(void)
{
	if (m_DmgTake || m_DmgSave || m_bitsHUDDamage != m_bitsDamageType)
	{
		// Comes from inside me if not set
		Vector damageOrigin = GetLocalOrigin();
		// send "damage" message
		// causes screen to flash, and pain compass to show direction of damage
		damageOrigin = m_DmgOrigin;

		// only send down damage type that have hud art
		int iShowHudDamage = g_pGameRules->Damage_GetShowOnHud();
		int visibleDamageBits = m_bitsDamageType & iShowHudDamage;

		m_DmgTake = clamp(m_DmgTake, 0, 255);
		m_DmgSave = clamp(m_DmgSave, 0, 255);

		// If we're poisoned, but it wasn't this frame, don't send the indicator
		// Without this check, any damage that occured to the player while they were
		// recovering from a poison bite would register as poisonous as well and flash
		// the whole screen! -- jdw
		if (visibleDamageBits & DMG_POISON)
		{
			float flLastPoisonedDelta = gpGlobals->curtime - m_tbdPrev;
			if (flLastPoisonedDelta > 0.1f)
			{
				visibleDamageBits &= ~DMG_POISON;
			}
		}

		CSingleUserRecipientFilter user(this);
		user.MakeReliable();
		UserMessageBegin(user, "Damage");
		WRITE_BYTE(m_DmgSave);
		WRITE_BYTE(m_DmgTake);
		WRITE_LONG(visibleDamageBits);
		WRITE_FLOAT(damageOrigin.x);	//BUG: Should be fixed point (to hud) not floats
		WRITE_FLOAT(damageOrigin.y);	//BUG: However, the HUD does _not_ implement bitfield messages (yet)
		WRITE_FLOAT(damageOrigin.z);	//BUG: We use WRITE_VEC3COORD for everything else
		MessageEnd();

		m_DmgTake = 0;
		m_DmgSave = 0;
		m_bitsHUDDamage = m_bitsDamageType;

		// Clear off non-time-based damage indicators
		int iTimeBasedDamage = g_pGameRules->Damage_GetTimeBased();
		m_bitsDamageType &= iTimeBasedDamage;
	}

	// Update Flashlight
	if (FlashlightIsOn() && !sv_infinite_aux_power.GetBool())
	{
		m_HL2Local.m_flFlashBattery -= (!IsSuitEquipped() ? FLASH_DRAIN_TIME_SUITLESS : FLASH_DRAIN_TIME) * gpGlobals->frametime;
		if (m_HL2Local.m_flFlashBattery < 0.0f)
		{
			FlashlightTurnOff();
			m_HL2Local.m_flFlashBattery = 0.0f;
		}

		// Turn off the flashlight if the current weapon doesn't have the 'flashlight' attachment,
		// remove this check to fallback to the 'muzzle' attachment.
		CBaseViewModel* pVM = GetViewModel();
		if (!IsSuitEquipped() && (!GetActiveWeapon() || (pVM && !pVM->LookupAttachment("flashlight"))))
		{
			FlashlightTurnOff();
		}
	}
	else
	{
		// Allow a suitless player to horde flashlight power when grabbing batteries.
		m_HL2Local.m_flFlashBattery += (!IsSuitEquipped() ? FLASH_CHARGE_TIME_SUITLESS : FLASH_CHARGE_TIME) * gpGlobals->frametime;
		if (IsSuitEquipped() && m_HL2Local.m_flFlashBattery > 100.0f)
		{
			m_HL2Local.m_flFlashBattery = 100.0f;
		}
	}

	BaseClass::UpdateClientData();
}

void CVancePlayer::ItemPostFrame()
{
	BaseClass::ItemPostFrame();

	if (m_bPlayUseDenySound)
	{
		m_bPlayUseDenySound = false;

		if (IsSuitEquipped())
		{
			EmitSound("HL2Player.UseDeny");
		}
	}
}

// Set the activity based on an event or current state
void CVancePlayer::SetAnimation(PLAYER_ANIM playerAnim)
{
	float speed = GetAbsVelocity().Length2D();

	if (GetFlags() & (FL_FROZEN | FL_ATCONTROLS))
	{
		speed = 0;
		playerAnim = PLAYER_IDLE;
	}

	Activity idealActivity = ACT_HL2MP_RUN;

	if (playerAnim == PLAYER_JUMP)
	{
		if (HasWeapons())
			idealActivity = ACT_HL2MP_JUMP;
		else
			idealActivity = ACT_JUMP;
	}
	else if (playerAnim == PLAYER_DIE)
	{
		if (m_lifeState == LIFE_ALIVE)
		{
			return;
		}
	}
	else if (playerAnim == PLAYER_ATTACK1)
	{
		if (GetActivity() == ACT_HOVER ||
			GetActivity() == ACT_SWIM ||
			GetActivity() == ACT_HOP ||
			GetActivity() == ACT_LEAP ||
			GetActivity() == ACT_DIESIMPLE)
		{
			idealActivity = GetActivity();
		}
		else
		{
			idealActivity = ACT_HL2MP_GESTURE_RANGE_ATTACK;
		}
	}
	else if (playerAnim == PLAYER_RELOAD)
	{
		idealActivity = ACT_HL2MP_GESTURE_RELOAD;
	}
	else if (playerAnim == PLAYER_IDLE || playerAnim == PLAYER_WALK)
	{
		if (!(GetFlags() & FL_ONGROUND) && (GetActivity() == ACT_HL2MP_JUMP || GetActivity() == ACT_JUMP))    // Still jumping
		{
			idealActivity = GetActivity();
		}
		else if (GetWaterLevel() > 1)
		{
			if (speed == 0)
			{
				if (HasWeapons())
					idealActivity = ACT_HL2MP_IDLE;
				else
					idealActivity = ACT_IDLE;
			}
			else
			{
				if (HasWeapons())
					idealActivity = ACT_HL2MP_RUN;
				else
					idealActivity = ACT_RUN;
			}
		}
		else
		{
			if (GetFlags() & FL_DUCKING)
			{
				if (speed > 0)
				{
					if (HasWeapons())
						idealActivity = ACT_HL2MP_WALK_CROUCH;
					else
						idealActivity = ACT_WALK_CROUCH;
				}
				else
				{
					if (HasWeapons())
						idealActivity = ACT_HL2MP_IDLE_CROUCH;
					else
						idealActivity = ACT_COVER_LOW;
				}
			}
			else
			{
				if (speed > 0)
				{
					if (HasWeapons())
						idealActivity = ACT_HL2MP_RUN;
					else
						idealActivity = ACT_WALK;
				}
				else
				{
					if (HasWeapons())
						idealActivity = ACT_HL2MP_IDLE;
					else
						idealActivity = ACT_IDLE;
				}
			}
		}

		//idealActivity = TranslateTeamActivity( idealActivity );
	}

	if (IsInAVehicle())
	{
		idealActivity = ACT_COVER_LOW;
	}

	int animDesired;
	if (idealActivity == ACT_HL2MP_GESTURE_RANGE_ATTACK)
	{
		RestartGesture(Weapon_TranslateActivity(idealActivity));

		// FIXME: this seems a bit wacked
		Weapon_SetActivity(Weapon_TranslateActivity(ACT_RANGE_ATTACK1), 0);

		return;
	}
	else if (idealActivity == ACT_HL2MP_GESTURE_RELOAD)
	{
		RestartGesture(Weapon_TranslateActivity(idealActivity));
		return;
	}
	else
	{
		SetActivity(idealActivity);

		animDesired = SelectWeightedSequence(Weapon_TranslateActivity(idealActivity));

		if (animDesired == -1)
		{
			animDesired = SelectWeightedSequence(idealActivity);

			if (animDesired == -1)
			{
				animDesired = 0;
			}
		}

		// Already using the desired animation?
		if (GetSequence() == animDesired)
			return;

		m_flPlaybackRate = 1.0;
		ResetSequence(animDesired);
		SetCycle(0);
		return;
	}

	// Already using the desired animation?
	if (GetSequence() == animDesired)
		return;

	//Msg( "Set animation to %d\n", animDesired );
	// Reset to first frame of desired animation
	ResetSequence(animDesired);
	SetCycle(0);
}

ConVar vance_slide_velocity("vance_slide_velocity", "865", FCVAR_CHEAT);

void CVancePlayer::HandleSlide(void)
{
	m_ParkourAction = ACTION_NONE;
}

void CVancePlayer::HandleVault(void)
{
	m_ParkourAction = ACTION_NONE;
}

void CVancePlayer::HandleLedgeClimb(void)
{
	Vector forward;
	QAngle angle = GetAbsAngles();
	AngleVectors(QAngle(0, angle.y, angle.z), &forward);

	if (m_flClimbFraction >= 1.0f)
	{
		Interpolator_CurveInterpolate(INTERPOLATE_CATMULL_ROM,
			m_vecClimbStartOrigin -Vector(0, 0, VEC_HULL_MAX.z) * 2, // pre
			m_vecClimbStartOrigin, m_vecClimbDesiredOrigin, // start end
			m_vecClimbStartOrigin - Vector(0, 0, VEC_HULL_MAX.z) * 2, // next
			0.9f,
			m_vecClimbOutVelocity);

		m_vecClimbOutVelocity = (m_vecClimbOutVelocity - m_vecClimbDesiredOrigin) * -10;

		m_ParkourAction = ACTION_NONE;
		m_vecClimbStartOrigin = vec3_origin;
		m_vecClimbCurrentOrigin = vec3_origin;
		m_vecClimbDesiredOrigin = vec3_origin;

		SetSolid(SOLID_BBOX);
		SetMoveType(MOVETYPE_WALK);
		SetAbsVelocity(m_vecClimbOutVelocity);
	}
	else
	{
		m_flClimbFraction += CLIMB_LERPSPEED;
		Interpolator_CurveInterpolate(INTERPOLATE_CATMULL_ROM,
			m_vecClimbStartOrigin - Vector(0, 0, VEC_HULL_MAX.z) * 2, // pre
			m_vecClimbStartOrigin, m_vecClimbDesiredOrigin, // start end
			m_vecClimbStartOrigin - Vector(0, 0, VEC_HULL_MAX.z) * 2, // next
			m_flClimbFraction,
			m_vecClimbCurrentOrigin);

		if (vance_climb_debug.GetBool())
		{
			debugoverlay->AddBoxOverlay(m_vecClimbStartOrigin, VEC_HULL_MIN, VEC_HULL_MAX, vec3_angle, 255, 0, 0, 1, 10);
			debugoverlay->AddBoxOverlay(m_vecClimbDesiredOrigin, VEC_HULL_MIN, VEC_HULL_MAX, vec3_angle, 0, 255, 0, 1, 10);
		}
		//m_vecClimbCurrentOrigin = Lerp<Vector>(m_flClimbFraction, m_vecClimbStartOrigin, m_vecClimbDesiredOrigin);
		SetAbsOrigin(m_vecClimbCurrentOrigin);
		SetAbsVelocity(vec3_origin);
	}
}

// Returns true on failure, false on success
// it was done this way so we can use it in a loop
bool CVancePlayer::StartLedgeClimb(Vector start)
{
	Vector forward;
	EyeVectors(&forward);

	trace_t tr;
	UTIL_TraceHull(start + Vector(0,0,VEC_HULL_MAX.z),
				   start - Vector(0, 0, VEC_HULL_MAX.z) * 2,
		VEC_HULL_MIN, VEC_HULL_MAX, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);

	UTIL_TraceHull(tr.endpos,
		tr.endpos,
		VEC_HULL_MIN, VEC_HULL_MAX, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);
	if (tr.DidHit())
		return true;

	if (vance_climb_debug.GetBool())
	{
		debugoverlay->AddBoxOverlay(start + Vector(0, 0, VEC_HULL_MAX.z), VEC_HULL_MIN, VEC_HULL_MAX, vec3_angle, 0, 0, 255, 100, 10);
		debugoverlay->AddBoxOverlay(start - Vector(0, 0, VEC_HULL_MAX.z) * 2, VEC_HULL_MIN, VEC_HULL_MAX, vec3_angle, 0, 0, 255, 100, 10);

		debugoverlay->AddLineOverlay(start + Vector(0, 0, VEC_HULL_MAX.z), start - Vector(0, 0, VEC_HULL_MAX.z) * 2, 0, 0, 255, true, 10);
	}

	m_vecClimbDesiredOrigin = tr.endpos + Vector(0, 0, 5.0f);
	m_vecClimbStartOrigin = GetAbsOrigin();
	m_flClimbFraction = 0.0f;
	m_ParkourAction = ACTION_CLIMB;

	// Don't get stuck during this traversal since we'll just be slamming the player origin
	SetMoveType(MOVETYPE_NOCLIP);
	SetMoveCollide(MOVECOLLIDE_DEFAULT);
	SetSolid(SOLID_NONE);

	return false;
}

void CVancePlayer::Think(void)
{
	if (GetGroundEntity()) // if we're on the ground
	{
		/*if ((m_nButtons & IN_JUMP) && (GetAbsOrigin().z + tr.m_pEnt->WorldAlignMaxs().z) >= 36)
		{
			m_ParkourAction = ACTION_VAULT;
			SetNextThink(gpGlobals->curtime + 0.05f);
			return;
		}

		if ((IsSprinting()) && (m_nButtons & IN_DUCK) && (m_ParkourAction != ACTION_SLIDE))
		{
			m_ParkourAction = ACTION_SLIDE;
			//m_flSprintElapsedTime = 0.0f;
			SetNextThink(gpGlobals->curtime + 0.05f);
			return;
		}*/
	}

	if ((m_nButtons & IN_JUMP) && m_ParkourAction == ACTION_NONE)
	{
		Vector forward;
		QAngle angDir = QAngle(0.0f, EyeAngles().y, EyeAngles().z);
		AngleVectors(angDir, &forward);

		Vector eyeCrouchOrigin = VEC_DUCK_VIEW + GetAbsOrigin();

		Vector vStart = eyeCrouchOrigin;
		Vector vEnd = vStart + (forward * CLIMB_TRACE_DIST);

		trace_t tr;
		UTIL_TraceLine(vStart, vEnd, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);

		if (!tr.DidHitWorld())
		{
			SetNextThink(gpGlobals->curtime + 0.05f);
			return;
		}

		if (vance_climb_debug.GetBool())
		{
			debugoverlay->AddLineOverlay(vStart, tr.endpos, 0, 0, 255, true, 10);
			debugoverlay->AddLineOverlay(vStart, vEnd, 255, 0, 0, true, 10);
		}

		m_flClimbTraceFraction = tr.fraction;

		for (float posFraction = 0.0f; StartLedgeClimb(tr.endpos) && posFraction <= 1.0f; posFraction += 1.0f / vance_climb_checkray_count.GetInt())
		{
			vStart = eyeCrouchOrigin + (VEC_VIEW - VEC_DUCK_VIEW) * posFraction;
			vEnd = vStart + (forward * CLIMB_TRACE_DIST);
			UTIL_TraceLine(vStart, vEnd, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);

			if (vance_climb_debug.GetBool())
			{
				debugoverlay->AddLineOverlay(vStart, tr.endpos, 0, 0, 255, true, 10);
			}
		}
	}

	SetNextThink(gpGlobals->curtime + 0.05f);
	return;
}
