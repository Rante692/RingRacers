// DR. ROBOTNIK'S RING RACERS
//-----------------------------------------------------------------------------
// Copyright (C) 2022 by Sally "TehRealSalt" Cochenour
// Copyright (C) 2022 by Kart Krew
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  ufo.c
/// \brief Special Stage UFO + Emerald handler

#include "../doomdef.h"
#include "../doomstat.h"
#include "../info.h"
#include "../k_kart.h"
#include "../k_objects.h"
#include "../m_random.h"
#include "../p_local.h"
#include "../r_main.h"
#include "../s_sound.h"
#include "../g_game.h"
#include "../z_zone.h"
#include "../k_waypoint.h"
#include "../k_specialstage.h"
#include "../r_skins.h"

#define UFO_BASE_SPEED (42 * FRACUNIT) // UFO's slowest speed.
#define UFO_SPEEDUP (FRACUNIT >> 1) // Acceleration
#define UFO_SLOWDOWN (FRACUNIT >> 1) // Deceleration
#define UFO_SPACING (768 * FRACUNIT) // How far the UFO wants to stay in front
#define UFO_DEADZONE (2048 * FRACUNIT) // Deadzone where it won't update it's speed as much.
#define UFO_SPEEDFACTOR (FRACUNIT * 3 / 4) // Factor of player's best speed, to make it more fair.
#define UFO_DAMAGED_SPEED (UFO_BASE_SPEED >> 1) // Speed to add when UFO takes damage.
#define UFO_START_SPEED (UFO_BASE_SPEED << 1) // Speed when the map starts.

#define UFO_NUMARMS (3)
#define UFO_ARMDELTA (ANGLE_MAX / UFO_NUMARMS)

#define ufo_waypoint(o) ((o)->extravalue1)
#define ufo_distancetofinish(o) ((o)->extravalue2)
#define ufo_speed(o) ((o)->watertop)
#define ufo_collectdelay(o) ((o)->threshold)

#define ufo_pieces(o) ((o)->hnext)

#define ufo_piece_type(o) ((o)->extravalue1)

#define ufo_piece_owner(o) ((o)->target)
#define ufo_piece_next(o) ((o)->hnext)
#define ufo_piece_prev(o) ((o)->hprev)

enum
{
	UFO_PIECE_TYPE_POD,
	UFO_PIECE_TYPE_ARM,
	UFO_PIECE_TYPE_STEM,
};

static sfxenum_t hums[16] = {sfx_claw01, sfx_claw02, sfx_claw03, sfx_claw04, sfx_claw05, sfx_claw06, sfx_claw07, sfx_claw08, sfx_claw09, sfx_claw10, sfx_claw11, sfx_claw12, sfx_claw13, sfx_claw14, sfx_claw15, sfx_claw16};
static int maxhum = sizeof(hums) / sizeof(hums[0]) - 1;

static void SpawnUFOSpeedLines(mobj_t *ufo)
{
	mobj_t *fast = P_SpawnMobjFromMobj(ufo,
		P_RandomRange(PR_DECORATION, -120, 120) * FRACUNIT,
		P_RandomRange(PR_DECORATION, -120, 120) * FRACUNIT,
		(ufo->info->height / 2) + (P_RandomRange(PR_DECORATION, -24, 24) * FRACUNIT),
		MT_FASTLINE
	);

	fast->scale *= 3;

	P_SetTarget(&fast->target, ufo);
	fast->angle = K_MomentumAngle(ufo);

	fast->color = SKINCOLOR_WHITE;
	fast->colorized = true;

	K_MatchGenericExtraFlags(fast, ufo);
}

static void SpawnEmeraldSpeedLines(mobj_t *mo)
{
	mobj_t *fast = P_SpawnMobjFromMobj(mo,
		P_RandomRange(PR_DECORATION, -48, 48) * FRACUNIT,
		P_RandomRange(PR_DECORATION, -48, 48) * FRACUNIT,
		P_RandomRange(PR_DECORATION, 0, 64) * FRACUNIT,
		MT_FASTLINE);
	P_SetMobjState(fast, S_KARTINVLINES1);

	P_SetTarget(&fast->target, mo);
	fast->angle = K_MomentumAngle(mo);

	fast->momx = 3*mo->momx/4;
	fast->momy = 3*mo->momy/4;
	fast->momz = 3*P_GetMobjZMovement(mo)/4;

	K_MatchGenericExtraFlags(fast, mo);
	K_ReduceVFX(fast, mo->player);

	fast->color = mo->color;
	fast->colorized = true;
}

static void UFOMoveTo(mobj_t *ufo, fixed_t destx, fixed_t desty, fixed_t destz)
{
	ufo->momx = destx - ufo->x;
	ufo->momy = desty - ufo->y;
	ufo->momz = destz - ufo->z;
}

static fixed_t GenericDistance(
	fixed_t curx, fixed_t cury, fixed_t curz,
	fixed_t destx, fixed_t desty, fixed_t destz)
{
	return P_AproxDistance(P_AproxDistance(destx - curx, desty - cury), destz - curz);
}

static boolean UFOEmeraldChase(mobj_t *ufo)
{
	return (ufo->health <= 1);
}

static boolean UFOPieceValid(mobj_t *piece)
{
	return (piece != NULL && P_MobjWasRemoved(piece) == false && piece->health > 0);
}

static void UFOUpdateDistanceToFinish(mobj_t *ufo)
{
	waypoint_t *finishLine = K_GetFinishLineWaypoint();
	waypoint_t *nextWaypoint = K_GetWaypointFromIndex((size_t)ufo_waypoint(ufo));

	if (nextWaypoint != NULL && finishLine != NULL)
	{
		const boolean useshortcuts = false;
		const boolean huntbackwards = false;
		boolean pathfindsuccess = false;
		path_t pathtofinish = {0};

		pathfindsuccess =
			K_PathfindToWaypoint(nextWaypoint, finishLine, &pathtofinish, useshortcuts, huntbackwards);

		// Update the UFO's distance to the finish line if a path was found.
		if (pathfindsuccess == true)
		{
			// Add euclidean distance to the next waypoint to the distancetofinish
			UINT32 adddist;
			fixed_t disttowaypoint =
				P_AproxDistance(
					(ufo->x >> FRACBITS) - (nextWaypoint->mobj->x >> FRACBITS),
					(ufo->y >> FRACBITS) - (nextWaypoint->mobj->y >> FRACBITS));
			disttowaypoint = P_AproxDistance(disttowaypoint, (ufo->z >> FRACBITS) - (nextWaypoint->mobj->z >> FRACBITS));

			adddist = (UINT32)disttowaypoint;

			ufo_distancetofinish(ufo) = pathtofinish.totaldist + adddist;
			Z_Free(pathtofinish.array);
		}
	}
}

static void UFOUpdateSpeed(mobj_t *ufo)
{
	const fixed_t baseSpeed = FixedMul(UFO_BASE_SPEED, K_GetKartGameSpeedScalar(gamespeed));
	const UINT32 spacing = FixedMul(FixedMul(UFO_SPACING, mapobjectscale), K_GetKartGameSpeedScalar(gamespeed)) >> FRACBITS;
	const UINT32 deadzone = FixedMul(FixedMul(UFO_DEADZONE, mapobjectscale), K_GetKartGameSpeedScalar(gamespeed)) >> FRACBITS;

	// Best values of all of the players.
	UINT32 bestDist = UINT32_MAX;
	fixed_t bestSpeed = 0;

	// Desired values for the UFO itself.
	UINT32 wantedDist = UINT32_MAX;
	fixed_t wantedSpeed = ufo_speed(ufo);
	fixed_t speedDelta = 0;

	UINT8 i;

	for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *player = NULL;

		if (playeringame[i] == false)
		{
			continue;
		}

		player = &players[i];
		if (player->spectator == true)
		{
			continue;
		}

		if (player->mo == NULL || P_MobjWasRemoved(player->mo) == true)
		{
			continue;
		}

		if (player->distancetofinish < bestDist)
		{
			bestDist = player->distancetofinish;

			// Doesn't matter if a splitscreen player behind is moving faster behind the one most caught up.
			bestSpeed = R_PointToDist2(0, 0, player->rmomx, player->rmomy);
			bestSpeed = min(bestSpeed, K_GetKartSpeed(player, false, false)); // Don't become unfair with Sneakers.
			bestSpeed = FixedDiv(bestSpeed, mapobjectscale); // Unscale from mapobjectscale to FRACUNIT
			bestSpeed = FixedMul(bestSpeed, UFO_SPEEDFACTOR); // Make it a bit more lenient
		}
	}

	if (bestDist == UINT32_MAX)
	{
		// Invalid, lets go back to base speed.
		wantedSpeed = baseSpeed;
	}
	else
	{
		INT32 distDelta = 0;

		if (bestDist > spacing)
		{
			wantedDist = bestDist - spacing;
		}
		else
		{
			wantedDist = 0;
		}

		distDelta = ufo_distancetofinish(ufo) - wantedDist;

		if (distDelta > 0)
		{
			// Too far behind! Start speeding up!
			wantedSpeed = max(bestSpeed, baseSpeed << 2);
		}
		else
		{
			if (abs(distDelta) <= deadzone)
			{
				// We're in a good spot, try to match the player.
				wantedSpeed = max(bestSpeed >> 1, baseSpeed);
			}
			else
			{
				// Too far ahead! Start slowing down!
				wantedSpeed = baseSpeed;
			}
		}
	}

	// Slowly accelerate or decelerate to
	// get to our desired speed.
	speedDelta = wantedSpeed - ufo_speed(ufo);
	if (speedDelta > 0)
	{
		if (abs(speedDelta) <= UFO_SPEEDUP)
		{
			ufo_speed(ufo) = wantedSpeed;
		}
		else
		{
			ufo_speed(ufo) += UFO_SPEEDUP;
		}

		// these number are primarily vibes based and not empirically derived
		if (UFOEmeraldChase(ufo))
		{
			if (ufo_speed(ufo) > 50*FRACUNIT)
				SpawnEmeraldSpeedLines(ufo);
		}
		else if (ufo_speed(ufo) > 70*FRACUNIT && !S_SoundPlaying(ufo, sfx_clawzm))
		{			
			S_StartSound(ufo, sfx_clawzm);
		}
	}
	else if (speedDelta < 0)
	{
		if (abs(speedDelta) <= UFO_SLOWDOWN)
		{
			ufo_speed(ufo) = wantedSpeed;
		}
		else
		{
			ufo_speed(ufo) -= UFO_SLOWDOWN;
		}
	}
}

static void UFOUpdateAngle(mobj_t *ufo)
{
	angle_t dest = K_MomentumAngle(ufo);
	INT32 delta = AngleDeltaSigned(ufo->angle, dest);
	ufo->angle += delta >> 2;
}

waypoint_t *K_GetSpecialUFOWaypoint(mobj_t *ufo)
{
	if ((ufo == NULL) && (specialstageinfo.valid == true))
	{
		ufo = specialstageinfo.ufo;
	}

	if (ufo != NULL && P_MobjWasRemoved(ufo) == false
		&& ufo->type == MT_SPECIAL_UFO)
	{
		if (ufo_waypoint(ufo) >= 0)
		{
			return K_GetWaypointFromIndex((size_t)ufo_waypoint(ufo));
		}
	}

	return NULL;
}

static void UFOMove(mobj_t *ufo)
{
	waypoint_t *curWaypoint = NULL;
	waypoint_t *destWaypoint = NULL;

	fixed_t distLeft = INT32_MAX;
	fixed_t newX = ufo->x;
	fixed_t newY = ufo->y;
	fixed_t newZ = ufo->z;
	const fixed_t floatHeight = 24 * ufo->scale;

	const boolean useshortcuts = false;
	const boolean huntbackwards = false;
	boolean pathfindsuccess = false;
	path_t pathtofinish = {0};
	size_t pathIndex = 0;

	boolean reachedEnd = false;

	curWaypoint = K_GetSpecialUFOWaypoint(ufo);
	destWaypoint = K_GetFinishLineWaypoint();

	if (curWaypoint == NULL || destWaypoint == NULL)
	{
		// Waypoints aren't valid.
		// Just go straight up.
		// :japanese_ogre: : "Abrupt and funny is the funniest way to end the special stage anyways"
		ufo->momx = 0;
		ufo->momy = 0;
		ufo->momz = ufo_speed(ufo);
		return;
	}

	distLeft = FixedMul(ufo_speed(ufo), mapobjectscale);

	while (distLeft > 0)
	{
		fixed_t wpX = curWaypoint->mobj->x;
		fixed_t wpY = curWaypoint->mobj->y;
		fixed_t wpZ = curWaypoint->mobj->z + floatHeight;

		fixed_t distToNext = GenericDistance(
			newX, newY, newZ,
			wpX, wpY, wpZ
		);

		if (distToNext > distLeft)
		{
			// Only made it partially there.
			newX += FixedMul(FixedDiv(wpX - newX, distToNext), distLeft);
			newY += FixedMul(FixedDiv(wpY - newY, distToNext), distLeft);
			newZ += FixedMul(FixedDiv(wpZ - newZ, distToNext), distLeft);

			distLeft = 0;
		}
		else
		{
			// Close enough to the next waypoint,
			// move there and remove the distance.
			newX = wpX;
			newY = wpY;
			newZ = wpZ;

			distLeft -= distToNext;

			if (curWaypoint == destWaypoint)
			{
				// Reached the end.
				reachedEnd = true;
				break;
			}

			// Create waypoint path to our destination.
			// Crazy over-engineered, just to catch when
			// waypoints are insanely close to each other :P
			if (pathfindsuccess == false)
			{
				pathfindsuccess = K_PathfindToWaypoint(
					curWaypoint, destWaypoint,
					&pathtofinish,
					useshortcuts, huntbackwards
				);

				if (pathfindsuccess == false)
				{
					// Path isn't valid.
					// Just keep going.
					break;
				}
			}

			pathIndex++;

			if (pathIndex >= pathtofinish.numnodes)
			{
				// Successfully reached the end of the path.
				reachedEnd = true;
				break;
			}

			// Now moving to the next waypoint.
			curWaypoint = (waypoint_t *)pathtofinish.array[pathIndex].nodedata;
			ufo_waypoint(ufo) = (INT32)K_GetWaypointHeapIndex(curWaypoint);
		}
	}

	UFOMoveTo(ufo, newX, newY, newZ);

	if (reachedEnd == true)
	{
		UINT8 i;

		// Invalidate UFO/emerald
		ufo_waypoint(ufo) = -1;
		ufo->flags &= ~(MF_SPECIAL|MF_PICKUPFROMBELOW);

		// Disable player
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i])
				continue;
			if (players[i].spectator)
				continue;

			players[i].pflags |= PF_NOCONTEST;
			P_DoPlayerExit(&players[i]);
		}
	}

	if (pathfindsuccess == true)
	{
		Z_Free(pathtofinish.array);
	}
}

static void UFOEmeraldVFX(mobj_t *ufo)
{
	const INT32 bobS = 32;
	const angle_t bobA = (leveltime & (bobS - 1)) * (ANGLE_MAX / bobS);
	const fixed_t bobH = 16 * ufo->scale;

	ufo->sprzoff = FixedMul(bobH, FINESINE(bobA >> ANGLETOFINESHIFT));

	if (leveltime % 3 == 0)
	{
		mobj_t *sparkle = P_SpawnMobjFromMobj(
			ufo,
			P_RandomRange(PR_SPARKLE, -48, 48) * FRACUNIT,
			P_RandomRange(PR_SPARKLE, -48, 48) * FRACUNIT,
			(P_RandomRange(PR_SPARKLE, 0, 64) * FRACUNIT) + FixedDiv(ufo->sprzoff, ufo->scale),
			MT_EMERALDSPARK
		);

		sparkle->color = ufo->color;
		sparkle->momz += 8 * ufo->scale * P_MobjFlip(ufo);
	}
}

static boolean UFOHumPlaying(mobj_t *ufo) {
	INT32 i;
	for (i = 0; i <= maxhum; i++)
	{
		if (S_SoundPlaying(ufo, hums[i]))
			return true;
	}
	return false;
}

static void UFOUpdateSound(mobj_t *ufo) {
	INT32 maxhealth = max(mobjinfo[MT_SPECIAL_UFO].spawnhealth, 1);
	INT32 healthlevel = maxhum * ufo->health / maxhealth;

	if (!UFOEmeraldChase(ufo) && !UFOHumPlaying(ufo))
	{
		healthlevel = min(max(healthlevel, 1), maxhum);
		S_StartSound(ufo, hums[maxhum - healthlevel]);
	}
}

void Obj_SpecialUFOThinker(mobj_t *ufo)
{
	UFOMove(ufo);
	UFOUpdateAngle(ufo);
	UFOUpdateDistanceToFinish(ufo);
	UFOUpdateSpeed(ufo);
	UFOUpdateSound(ufo);

	if (UFOEmeraldChase(ufo) == true)
	{
		// Spawn emerald sparkles
		UFOEmeraldVFX(ufo);
		ufo_collectdelay(ufo)--;
	}
	else
	{
		ufo_collectdelay(ufo) = TICRATE;
	}
}

static void UFOCopyHitlagToPieces(mobj_t *ufo)
{
	mobj_t *piece = NULL;

	piece = ufo_pieces(ufo);
	while (UFOPieceValid(piece) == true)
	{
		piece->hitlag = ufo->hitlag;
		piece->eflags = (piece->eflags & ~MFE_DAMAGEHITLAG) | (ufo->eflags & MFE_DAMAGEHITLAG);
		piece = ufo_piece_next(piece);
	}
}

static void UFOKillPiece(mobj_t *piece)
{
	angle_t dir = ANGLE_MAX;
	fixed_t thrust = 0;

	if (UFOPieceValid(piece) == false)
	{
		return;
	}

	piece->health = 0;
	piece->tics = TICRATE;
	piece->flags &= ~MF_NOGRAVITY;

	switch (ufo_piece_type(piece))
	{
		case UFO_PIECE_TYPE_STEM:
		{
			piece->tics = 1;
			return;
		}
		case UFO_PIECE_TYPE_ARM:
		{
			dir = piece->angle;
			thrust = 12 * piece->scale;
			break;
		}
		default:
		{
			dir = FixedAngle(P_RandomRange(PR_DECORATION, 0, 359) << FRACBITS);
			thrust = 4 * piece->scale;
			break;
		}
	}

	P_Thrust(piece, dir, -thrust);
	P_SetObjectMomZ(piece, 12*FRACUNIT, true);
}

static void UFOKillPieces(mobj_t *ufo)
{
	mobj_t *piece = NULL;

	piece = ufo_pieces(ufo);
	while (UFOPieceValid(piece) == true)
	{
		UFOKillPiece(piece);
		piece = ufo_piece_next(piece);
	}
}

static UINT8 GetUFODamage(mobj_t *inflictor, UINT8 damageType)
{
	if (inflictor != NULL && P_MobjWasRemoved(inflictor) == false)
	{
		switch (inflictor->type)
		{
			case MT_JAWZ_SHIELD:
			case MT_ORBINAUT_SHIELD:
			{
				// Shields deal chip damage.
				return 10;
			}
			case MT_JAWZ:
			{
				// Thrown Jawz deal a bit extra.
				return 15;
			}
			case MT_ORBINAUT:
			{
				// Thrown orbinauts deal double damage.
				return 20;
			}
			case MT_SPB:
			{
				// SPB deals triple damage.
				return 30;
			}
			case MT_BANANA:
			{
				// Banana snipes deal triple damage,
				// laid down bananas deal regular damage.
				if (inflictor->health > 1)
				{
					return 30;
				}

				return 10;
			}
			case MT_PLAYER:
			{
				// Players deal damage relative to how many sneakers they used.
				return 15 * max(1, inflictor->player->numsneakers);
			}
			default:
			{
				break;
			}
		}
	}

	// Guess from damage type.
	switch (damageType & DMG_TYPEMASK)
	{
		case DMG_NORMAL:
		case DMG_STING:
		default:
		{
			return 10;
		}
		case DMG_WIPEOUT:
		{
			return 20;
		}
		case DMG_EXPLODE:
		case DMG_TUMBLE:
		{
			return 30;
		}
		case DMG_VOLTAGE:
		{
			return 15;
		}
	}
}

boolean Obj_SpecialUFODamage(mobj_t *ufo, mobj_t *inflictor, mobj_t *source, UINT8 damageType)
{
	const fixed_t addSpeed = FixedMul(UFO_DAMAGED_SPEED, K_GetKartGameSpeedScalar(gamespeed));
	UINT8 damage = 1;

	(void)source;

	if (UFOEmeraldChase(ufo) == true)
	{
		// Damaged fully already, no need for any more.
		return false;
	}

	damage = GetUFODamage(inflictor, damageType);

	if (damage <= 0)
	{
		return false;
	}

	if (source->player)
	{
		UINT32 skinflags = (demo.playback)
			? demo.skinlist[demo.currentskinid[(source->player-players)]].flags
			: skins[source->player->skin].flags;
		if (skinflags & SF_IRONMAN)
			SetRandomFakePlayerSkin(source->player, true);
	}


	// Speed up on damage!
	ufo_speed(ufo) += addSpeed;

	K_SetHitLagForObjects(ufo, inflictor, (damage / 3) + 2, true);
	UFOCopyHitlagToPieces(ufo);

	if (damage >= ufo->health - 1)
	{
		// Destroy the UFO parts, and make the emerald collectible!
		UFOKillPieces(ufo);

		ufo->health = 1;
		ufo->flags = (ufo->flags & ~MF_SHOOTABLE) | (MF_SPECIAL|MF_PICKUPFROMBELOW);
		ufo->shadowscale = FRACUNIT/3;

		P_LinedefExecute(LE_PINCHPHASE, ufo, NULL);

		S_StopSound(ufo);
		S_StartSound(ufo, sfx_clawk2);
		P_StartQuake(64<<FRACBITS, 20);

		ufo_speed(ufo) += addSpeed; // Even more speed!
		return true;
	}

	S_StartSound(ufo, sfx_clawht);
	S_StopSoundByID(ufo, sfx_clawzm);
	P_StartQuake(64<<FRACBITS, 10);
	ufo->health -= damage;

	return true;
}

void Obj_PlayerUFOCollide(mobj_t *ufo, mobj_t *other)
{
	if (other->player == NULL)
	{
		return;
	}

	if ((other->player->sneakertimer > 0)
		&& !P_PlayerInPain(other->player)
		&& (other->player->flashing == 0))
	{
		// Bump and deal damage.
		Obj_SpecialUFODamage(ufo, other, other, DMG_STEAL);
		other->player->sneakertimer = 0;
	}
	else
	{
		const angle_t moveAngle = K_MomentumAngle(ufo);
		const angle_t clipAngle = R_PointToAngle2(ufo->x, ufo->y, other->x, other->y);

		if (other->z > ufo->z + ufo->height)
		{
			return; // overhead
		}

		if (other->z + other->height < ufo->z)
		{
			return; // underneath
		}

		if (AngleDelta(moveAngle, clipAngle) < ANG60)
		{
			// in front
			K_StumblePlayer(other->player);
		}
	}

	K_KartBouncing(other, ufo);
}

void Obj_UFOPieceThink(mobj_t *piece)
{
	mobj_t *ufo = ufo_piece_owner(piece);

	if (ufo == NULL || P_MobjWasRemoved(ufo) == true)
	{
		P_KillMobj(piece, NULL, NULL, DMG_NORMAL);
		return;
	}

	piece->destscale = 3 * ufo->destscale / 2;
	piece->scalespeed = ufo->scalespeed;

	switch (ufo_piece_type(piece))
	{
		case UFO_PIECE_TYPE_POD:
		{
			UFOMoveTo(piece, ufo->x, ufo->y, ufo->z + (132 * piece->scale));
			if (S_SoundPlaying(ufo, sfx_clawzm) && ufo_speed(ufo) > 70*FRACUNIT)
				SpawnUFOSpeedLines(piece);
			break;
		}
		case UFO_PIECE_TYPE_ARM:
		{
			fixed_t dis = (88 * piece->scale);

			fixed_t x = ufo->x - FixedMul(dis, FINECOSINE(piece->angle >> ANGLETOFINESHIFT));
			fixed_t y = ufo->y - FixedMul(dis, FINESINE(piece->angle >> ANGLETOFINESHIFT));

			UFOMoveTo(piece, x, y, ufo->z + (24 * piece->scale));

			piece->angle -= FixedMul(ANG2, FixedDiv(ufo_speed(ufo), UFO_BASE_SPEED));
			break;
		}
		case UFO_PIECE_TYPE_STEM:
		{
			fixed_t stemZ = ufo->z + (294 * piece->scale);
			fixed_t sc = FixedDiv(FixedDiv(ufo->ceilingz - stemZ, piece->scale), 15 * FRACUNIT);

			UFOMoveTo(piece, ufo->x, ufo->y, stemZ);
			if (sc > 0)
			{
				piece->spriteyscale = sc;
			}
			break;
		}
		default:
		{
			P_RemoveMobj(piece);
			return;
		}
	}
}

void Obj_UFOPieceDead(mobj_t *piece)
{
	piece->renderflags ^= RF_DONTDRAW;
}

void Obj_UFOPieceRemoved(mobj_t *piece)
{
	// Repair piece list.
	mobj_t *ufo = ufo_piece_owner(piece);
	mobj_t *next = ufo_piece_next(piece);
	mobj_t *prev = ufo_piece_prev(piece);

	if (prev != NULL && P_MobjWasRemoved(prev) == false)
	{
		P_SetTarget(
			&ufo_piece_next(prev),
			(next != NULL && P_MobjWasRemoved(next) == false) ? next : NULL
		);
	}

	if (next != NULL && P_MobjWasRemoved(next) == false)
	{
		P_SetTarget(
			&ufo_piece_prev(next),
			(prev != NULL && P_MobjWasRemoved(prev) == false) ? prev : NULL
		);
	}

	if (ufo != NULL && P_MobjWasRemoved(ufo) == false)
	{
		if (piece == ufo_pieces(ufo))
		{
			P_SetTarget(
				&ufo_pieces(ufo),
				(next != NULL && P_MobjWasRemoved(next) == false) ? next : NULL
			);
		}
	}

	P_SetTarget(&ufo_piece_next(piece), NULL);
	P_SetTarget(&ufo_piece_prev(piece), NULL);
}

static mobj_t *InitSpecialUFO(waypoint_t *start)
{
	mobj_t *ufo = NULL;
	mobj_t *overlay = NULL;
	mobj_t *piece = NULL;
	mobj_t *prevPiece = NULL;
	size_t i;

	if (start == NULL)
	{
		// Simply create at the origin with default values.
		ufo = P_SpawnMobj(0, 0, 0, MT_SPECIAL_UFO);
		ufo_waypoint(ufo) = -1; // Invalidate
		ufo_distancetofinish(ufo) = INT32_MAX;
	}
	else
	{
		// Create with a proper waypoint track!
		ufo = P_SpawnMobj(start->mobj->x, start->mobj->y, start->mobj->z, MT_SPECIAL_UFO);
		ufo_waypoint(ufo) = (INT32)K_GetWaypointHeapIndex(start);
		UFOUpdateDistanceToFinish(ufo);
	}

	ufo_speed(ufo) = FixedMul(UFO_START_SPEED, K_GetKartGameSpeedScalar(gamespeed));

	// TODO: Adjustable Special Stage emerald color
	ufo->color = SKINCOLOR_CHAOSEMERALD1;

	overlay = P_SpawnMobjFromMobj(ufo, 0, 0, 0, MT_OVERLAY);
	P_SetTarget(&overlay->target, ufo);
	overlay->color = ufo->color;

	// TODO: Super Emeralds / Chaos Rings
	P_SetMobjState(overlay, S_CHAOSEMERALD_UNDER);

	// Create UFO pieces.
	// First: UFO center.
	piece = P_SpawnMobjFromMobj(ufo, 0, 0, 0, MT_SPECIAL_UFO_PIECE);
	P_SetTarget(&ufo_piece_owner(piece), ufo);

	P_SetMobjState(piece, S_SPECIAL_UFO_POD);
	ufo_piece_type(piece) = UFO_PIECE_TYPE_POD;

	overlay = P_SpawnMobjFromMobj(piece, 0, 0, 0, MT_OVERLAY);
	P_SetTarget(&overlay->target, piece);
	P_SetMobjState(overlay, S_SPECIAL_UFO_OVERLAY);

	P_SetTarget(&ufo_pieces(ufo), piece);
	prevPiece = piece;

	// Add the catcher arms.
	for (i = 0; i < UFO_NUMARMS; i++)
	{
		piece = P_SpawnMobjFromMobj(ufo, 0, 0, 0, MT_SPECIAL_UFO_PIECE);
		P_SetTarget(&ufo_piece_owner(piece), ufo);

		P_SetMobjState(piece, S_SPECIAL_UFO_ARM);
		ufo_piece_type(piece) = UFO_PIECE_TYPE_ARM;

		piece->angle = UFO_ARMDELTA * i;

		P_SetTarget(&ufo_piece_next(prevPiece), piece);
		P_SetTarget(&ufo_piece_prev(piece), prevPiece);
		prevPiece = piece;
	}

	// Add the stem.
	piece = P_SpawnMobjFromMobj(ufo, 0, 0, 0, MT_SPECIAL_UFO_PIECE);
	P_SetTarget(&ufo_piece_owner(piece), ufo);

	P_SetMobjState(piece, S_SPECIAL_UFO_STEM);
	ufo_piece_type(piece) = UFO_PIECE_TYPE_STEM;

	P_SetTarget(&ufo_piece_next(prevPiece), piece);
	P_SetTarget(&ufo_piece_prev(piece), prevPiece);
	prevPiece = piece;

	return ufo;
}

mobj_t *Obj_CreateSpecialUFO(void)
{
	waypoint_t *finishWaypoint = K_GetFinishLineWaypoint();
	waypoint_t *startWaypoint = NULL;

	if (finishWaypoint != NULL)
	{
		const boolean huntbackwards = true;
		const boolean useshortcuts = false;
		const UINT32 traveldist = INT32_MAX; // Go as far back as possible. Not UINT32_MAX to avoid possible overflow.
		boolean pathfindsuccess = false;
		path_t pathtofinish = {0};

		pathfindsuccess = K_PathfindThruCircuit(
			finishWaypoint, traveldist,
			&pathtofinish,
			useshortcuts, huntbackwards
		);

		if (pathfindsuccess == true)
		{
			startWaypoint = (waypoint_t *)pathtofinish.array[ pathtofinish.numnodes - 1 ].nodedata;
			Z_Free(pathtofinish.array);
		}
	}

	return InitSpecialUFO(startWaypoint);
}

UINT32 K_GetSpecialUFODistance(void)
{
	if (specialstageinfo.valid == true)
	{
		if (specialstageinfo.ufo != NULL && P_MobjWasRemoved(specialstageinfo.ufo) == false)
		{
			return (UINT32)ufo_distancetofinish(specialstageinfo.ufo);
		}
	}

	return UINT32_MAX;
}
