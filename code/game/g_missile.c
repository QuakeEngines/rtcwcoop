/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein single player GPL Source Code (RTCW SP Source Code).  

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW SP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW SP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "g_local.h"

#define MISSILE_PRESTEP_TIME    50


extern void gas_think( gentity_t *gas );
extern void gas_touch( gentity_t *gas, gentity_t *other, trace_t *trace );
extern void SP_target_smoke( gentity_t *ent );



void G_ExplodeMissilePoisonGas( gentity_t *ent );
void M_think( gentity_t *ent );
/*
================
G_BounceMissile

================
*/
qboolean G_BounceMissile( gentity_t *ent, trace_t *trace ) {
	vec3_t velocity;
	float dot;
	int hitTime;
	int contents;       //----(SA)	added
/*
		// Ridah, if we are a grenade, and we have hit an AI that is waiting to catch us, give them a grenade, and delete ourselves
	if ((ent->splashMethodOfDeath == MOD_GRENADE_SPLASH) && (g_entities[trace->entityNum].flags & FL_AI_GRENADE_KICK) &&
		(trace->endpos[2] > g_entities[trace->entityNum].r.currentOrigin[2])) {
		g_entities[trace->entityNum].grenadeExplodeTime = ent->nextthink;
		g_entities[trace->entityNum].flags &= ~FL_AI_GRENADE_KICK;
		Add_Ammo( &g_entities[trace->entityNum], WP_GRENADE_LAUNCHER, 1, qfalse );	//----(SA)	modified
		G_FreeEntity( ent );
		return qfalse;
	}
*/
	contents = trap_PointContents( ent->s.origin, -1 );

	// reflect the velocity on the trace plane
	hitTime = level.previousTime + ( level.time - level.previousTime ) * trace->fraction;
	BG_EvaluateTrajectoryDelta( &ent->s.pos, hitTime, velocity );
	dot = DotProduct( velocity, trace->plane.normal );
	VectorMA( velocity, -2 * dot, trace->plane.normal, ent->s.pos.trDelta );

	// RF, record this for mover pushing
	if ( trace->plane.normal[2] > 0.2 /*&& VectorLength( ent->s.pos.trDelta ) < 40*/ ) {
		ent->s.groundEntityNum = trace->entityNum;
	}

	if ( ent->s.eFlags & EF_BOUNCE_HALF ) {
		if ( contents & MASK_WATER ) {
			// barely bounce underwater
			VectorScale( ent->s.pos.trDelta, 0.04f, ent->s.pos.trDelta );
		} else {
			if ( ent->s.eFlags & EF_BOUNCE ) {     // both flags marked, do a third type of bounce
				VectorScale( ent->s.pos.trDelta, 0.25, ent->s.pos.trDelta );
			} else {
				VectorScale( ent->s.pos.trDelta, 0.65, ent->s.pos.trDelta );
			}
		}

		// check for stop
		if ( trace->plane.normal[2] > 0.2 && VectorLength( ent->s.pos.trDelta ) < 40 ) {
//----(SA)	make the world the owner of the dynamite, so the player can shoot it after it stops moving
			if ( ent->s.weapon == WP_DYNAMITE ) {
				ent->r.ownerNum = ENTITYNUM_WORLD;

				// make shootable
				if ( g_gametype.integer <= GT_SINGLE_PLAYER ) {
					ent->health             = 5;
					ent->takedamage         = qtrue;

					// small target cube
					VectorSet( ent->r.mins, -4, -4, 0 );
					VectorCopy( ent->r.mins, ent->r.absmin );
					VectorSet( ent->r.maxs, 4, 4, 8 );
					VectorCopy( ent->r.maxs, ent->r.absmax );
				}

			}
//----(SA)	end
			G_SetOrigin( ent, trace->endpos );
			ent->s.time = level.time / 4;
			return qfalse;
		}
	}

	VectorAdd( ent->r.currentOrigin, trace->plane.normal, ent->r.currentOrigin );
	VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
	ent->s.pos.trTime = level.time;

	if ( contents & MASK_WATER ) {
		return qfalse;  // no bounce sound

	}
	return qtrue;
}

/*
================
G_MissileImpact
	impactDamage is how much damage the impact will do to func_explosives
================
*/
void G_MissileImpact( gentity_t *ent, trace_t *trace, int impactDamage, vec3_t dir ) {  //----(SA)	added 'dir'
	gentity_t       *other;
	qboolean hitClient = qfalse;
	vec3_t velocity;
	int etype;

	other = &g_entities[trace->entityNum];

	// DHM - Nerve :: Only in single player
	if ( g_gametype.integer <= GT_SINGLE_PLAYER ) {
		AICast_ProcessBullet( &g_entities[ent->r.ownerNum], g_entities[ent->r.ownerNum].s.pos.trBase, trace->endpos );
	}

	// handle func_explosives
	if ( other->classname && Q_stricmp( other->classname, "func_explosive" ) == 0 ) {
		// the damage is sufficient to "break" the ent (health == 0 is non-breakable)
		if ( other->health && impactDamage >= other->health ) {
			// check for other->takedamage needs to be inside the health check since it is
			// likely that, if successfully destroyed by the missile, in the next runmissile()
			// update takedamage would be set to '0' and the func_explosive would not be
			// removed yet, causing a bounce.
			if ( other->takedamage ) {
				BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, velocity );
				G_Damage( other, ent, &g_entities[ent->r.ownerNum], velocity, ent->s.origin, impactDamage, 0, ent->methodOfDeath );
			}

			// its possible of the func_explosive not to die from this and it
			// should reflect the missile or explode it not vanish into oblivion
			if ( other->health <= 0 ) {
				if ( other->s.frame != 1 ) { // playing death animation, still 'solid'
					return;
				}
			}
		}
	}

	// check for bounce
	if ( !other->takedamage &&
		 ( ent->s.eFlags & ( EF_BOUNCE | EF_BOUNCE_HALF ) ) ) {
		if ( G_BounceMissile( ent, trace ) && !trace->startsolid ) {  // no bounce, no bounce sound
			int flags = 0;
			if ( !Q_stricmp( ent->classname, "flamebarrel" ) ) {
				G_AddEvent( ent, EV_FLAMEBARREL_BOUNCE, 0 );
			} else {
				flags = trace->surfaceFlags;
				flags = ( flags >> 12 );    // shift right 12 so I can send in parm	(#define SURF_METAL 0x1000.  metal is lowest flag i need for sound)
				G_AddEvent( ent, EV_GRENADE_BOUNCE, flags );    //----(SA)	send surfaceflags for sound selection
			}
		}
		return;
	}

	if ( other->takedamage && ent->s.density == 1 ) {
		G_ExplodeMissilePoisonGas( ent );
		return;
	}

	// impact damage
	if ( other->takedamage ) { //&& Q_stricmp (ent->classname, "flamebarrel")) {
		// FIXME: wrong damage direction?
		if ( ent->damage ) {


			if ( LogAccuracyHit( other, &g_entities[ent->r.ownerNum] ) ) {
				if ( g_entities[ent->r.ownerNum].client ) {
					g_entities[ent->r.ownerNum].client->ps.persistant[PERS_ACCURACY_HITS]++;
				}
				hitClient = qtrue;
			}
			BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, velocity );
			if ( VectorLength( velocity ) == 0 ) {
				velocity[2] = 1;    // stepped on a grenade
			}
			G_Damage( other, ent, &g_entities[ent->r.ownerNum], velocity,
					  ent->s.origin, ent->damage,
					  0, ent->methodOfDeath );
		} else    // if no damage value, then this is a splash damage grenade only
		{
			G_BounceMissile( ent, trace );
			return;
		}
	}

//----(SA) removed as we have no hook

	// is it cheaper in bandwidth to just remove this ent and create a new
	// one, rather than changing the missile into the explosion?

	if ( other->takedamage && other->client ) {
		G_AddEvent( ent, EV_MISSILE_HIT, DirToByte( trace->plane.normal ) );
		ent->s.otherEntityNum = other->s.number;
	} else {
		// Ridah, try projecting it in the direction it came from, for better decals
//		G_AddEvent( ent, EV_MISSILE_MISS, DirToByte( trace->plane.normal ) );
//		vec3_t dir;
//		BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, dir );
		BG_GetMarkDir( dir, trace->plane.normal, dir );
		G_AddEvent( ent, EV_MISSILE_MISS, DirToByte( dir ) );
	}

	ent->freeAfterEvent = qtrue;

	// change over to a normal entity right at the point of impact
	etype = ent->s.eType;
	ent->s.eType = ET_GENERAL;

	SnapVectorTowards( trace->endpos, ent->s.pos.trBase );  // save net bandwidth

	G_SetOrigin( ent, trace->endpos );

	// splash damage (doesn't apply to person directly hit)
	if ( ent->splashDamage ) {
		if ( G_RadiusDamage( trace->endpos, ent->parent, ent->splashDamage, ent->splashRadius,
							 other, ent->splashMethodOfDeath ) ) {
			if ( !hitClient && g_entities[ent->r.ownerNum].client ) {
				g_entities[ent->r.ownerNum].client->ps.persistant[PERS_ACCURACY_HITS]++;
			}
		}
	}

	trap_LinkEntity( ent );


	// weapons with no default smoke business
	if ( etype == ET_MISSILE ) {
		switch ( ent->s.weapon ) {
		case WP_MORTAR:
			return;
		default:
			break;
		}
	}


	if ( strcmp( ent->classname, "zombiespit" ) ) {
		gentity_t *Msmoke;

		Msmoke = G_Spawn();
		VectorCopy( ent->r.currentOrigin, Msmoke->s.origin );
		Msmoke->think = M_think;
		Msmoke->nextthink = level.time + FRAMETIME;
		Msmoke->health = 5;
	}
}

/*
==============
Concussive_think
==============
*/
void Concussive_think( gentity_t *ent ) {
	gentity_t *player;
	vec3_t dir;
	vec3_t kvel;
	float grav = 24;
	vec3_t vec;
	float len;
	int i;

	if ( level.time > ent->delay ) {
		ent->think = G_FreeEntity;
	}

	ent->nextthink = level.time + FRAMETIME;

	for ( i = 0 ; i < g_maxclients.integer ; i++ ) {
		player = &g_entities[i];

		if ( !player || !player->inuse ) {
			continue;
		}

		if ( player->r.svFlags & SVF_CASTAI ) {
			continue;
		}

		VectorSubtract( player->r.currentOrigin, ent->s.origin, vec );
		len = VectorLength( vec );

		//	G_Printf ("len = %5.3f\n", len);

		if ( len > 512 ) {
			return;
		}

		VectorSet( dir, 0, 0, 1 );
		VectorScale( dir, grav, kvel );
		VectorAdd( player->client->ps.velocity, kvel, player->client->ps.velocity );

		if ( !player->client->ps.pm_time ) {
			int t;

			t = grav * 2;
			if ( t < 50 ) {
				t = 50;
			}
			if ( t > 200 ) {
				t = 200;
			}
			player->client->ps.pm_time = t;
			player->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
		}
	}
}

/*
==============
Concussive_fx
	shake the player
	caused by explosives (grenades/dynamite/etc.)
==============
*/
//void Concussive_fx (gentity_t *ent)
void Concussive_fx( vec3_t origin ) {
//	gentity_t *tent;
//	gentity_t *player;

	gentity_t *concussive;

	// TODO: use new, good shake effect
//	return;



	concussive = G_Spawn();
//	VectorCopy (ent->s.origin, concussive->s.origin);
	VectorCopy( origin, concussive->s.origin );
	concussive->think = Concussive_think;
	concussive->nextthink = level.time + FRAMETIME;
	concussive->delay = level.time + 500;

	return;
}

/*
==============
M_think
==============
*/
void M_think( gentity_t *ent ) {
	gentity_t *tent;

	ent->count++;

//	if (ent->count == 1)
//		Concussive_fx (ent);	//----(SA)	moved to G_ExplodeMissile()

	if ( ent->count == ent->health ) {
		ent->think = G_FreeEntity;
	}

	tent = G_TempEntity( ent->s.origin, EV_SMOKE );
	VectorCopy( ent->s.origin, tent->s.origin );
	if ( ent->s.density == 1 ) {
		tent->s.origin[2] += 16;
	} else {
		// tent->s.origin[2]+=32;
		// Note to self Maxx said to lower the spawn loc for the smoke 16 units
		tent->s.origin[2] += 16;
	}

	tent->s.time = 3000;
	tent->s.time2 = 100;
	tent->s.density = 0;
	if ( ent->s.density == 1 ) {
		tent->s.angles2[0] = 16;
	} else {
		// Note to self Maxx changed this to 24
		tent->s.angles2[0] = 24;
	}
	tent->s.angles2[1] = 96;
	tent->s.angles2[2] = 50;

	ent->nextthink = level.time + FRAMETIME;

}

// JPW NERVE
// think func for below
void Shaker_think( gentity_t *ent ) {
	vec3_t vec;          // muzzlebounce, JPW NERVE no longer used
	gentity_t   *player;
	float len, radius = ent->splashDamage, bounceamt;
	int i;
	char cmd[64];           //DAJ
	ent->think = G_FreeEntity;
	ent->nextthink = level.time + FRAMETIME;

	for ( i = 0; i < level.maxclients; i++ ) {
		// skip if not connected
		if ( level.clients[i].pers.connected != CON_CONNECTED ) {
			continue;
		}
		// skip if in limbo
		if ( level.clients[i].ps.pm_flags & PMF_LIMBO ) {
			continue;
		}
		// skip if not on same team
		if ( level.clients[i].sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}

		// found a live one
		player = &g_entities[i];
		VectorSubtract( player->r.currentOrigin, ent->s.origin, vec );
		len = VectorLength( vec );

		if ( len > radius ) {         // largest bomb blast = 600
			continue;
		}

		// NERVE - SMF - client side camera shake
		bounceamt = min( 1.0f, 1.0f - ( len / radius ) );
		Com_sprintf( cmd, sizeof(cmd), "shake %.4f", bounceamt );   //DAJ
		trap_SendServerCommand( player->s.clientNum, cmd );
	}
}
// jpw

/*
=============
Ground_Shaker
        like concussive_fx but means it
=============
*/
void Ground_Shaker( vec3_t origin, float range ) {
	gentity_t *concussive;

	concussive = G_Spawn();
	VectorCopy( origin, concussive->s.origin );
	concussive->think = Shaker_think;
	concussive->nextthink = level.time + FRAMETIME;
	concussive->splashDamage = range;
	concussive->delay = level.time + 200;           // NERVE - SMF - changed from 1000 to 200
	return;
}

/*
================
G_ExplodeMissile

Explode a missile without an impact
================
*/
void G_ExplodeMissile( gentity_t *ent ) {
	vec3_t dir;
	vec3_t origin;
	qboolean small = qfalse;
	qboolean zombiespit = qfalse;
	int etype;

	BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );
	SnapVector( origin );
	G_SetOrigin( ent, origin );

	// we don't have a valid direction, so just point straight up
	dir[0] = dir[1] = 0;
	dir[2] = 1;

	etype = ent->s.eType;

	ent->s.eType = ET_GENERAL;

	if ( !Q_stricmp( ent->classname, "props_explosion" ) ) {
		G_AddEvent( ent, EV_MISSILE_MISS_SMALL, DirToByte( dir ) );
		small = qtrue;
	}
// JPW NERVE
	else if ( !Q_stricmp( ent->classname, "air strike" ) ) {
		G_AddEvent( ent, EV_MISSILE_MISS_LARGE, DirToByte( dir ) );
		small = qfalse;
	}
// jpw
	else if ( !Q_stricmp( ent->classname, "props_explosion_large" ) ) {
		G_AddEvent( ent, EV_MISSILE_MISS_LARGE, DirToByte( dir ) );
		small = qfalse;
	} else if ( !Q_stricmp( ent->classname, "zombiespit" ) )      {
		G_AddEvent( ent, EV_SPIT_MISS, DirToByte( dir ) );
		zombiespit = qtrue;
	} else if ( !Q_stricmp( ent->classname, "flamebarrel" ) )      {
		ent->freeAfterEvent = qtrue;
		trap_LinkEntity( ent );
		return;
	} else {
		G_AddEvent( ent, EV_MISSILE_MISS, DirToByte( dir ) );
	}

	ent->freeAfterEvent = qtrue;

	// splash damage
	if ( ent->splashDamage ) {
		if ( G_RadiusDamage( ent->r.currentOrigin, ent->parent, ent->splashDamage, ent->splashRadius, ent, ent->splashMethodOfDeath ) ) {    //----(SA)
			if ( g_entities[ent->r.ownerNum].client ) {
				g_entities[ent->r.ownerNum].client->ps.persistant[PERS_ACCURACY_HITS]++;
			}
		}
	}

	trap_LinkEntity( ent );

	if ( etype == ET_MISSILE ) {
		if ( g_gametype.integer <= GT_COOP ) {
			if ( ent->s.weapon == WP_DYNAMITE ) {                     // do some scoring
				// check if dynamite is in trigger_objective_info field
				vec3_t mins, maxs;
				//static vec3_t range = { 18, 18, 18 }; // NOTE can use this to massage throw distance outside trigger field // TTimo unused
				int i,num,touch[MAX_GENTITIES];
				gentity_t   *hit;

				// NERVE - SMF - made this the actual bounding box of dynamite instead of range
				VectorAdd( ent->r.currentOrigin, ent->r.mins, mins );
				VectorAdd( ent->r.currentOrigin, ent->r.maxs, maxs );
				num = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
				VectorAdd( ent->r.currentOrigin, ent->r.mins, mins );
				VectorAdd( ent->r.currentOrigin, ent->r.maxs, maxs );

				for ( i = 0 ; i < num ; i++ ) {
					hit = &g_entities[touch[i]];
					if ( !hit->target ) {
						continue;
					}

					if ( !( hit->r.contents & CONTENTS_TRIGGER ) ) {
						continue;
					}
					if ( !strcmp( hit->classname,"trigger_objective_info" ) ) {
						if ( !( hit->spawnflags & ( AXIS_OBJECTIVE | ALLIED_OBJECTIVE ) ) ) {
							continue;
						}

						if ( hit->spawnflags & AXIS_OBJECTIVE || hit->spawnflags & ALLIED_OBJECTIVE ) {
							//if ( ( ( hit->spawnflags & AXIS_OBJECTIVE ) && ( ent->s.teamNum == TEAM_BLUE ) ) ||
							//        ( ( hit->spawnflags & ALLIED_OBJECTIVE ) && ( ent->s.teamNum == TEAM_RED ) ) ) {
							G_UseTargets( hit,ent );
							hit->think = G_FreeEntity;
							hit->nextthink = level.time + FRAMETIME;

							if ( ent->parent->client ) {
								if ( ent->s.teamNum == ent->parent->client->sess.sessionTeam ) {                                         // make sure player hasn't changed teams -- per atvi req
									AddScore( ent->parent, hit->accuracy );                                             // set from map, see g_trigger
								}
							}
						}
					}
				}
			}
		}
	
		// give big weapons the shakey shakey
		if ( ent->s.weapon == WP_DYNAMITE || ent->s.weapon == WP_PANZERFAUST || ent->s.weapon == WP_GRENADE_LAUNCHER ||
			ent->s.weapon == WP_GRENADE_PINEAPPLE || ent->s.weapon == WP_MORTAR ) {
			Ground_Shaker( ent->r.currentOrigin, ent->splashDamage * 4 );
		}
		return;
	}

	if ( !zombiespit ) {
		gentity_t *Msmoke;

		Msmoke = G_Spawn();
		VectorCopy( ent->r.currentOrigin, Msmoke->s.origin );
		if ( small ) {
			Msmoke->s.density = 1;
		}
		Msmoke->think = M_think;
		Msmoke->nextthink = level.time + FRAMETIME;

		if ( ent->parent && !Q_stricmp( ent->parent->classname, "props_flamebarrel" ) ) {
			Msmoke->health = 10;
		} else {
			Msmoke->health = 5;
		}

		Concussive_fx( Msmoke->s.origin );
	}
}

/*
================
G_MissileDie
================
*/
void G_MissileDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod ) {
	if ( inflictor == self ) {
		return;
	}
	self->takedamage    = qfalse;
	self->think         = G_ExplodeMissile;
	self->nextthink     = level.time + 10;
}

/*
================
G_ExplodeMissilePoisonGas

Explode a missile without an impact
================
*/
void G_ExplodeMissilePoisonGas( gentity_t *ent ) {
	vec3_t dir;
	vec3_t origin;

	BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );
	SnapVector( origin );
	G_SetOrigin( ent, origin );

	// we don't have a valid direction, so just point straight up
	dir[0] = dir[1] = 0;
	dir[2] = 1;

	ent->freeAfterEvent = qtrue;


	{
		gentity_t *gas;

		gas = G_Spawn();
		gas->think = gas_think;
		gas->nextthink = level.time + FRAMETIME;
		gas->r.contents = CONTENTS_TRIGGER;
		gas->touch = gas_touch;
		gas->health = 100;
		G_SetOrigin( gas, origin );

		trap_LinkEntity( gas );
	}

}

/*
================
G_RunMissile

================
*/
void G_RunMissile( gentity_t *ent ) {
	vec3_t origin, dir;         // 'dir' is 'deltaMove'
	trace_t tr;
	int impactDamage;

	// Ridah, make AI aware of this danger
	// DHM - Nerve :: Only in single player
	if ( g_gametype.integer <= GT_SINGLE_PLAYER ) {
		AICast_CheckDangerousEntity( ent, DANGER_MISSILE, ent->splashRadius, 0.1, 1.0, qtrue );
	}

	// get current position
	BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );

//----(SA)	added direction to pass for mark determination
	VectorSubtract( origin, ent->r.currentOrigin, dir );
	VectorNormalize( dir );
//----(SA)	end

	// trace a line from the previous position to the current position,
	// ignoring interactions with the missile owner
	trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, origin,
				ent->r.ownerNum, ent->clipmask );

	VectorCopy( tr.endpos, ent->r.currentOrigin );

	if ( tr.startsolid ) {
		tr.fraction = 0;
	}

	trap_LinkEntity( ent );

	if ( tr.fraction != 1 ) {
		// never explode or bounce on sky
		if  (   tr.surfaceFlags & SURF_NOIMPACT ) {
			// If grapple, reset owner
			if ( ent->parent && ent->parent->client && ent->parent->client->hook == ent ) {
				ent->parent->client->hook = NULL;
			}
			G_FreeEntity( ent );
			return;
		}

		if ( ent->s.weapon == WP_PANZERFAUST ) {
			impactDamage = 999; // goes through pretty much any func_explosives
		} else {
			impactDamage = 6;   // "grenade"/"dynamite"		// probably adjust this based on velocity //----(SA)	adjusted to not break through so much stuff.  try it to see if this is good enough

		}
		G_MissileImpact( ent, &tr, impactDamage, dir ); //----(SA)	added 'dir'

		if ( ent->s.eType != ET_MISSILE ) {
			return;     // exploded
		}
	} else if ( VectorLength( ent->s.pos.trDelta ) ) {  // free fall/no intersection
		ent->s.groundEntityNum = ENTITYNUM_NONE;
	}

	// check think function after bouncing
	G_RunThink( ent );
}

/*
================
G_PredictBounceMissile

================
*/
void G_PredictBounceMissile( gentity_t *ent, trajectory_t *pos, trace_t *trace, int time ) {
	vec3_t velocity, origin;
	float dot;
	int hitTime;

	BG_EvaluateTrajectory( pos, time, origin );

	// reflect the velocity on the trace plane
	hitTime = time;
	BG_EvaluateTrajectoryDelta( pos, hitTime, velocity );
	dot = DotProduct( velocity, trace->plane.normal );
	VectorMA( velocity, -2 * dot, trace->plane.normal, pos->trDelta );

	if ( ent->s.eFlags & EF_BOUNCE_HALF ) {
		if ( ent->s.eFlags & EF_BOUNCE ) {     // both flags marked, do a third type of bounce
			VectorScale( pos->trDelta, 0.25, pos->trDelta );
		} else {
			VectorScale( pos->trDelta, 0.65, pos->trDelta );
		}

		// check for stop
		if ( trace->plane.normal[2] > 0.2 && VectorLength( pos->trDelta ) < 40 ) {
			VectorCopy( trace->endpos, pos->trBase );
			return;
		}
	}

	VectorAdd( origin, trace->plane.normal, pos->trBase );
	pos->trTime = time;
}

/*
================
G_PredictMissile

  selfNum is the character that is checking to see what the missile is going to do

  returns qfalse if the missile won't explode, otherwise it'll return the time is it expected to explode
================
*/
int G_PredictMissile( gentity_t *ent, int duration, vec3_t endPos, qboolean allowBounce ) {
	vec3_t origin;
	trace_t tr;
	int time;
	trajectory_t pos;
	vec3_t org;
	gentity_t backupEnt;

	pos = ent->s.pos;
	BG_EvaluateTrajectory( &pos, level.time, org );

	backupEnt = *ent;

	for ( time = level.time + FRAMETIME; time < level.time + duration; time += FRAMETIME ) {

		// get current position
		BG_EvaluateTrajectory( &pos, time, origin );

		// trace a line from the previous position to the current position,
		// ignoring interactions with the missile owner
		trap_Trace( &tr, org, ent->r.mins, ent->r.maxs, origin,
					ent->r.ownerNum, ent->clipmask );

		VectorCopy( tr.endpos, org );

		if ( tr.startsolid ) {
			*ent = backupEnt;
			return qfalse;
		}

		if ( tr.fraction != 1 ) {
			// never explode or bounce on sky
			if  ( tr.surfaceFlags & SURF_NOIMPACT ) {
				*ent = backupEnt;
				return qfalse;
			}

			if ( allowBounce && ( ent->s.eFlags & ( EF_BOUNCE | EF_BOUNCE_HALF ) ) ) {
				G_PredictBounceMissile( ent, &pos, &tr, time - FRAMETIME + (int)( (float)FRAMETIME * tr.fraction ) );
				pos.trTime = time;
				continue;
			}

			// exploded, so drop out of loop
			break;
		}
	}
/*
	if (!allowBounce && tr.fraction < 1 && tr.entityNum > level.maxclients) {
		// go back a bit in time, so we can catch it in the air
		time -= 200;
		if (time < level.time + FRAMETIME)
			time = level.time + FRAMETIME;
		BG_EvaluateTrajectory( &pos, time, org );
	}
*/

	// get current position
	VectorCopy( org, endPos );
	// set the entity data back
	*ent = backupEnt;
	//
	if ( allowBounce && ( ent->s.eFlags & ( EF_BOUNCE | EF_BOUNCE_HALF ) ) ) {
		return ent->nextthink;
	} else {    // it will probably explode before it times out
		return time;
	}
}

// Rafael zombiespit
/*
================
G_RunSpit
================
*/


void G_RunSpit( gentity_t *ent ) {
	vec3_t origin;
	trace_t tr;
	vec3_t end;
	gentity_t   *smoke;

	// effect when it drips to floor
	if ( rand() % 100 > 60 ) {
		VectorCopy( ent->r.currentOrigin, end );
		end[0] += crandom() * 8;
		end[1] += crandom() * 8;
		end[2] -= 8192;

		trap_Trace( &tr, ent->r.currentOrigin, NULL, NULL, end,
					ent->r.ownerNum, MASK_SHOT );

		smoke = G_Spawn();
		VectorCopy( tr.endpos, smoke->s.origin );
		smoke->start_size = 4;
		smoke->end_size = 8;
		smoke->spawnflags |= 4;
		smoke->speed = 500; // 5 seconds
		smoke->duration = 100;
		smoke->health = 10 + ( crandom() * 5 );
		SP_target_smoke( smoke );
		smoke->s.density = 5;
	}

	// get current position
	BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );

	// trace a line from the previous position to the current position,
	// ignoring interactions with the missile owner
	trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, origin,
				ent->r.ownerNum, ent->clipmask );

	VectorCopy( tr.endpos, ent->r.currentOrigin );

	if ( tr.startsolid ) {
		tr.fraction = 0;
	}

	trap_LinkEntity( ent );

	if ( tr.fraction != 1 ) {
		// never explode or bounce on sky
		if  (   tr.surfaceFlags & SURF_NOIMPACT ) {
			// If grapple, reset owner
			if ( ent->parent && ent->parent->client->hook == ent ) {
				ent->parent->client->hook = NULL;
			}
			G_FreeEntity( ent );
			return;
		}

		// G_MissileImpact( ent, &tr );
		{
			gentity_t *gas;

			gas = G_Spawn();
			gas->think = gas_think;
			gas->nextthink = level.time + FRAMETIME;
			gas->r.contents = CONTENTS_TRIGGER;
			gas->touch = gas_touch;
			gas->health = 10;
			G_SetOrigin( gas, origin );
			gas->s.density = 5;
			trap_LinkEntity( gas );

			ent->freeAfterEvent = qtrue;

			// change over to a normal entity right at the point of impact
			ent->s.eType = ET_GENERAL;

		}

		if ( ent->s.eType != ET_MISSILE ) {
			return;     // exploded
		}
	}

	// check think function after bouncing
	G_RunThink( ent );
}


void G_RunCrowbar( gentity_t *ent ) {
	vec3_t origin;
	trace_t tr;

	// get current position
	BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );

	// trace a line from the previous position to the current position,
	// ignoring interactions with the missile owner
	trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, origin,
				ent->r.ownerNum, ent->clipmask );

	VectorCopy( tr.endpos, ent->r.currentOrigin );

	if ( tr.startsolid ) {
		tr.fraction = 0;
	}

	trap_LinkEntity( ent );

	if ( tr.fraction != 1 ) {
		// never explode or bounce on sky
		if  (   tr.surfaceFlags & SURF_NOIMPACT ) {
			// If grapple, reset owner
			if ( ent->parent && ent->parent->client->hook == ent ) {
				ent->parent->client->hook = NULL;
			}
			G_FreeEntity( ent );
			return;
		}

		if ( ent->s.eType != ET_MISSILE ) {
			return;     // exploded
		}
	}

	// check think function after bouncing
	G_RunThink( ent );
}

//=============================================================================
// DHM - Nerve :: Server side Flamethrower
//=============================================================================

// copied from cg_flamethrower.c
#define FLAME_START_SIZE        1.0
#define FLAME_START_MAX_SIZE    100.0   // when the flame is spawned, it should endevour to reach this size
#define FLAME_START_SPEED       1200.0  // speed of flame as it leaves the nozzle
#define FLAME_MIN_SPEED         60.0

// these are calculated (don't change)
#define FLAME_LENGTH            ( FLAMETHROWER_RANGE + 50.0 ) // NOTE: only modify the range, since this should always reflect that range

#define FLAME_LIFETIME          (int)( ( FLAME_LENGTH / FLAME_START_SPEED ) * 1000 )    // life duration in milliseconds
#define FLAME_FRICTION_PER_SEC  ( 2.0f * FLAME_START_SPEED )
#define GET_FLAME_SIZE_SPEED( x ) ( ( (float)x / FLAME_LIFETIME ) / 0.3 ) // x is the current sizeMax

#define FLAME_THRESHOLD 50

void G_FlameDamage( gentity_t *self ) {
	gentity_t   *body;
	int entityList[MAX_GENTITIES];
	int i, e, numListedEntities;
	float radius, boxradius, dist;
	vec3_t mins, maxs, point, v;
	trace_t tr;

	radius = self->speed;
	boxradius = 1.41421356 * radius; // radius * sqrt(2) for bounding box enlargement

	for ( i = 0 ; i < 3 ; i++ ) {
		mins[i] = self->r.currentOrigin[i] - boxradius;
		maxs[i] = self->r.currentOrigin[i] + boxradius;
	}

	numListedEntities = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );

	for ( e = 0 ; e < numListedEntities ; e++ ) {
		body = &g_entities[entityList[ e ]];

		if ( !body->takedamage ) {
			continue;
		}

// JPW NERVE don't catch fire if invulnerable or same team in no FF
		if ( body->client ) {
			if ( body->client->ps.powerups[PW_INVULNERABLE] >= level.time ) {
				body->flameQuota = 0;
				body->s.onFireEnd = level.time - 1;
				continue;
			}
			if ( !( g_friendlyFire.integer ) && OnSameTeam( body,self->parent ) ) {
				continue;
			}
		}
// jpw

// JPW NERVE don't catch fire if under water or invulnerable
		if ( body->waterlevel >= 3 ) {
			body->flameQuota = 0;
			body->s.onFireEnd = level.time - 1;
			continue;
		}
// jpw

		if ( !body->r.bmodel ) {
			VectorCopy( body->r.currentOrigin, point );
			if ( body->client ) {
				point[2] += body->client->ps.viewheight;
			}
			VectorSubtract( point, self->r.currentOrigin, v );
		} else {
			for ( i = 0 ; i < 3 ; i++ ) {
				if ( self->s.origin[i] < body->r.absmin[i] ) {
					v[i] = body->r.absmin[i] - self->r.currentOrigin[i];
				} else if ( self->r.currentOrigin[i] > body->r.absmax[i] ) {
					v[i] = self->r.currentOrigin[i] - body->r.absmax[i];
				} else {
					v[i] = 0;
				}
			}
		}

		dist = VectorLength( v );

		// The person who shot the flame only burns when within 1/2 the radius
		if ( body->s.number == self->r.ownerNum && dist >= ( radius * 0.5 ) ) {
			continue;
		}
		if ( dist >= radius ) {
			continue;
		}

		// Non-clients that take damage get damaged here
		if ( !body->client ) {
			if ( body->health > 0 ) {
				G_Damage( body, self->parent, self->parent, vec3_origin, self->r.currentOrigin, 2, 0, MOD_FLAMETHROWER );
			}
			continue;
		}

		// JPW NERVE -- do a trace to see if there's a wall btwn. body & flame centroid -- prevents damage through walls
		trap_Trace( &tr, self->r.currentOrigin, NULL, NULL, point, body->s.number, MASK_SHOT );
		if ( tr.fraction < 1.0 ) {
			continue;
		}
		// jpw

		// now check the damageQuota to see if we should play a pain animation
		// first reduce the current damageQuota with time
		if ( body->flameQuotaTime && body->flameQuota > 0 ) {
			body->flameQuota -= (int)( ( (float)( level.time - body->flameQuotaTime ) / 1000 ) * 2.5f );
			if ( body->flameQuota < 0 ) {
				body->flameQuota = 0;
			}
		}

		G_BurnMeGood( self, body );
	}
}

void G_RunFlamechunk( gentity_t *ent ) {
	vec3_t vel, add;
	vec3_t neworg;
	trace_t tr;
	float speed;

	VectorCopy( ent->s.pos.trDelta, vel );

	// Adust the current speed of the chunk
	if ( level.time - ent->timestamp > 50 ) {
		speed = VectorNormalize( vel );
		speed -= ( 50.f / 1000.f ) * FLAME_FRICTION_PER_SEC;

		if ( speed < FLAME_MIN_SPEED ) {
			speed = FLAME_MIN_SPEED;
		}

		VectorScale( vel, speed, ent->s.pos.trDelta );
	} else {
		speed = FLAME_START_SPEED;
	}

	// Move the chunk
	VectorScale( ent->s.pos.trDelta, 50.f / 1000.f, add );
	VectorAdd( ent->r.currentOrigin, add, neworg );

	trap_Trace( &tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, neworg, ent->r.ownerNum, MASK_SHOT | MASK_WATER ); // JPW NERVE

	if ( tr.startsolid ) {
		VectorCopy( vec3_origin, ent->s.pos.trDelta );
	} else if ( tr.fraction != 1.0f && !( tr.surfaceFlags & SURF_NOIMPACT ) ) {
		float dot;
		VectorCopy( tr.endpos, ent->r.currentOrigin );

		dot = DotProduct( vel, tr.plane.normal );
		VectorMA( vel, -2 * dot, tr.plane.normal, vel );
		VectorNormalize( vel );
		speed *= 0.5 * ( 0.25 + 0.75 * ( ( dot + 1.0 ) * 0.5 ) );
		VectorScale( vel, speed, ent->s.pos.trDelta );
	} else {
		VectorCopy( neworg, ent->r.currentOrigin );
	}

	// Do damage to nearby entities, every 100ms
	if ( ent->flameQuotaTime <= level.time ) {
		ent->flameQuotaTime = level.time + 100;
		G_FlameDamage( ent );
	}

	// Show debugging bbox
	if ( g_debugBullets.integer > 3 ) {
		gentity_t *bboxEnt;
		float size = ent->speed / 2;
		vec3_t b1, b2;
		vec3_t temp;
		VectorSet( temp, -size, -size, -size );
		VectorCopy( ent->r.currentOrigin, b1 );
		VectorCopy( ent->r.currentOrigin, b2 );
		VectorAdd( b1, temp, b1 );
		VectorSet( temp, size, size, size );
		VectorAdd( b2, temp, b2 );
		bboxEnt = G_TempEntity( b1, EV_RAILTRAIL );
		VectorCopy( b2, bboxEnt->s.origin2 );
		bboxEnt->s.dmgFlags = 1;    // ("type")
	}

	// Adjust the size
	if ( ent->speed < FLAME_START_MAX_SIZE ) {
		ent->speed += 10.f;

		if ( ent->speed > FLAME_START_MAX_SIZE ) {
			ent->speed = FLAME_START_MAX_SIZE;
		}
	}

	// Remove after 2 seconds
	if ( level.time - ent->timestamp > ( FLAME_LIFETIME - 150 ) ) { // JPW NERVE increased to 350 from 250 to match visuals better
		G_FreeEntity( ent );
		return;
	}

	G_RunThink( ent );
}

/*
=================
fire_flamechunk
=================
*/
gentity_t *fire_flamechunk( gentity_t *self, vec3_t start, vec3_t dir ) {
	gentity_t   *bolt;

	// Only spawn every other frame
	if ( self->count2 ) {
		self->count2--;
		return NULL;
	}

	self->count2 = 1;
	VectorNormalize( dir );

	bolt = G_Spawn();
	bolt->classname = "flamechunk";

	bolt->timestamp = level.time;
	bolt->flameQuotaTime = level.time + 50;
	bolt->s.eType = ET_FLAMETHROWER_CHUNK;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN | SVF_NOCLIENT;
	bolt->s.weapon = self->s.weapon;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->methodOfDeath = MOD_FLAMETHROWER;
	bolt->clipmask = MASK_MISSILESHOT;

	bolt->s.pos.trType = TR_DECCELERATE;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;     // move a bit on the very first frame
	bolt->s.pos.trDuration = 800;

	// 'speed' will be the current size radius of the chunk
	bolt->speed = FLAME_START_SIZE;
	VectorSet( bolt->r.mins, -4, -4, -4 );
	VectorSet( bolt->r.maxs, 4, 4, 4 );
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, FLAME_START_SPEED, bolt->s.pos.trDelta );

	SnapVector( bolt->s.pos.trDelta );          // save net bandwidth
	VectorCopy( start, bolt->r.currentOrigin );

	return bolt;
}

//=============================================================================

//----(SA) removed unused quake3 weapons.

int G_GetWeaponDamage( int weapon ); // JPW NERVE

/*
=================
fire_grenade

	NOTE!!!! NOTE!!!!!

	This accepts a /non-normalized/ direction vector to allow specification
	of how hard it's thrown.  Please scale the vector before calling.

=================
*/
gentity_t *fire_grenade( gentity_t *self, vec3_t start, vec3_t dir, int grenadeWPID ) {
	gentity_t   *bolt, *hit; // JPW NERVE
	qboolean noExplode = qfalse;
	vec3_t mins, maxs;      // JPW NERVE
	static vec3_t range = { 40, 40, 52 };   // JPW NERVE
	int i,num,touch[MAX_GENTITIES];         // JPW NERVE

	bolt = G_Spawn();

	// no self->client for shooter_grenade's
	if ( self->client && self->client->ps.grenadeTimeLeft ) {
		if ( grenadeWPID == WP_DYNAMITE ) {   // remove any fraction of a 5 second 'click'
			self->client->ps.grenadeTimeLeft *= 5;
			self->client->ps.grenadeTimeLeft -= ( self->client->ps.grenadeTimeLeft % 5000 );
			self->client->ps.grenadeTimeLeft += 5000;
//			if(self->client->ps.grenadeTimeLeft < 5000)	// allow dropping of dynamite that won't explode (for shooting)
//				noExplode = qtrue;
		}

		if ( !noExplode ) {
			bolt->nextthink = level.time + self->client->ps.grenadeTimeLeft;
		}
	} else {
		// let non-players throw the default duration
		if ( grenadeWPID == WP_DYNAMITE ) {
			if ( !noExplode ) {
				bolt->nextthink = level.time + 5000;
			}
		} else {
			bolt->nextthink = level.time + 2500;
		}
	}

	// no self->client for shooter_grenade's
	if ( self->client ) {
		self->client->ps.grenadeTimeLeft = 0;       // reset grenade timer

	}
	if ( !noExplode ) {
		bolt->think         = G_ExplodeMissile;
	}

	bolt->s.eType       = ET_MISSILE;
	bolt->r.svFlags     = SVF_USE_CURRENT_ORIGIN | SVF_BROADCAST;
	bolt->s.weapon      = grenadeWPID;
	bolt->r.ownerNum    = self->s.number;
	bolt->parent        = self;

// JPW NERVE -- commented out bolt->damage and bolt->splashdamage, override with G_GetWeaponDamage()
// so it works with different netgame balance.  didn't uncomment bolt->damage on dynamite 'cause its so *special*
	bolt->damage = G_GetWeaponDamage( grenadeWPID ); // overridden for dynamite
	bolt->splashDamage = G_GetWeaponDamage( grenadeWPID );

	if ( self->client && !self->aiCharacter ) {
		bolt->damage *= 2;
		bolt->splashDamage *= 2;
	}
// jpw

	switch ( grenadeWPID ) {
	case WP_GRENADE_LAUNCHER:
		bolt->classname             = "grenade";
//			bolt->damage				= 100;
//			bolt->splashDamage			= 100;
		if ( self->aiCharacter ) {
			bolt->splashRadius          = 300;
		} else {
			bolt->splashRadius          = 300;
		}
		bolt->methodOfDeath         = MOD_GRENADE;
		bolt->splashMethodOfDeath   = MOD_GRENADE_SPLASH;
		if ( g_gametype.integer >= GT_COOP ) {
			bolt->s.eFlags              = EF_BOUNCE_HALF;
		} else {
			bolt->s.eFlags              = EF_BOUNCE_HALF | EF_BOUNCE;
		}
		break;
	case WP_GRENADE_PINEAPPLE:
		bolt->classname             = "grenade";
//			bolt->damage				= 80;
//			bolt->splashDamage			= 80;
		bolt->splashRadius          = 450;
		bolt->methodOfDeath         = MOD_GRENADE;
		bolt->splashMethodOfDeath   = MOD_GRENADE_SPLASH;
		if ( g_gametype.integer >= GT_COOP ) {
			bolt->s.eFlags              = EF_BOUNCE_HALF;
		} else {
			bolt->s.eFlags              = EF_BOUNCE_HALF | EF_BOUNCE;
		}
		break;
// JPW NERVE
	case WP_GRENADE_SMOKE:
		bolt->classname             = "grenade";
		bolt->s.eFlags              = EF_BOUNCE_HALF;
		break;
// jpw
	case WP_DYNAMITE:
		// oh, this is /so/ cheap...
		// you need to pick up new code ;)
		trap_SendServerCommand( self - g_entities, va( "dp %d", ( bolt->nextthink - level.time ) / 1000 ) );
// JPW NERVE
		if ( g_gametype.integer > GT_SINGLE_PLAYER ) {
// check if player is in trigger objective field -- swiped from G_TouchTriggers()
			VectorSubtract( self->client->ps.origin, range, mins );
			VectorAdd( self->client->ps.origin, range, maxs );
			num = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
			VectorAdd( self->client->ps.origin, self->r.mins, mins );
			VectorAdd( self->client->ps.origin, self->r.maxs, maxs );

			for ( i = 0 ; i < num ; i++ ) {
				hit = &g_entities[touch[i]];

				if ( !hit->touch && !self->touch ) {
					continue;
				}
				if ( !( hit->r.contents & CONTENTS_TRIGGER ) ) {
					continue;
				}
				if ( !strcmp( hit->classname,"trigger_objective_info" ) ) {
					if ( hit->track ) {
						trap_SendServerCommand( -1, va( "cp \"%s\"", va( "Det charge planted near %s!", hit->track ) ) );
					} else {
						trap_SendServerCommand( -1, va( "cp \"%s\"", va( "Det charge planted near objective %d!", hit->count ) ) );
					}
					i = num;
				}
			}
		}
// jpw
		bolt->classname             = "dynamite";
		bolt->damage                = 0;
//			bolt->splashDamage			= 300;
		bolt->splashRadius          = 600;
		bolt->methodOfDeath         = MOD_DYNAMITE;
		bolt->splashMethodOfDeath   = MOD_DYNAMITE_SPLASH;
		bolt->s.eFlags              = ( EF_BOUNCE | EF_BOUNCE_HALF );   // EF_BOUNCE_HEAVY;

		// dynamite is shootable
// JPW NERVE only in single player
		if ( g_gametype.integer <= GT_SINGLE_PLAYER ) {
//				bolt->health				= 5;
//				bolt->takedamage			= qtrue;
//
//				// small target cube
//				VectorSet(bolt->r.mins, -4, -4, 0);
//				VectorCopy(bolt->r.mins, bolt->r.absmin);
//				VectorSet(bolt->r.maxs, 4, 4, 8);
//				VectorCopy(bolt->r.maxs, bolt->r.absmax);
		} else {
			bolt->health                = 5;
			bolt->takedamage            = qfalse;
		}
// jpw
		bolt->die                   = G_MissileDie;
		bolt->r.contents            = CONTENTS_CORPSE;      // (player can walk through)

		// nope - this causes the dynamite to impact on the players bb when he throws it.
		// will try setting it when it settles
//			bolt->r.ownerNum			= ENTITYNUM_WORLD;	// (SA) make the world the owner of the dynamite, so the player can shoot it without modifying the bullet code to ignore players id for hits

		break;
	}

// JPW NERVE -- blast radius proportional to damage
	if ( g_gametype.integer > GT_SINGLE_PLAYER ) {
		bolt->splashRadius = G_GetWeaponDamage( grenadeWPID );
	}
// jpw

	bolt->clipmask = MASK_MISSILESHOT;

	bolt->s.pos.trType = TR_GRAVITY;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;     // move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
	VectorCopy( dir, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );          // save net bandwidth

	VectorCopy( start, bolt->r.currentOrigin );

	return bolt;
}

//=============================================================================



/*
=================
fire_rocket
=================
*/
gentity_t *fire_rocket( gentity_t *self, vec3_t start, vec3_t dir ) {
	gentity_t   *bolt;

	VectorNormalize( dir );

	bolt = G_Spawn();
	bolt->classname = "rocket";
	bolt->nextthink = level.time + 20000;   // push it out a little
	bolt->think = G_ExplodeMissile;
	bolt->s.eType = ET_MISSILE;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN | SVF_BROADCAST;

	//DHM - Nerve :: Use the correct weapon in multiplayer
	if ( g_gametype.integer <= GT_SINGLE_PLAYER ) {
		bolt->s.weapon = WP_PANZERFAUST;
	} else {
		bolt->s.weapon = self->s.weapon;
	}

	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;

	if ( self->aiCharacter ) { // ai keep the values they've been using
		bolt->damage = 100;
		bolt->splashDamage = 120;
	} else {
		bolt->damage = G_GetWeaponDamage( WP_PANZERFAUST );
		bolt->splashDamage = G_GetWeaponDamage( WP_PANZERFAUST );
	}

// JPW NERVE
	if ( g_gametype.integer > GT_SINGLE_PLAYER ) {
		bolt->splashRadius = G_GetWeaponDamage( WP_PANZERFAUST );
	} else {
		if ( self->aiCharacter ) {
			bolt->splashRadius = 120;
		} else {
			bolt->splashRadius = G_GetWeaponDamage( WP_PANZERFAUST );
		}
	}

// jpw
	bolt->methodOfDeath = MOD_ROCKET;
	bolt->splashMethodOfDeath = MOD_ROCKET_SPLASH;
//	bolt->clipmask = MASK_SHOT;
	bolt->clipmask = MASK_MISSILESHOT;

	bolt->s.pos.trType = TR_LINEAR;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;     // move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
//	VectorScale( dir, 900, bolt->s.pos.trDelta );	// old speed was 900

// JPW NERVE

	if ( g_gametype.integer > GT_SINGLE_PLAYER ) {
		VectorScale( dir,2500,bolt->s.pos.trDelta );
	} else {
		// ai gets dynamics they're used to
		if ( self->aiCharacter ) {
			VectorScale( dir, 1000, bolt->s.pos.trDelta );
		} else {
			// (SA) trying a bit more speed in SP for player rockets
			VectorScale( dir, 1300, bolt->s.pos.trDelta );
		}
	}
// jpw

	SnapVector( bolt->s.pos.trDelta );          // save net bandwidth
	VectorCopy( start, bolt->r.currentOrigin );

	return bolt;
}

// Rafael zombie spit
/*
=====================
fire_zombiespit
=====================
*/
gentity_t *fire_zombiespit( gentity_t *self, vec3_t start, vec3_t dir ) {
	gentity_t   *bolt;

	VectorNormalize( dir );

	bolt = G_Spawn();
	bolt->classname = "zombiespit";
	bolt->nextthink = level.time + 10000;

	bolt->think = G_ExplodeMissile;

	bolt->s.eType = ET_ZOMBIESPIT;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;

	bolt->s.weapon = WP_PANZERFAUST;

	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;

	bolt->damage = 10;
	bolt->splashDamage = 10;
	bolt->splashRadius = 120;

	bolt->methodOfDeath = MOD_ZOMBIESPIT;
	bolt->splashMethodOfDeath = MOD_ZOMBIESPIT_SPLASH;

	bolt->clipmask = MASK_MISSILESHOT;

	bolt->s.loopSound = G_SoundIndex( "sound/Loogie/sizzle.wav" );

	// bolt->s.pos.trType = TR_LINEAR;
	bolt->s.pos.trType = TR_GRAVITY_LOW;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;     // move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, 600, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );          // save net bandwidth
	VectorCopy( start, bolt->r.currentOrigin );

	return bolt;
}

/*
=====================
fire_zombiespirit
=====================
*/
gentity_t *fire_zombiespirit( gentity_t *self, gentity_t *bolt, vec3_t start, vec3_t dir ) {

	VectorNormalize( dir );

	bolt->classname = "zombiespirit";
	bolt->nextthink = level.time + 10000;

	bolt->think = G_ExplodeMissile;

	bolt->s.eType = ET_ZOMBIESPIRIT;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;

	bolt->s.weapon = WP_PANZERFAUST;

	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = 10;
	bolt->splashDamage = 10;
	bolt->splashRadius = 120;

	bolt->methodOfDeath = MOD_ZOMBIESPIRIT;
	bolt->splashMethodOfDeath = MOD_ZOMBIESPIRIT_SPLASH;

	bolt->clipmask = MASK_MISSILESHOT;

	bolt->s.loopSound = G_SoundIndex( "sound/Zombie/attack/spirit_loop.wav" );

	bolt->s.pos.trType = TR_INTERPOLATE;        // we'll move it manually, since it needs to track it's enemy
	bolt->s.pos.trTime = level.time;            // move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, 800, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );          // save net bandwidth
	VectorCopy( start, bolt->r.currentOrigin );

	return bolt;
}

// the crowbar for the mechanic
gentity_t *fire_crowbar( gentity_t *self, vec3_t start, vec3_t dir ) {
	gentity_t   *bolt;

	VectorNormalize( dir );

	bolt = G_Spawn();
	bolt->classname = "crowbar";
	bolt->nextthink = level.time + 50000;
	bolt->think = G_ExplodeMissile;
	bolt->s.eType = ET_CROWBAR;


	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN | SVF_BROADCAST;
	// bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;

	bolt->s.weapon = WP_PANZERFAUST;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = 10;
	bolt->splashDamage = 0;
	bolt->splashRadius = 0;
	bolt->methodOfDeath = MOD_ROCKET;
	bolt->splashMethodOfDeath = MOD_ROCKET_SPLASH;
//	bolt->clipmask = MASK_SHOT;
	bolt->clipmask = MASK_MISSILESHOT;

	bolt->s.pos.trType = TR_GRAVITY;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;     // move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, 800, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );          // save net bandwidth
	VectorCopy( start, bolt->r.currentOrigin );

	return bolt;
}

// Rafael flamebarrel
/*
======================
fire_flamebarrel
======================
*/

gentity_t *fire_flamebarrel( gentity_t *self, vec3_t start, vec3_t dir ) {
	gentity_t   *bolt;

	VectorNormalize( dir );

	bolt = G_Spawn();
	bolt->classname = "flamebarrel";
	bolt->nextthink = level.time + 3000;
	bolt->think = G_ExplodeMissile;
	bolt->s.eType = ET_FLAMEBARREL;
	bolt->s.eFlags = EF_BOUNCE_HALF;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	bolt->s.weapon = WP_PANZERFAUST;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = 200;
	bolt->splashDamage = 40;
	bolt->splashRadius = 120;

	bolt->methodOfDeath = MOD_ROCKET;
	bolt->splashMethodOfDeath = MOD_ROCKET_SPLASH;

	bolt->clipmask = MASK_MISSILESHOT;

	bolt->s.pos.trType = TR_GRAVITY;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;     // move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
	VectorScale( dir, 900 + ( crandom() * 100 ), bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );          // save net bandwidth
	VectorCopy( start, bolt->r.currentOrigin );

	return bolt;
}


// Rafael sniper
/*
=================
fire_lead
=================
*/

void fire_lead( gentity_t *self, vec3_t start, vec3_t dir, int damage ) {

	trace_t tr;
	vec3_t end;
	gentity_t       *tent;
	gentity_t       *traceEnt;
	vec3_t forward, right, up;
	vec3_t angles;
	float r, u;
	qboolean anti_tank_enable = qfalse;

	r = crandom() * self->random;
	u = crandom() * self->random;

	vectoangles( dir, angles );
	AngleVectors( angles, forward, right, up );

	VectorMA( start, 8192, forward, end );
	VectorMA( end, r, right, end );
	VectorMA( end, u, up, end );

	trap_Trace( &tr, start, NULL, NULL, end, self->s.number, MASK_SHOT );
	if ( tr.surfaceFlags & SURF_NOIMPACT ) {
		return;
	}

	traceEnt = &g_entities[ tr.entityNum ];

	// snap the endpos to integers, but nudged towards the line
	SnapVectorTowards( tr.endpos, start );

	// send bullet impact
	if ( traceEnt->takedamage && traceEnt->client ) {
		tent = G_TempEntity( tr.endpos, EV_BULLET_HIT_FLESH );
		tent->s.eventParm = traceEnt->s.number;
	} else {
		// Ridah, bullet impact should reflect off surface
		vec3_t reflect;
		float dot;

		tent = G_TempEntity( tr.endpos, EV_BULLET_HIT_WALL );

		dot = DotProduct( forward, tr.plane.normal );
		VectorMA( forward, -2 * dot, tr.plane.normal, reflect );
		VectorNormalize( reflect );

		tent->s.eventParm = DirToByte( reflect );
		// done.
	}
	tent->s.otherEntityNum = self->s.number;

	if ( traceEnt->takedamage ) {

		if ( self->s.weapon == WP_SNIPER
			 && traceEnt->s.eType == ET_MOVER
			 && traceEnt->aiName[0] ) {
			anti_tank_enable = qtrue;
		}

		if ( anti_tank_enable ) {
			self->s.weapon = WP_PANZERFAUST;
		}

		G_Damage( traceEnt, self, self, forward, tr.endpos,
				  damage, 0, MOD_MACHINEGUN );

		if ( anti_tank_enable ) {
			self->s.weapon = WP_SNIPER;
		}

	}

}


// Rafael sniper
// visible

/*
==============
visible
==============
*/
qboolean visible( gentity_t *self, gentity_t *other ) {
//	vec3_t		spot1;
//	vec3_t		spot2;
	trace_t tr;
	gentity_t   *traceEnt;

	trap_Trace( &tr, self->r.currentOrigin, NULL, NULL, other->r.currentOrigin, self->s.number, MASK_SHOT );

	traceEnt = &g_entities[ tr.entityNum ];

	if ( traceEnt == other ) {
		return qtrue;
	}

	return qfalse;

}



/*
==============
fire_mortar
	dir is a non-normalized direction/power vector
==============
*/
gentity_t *fire_mortar( gentity_t *self, vec3_t start, vec3_t dir ) {
	gentity_t   *bolt;

//	VectorNormalize (dir);

	if ( self->spawnflags ) {
		gentity_t   *tent;
		tent = G_TempEntity( self->s.pos.trBase, EV_MORTAREFX );
		tent->s.density = self->spawnflags; // send smoke and muzzle flash flags
		VectorCopy( self->s.pos.trBase, tent->s.origin );
		VectorCopy( self->s.apos.trBase, tent->s.angles );
	}

	bolt = G_Spawn();
	bolt->classname = "mortar";
	bolt->nextthink = level.time + 20000;   // push it out a little
	bolt->think = G_ExplodeMissile;
	bolt->s.eType = ET_MISSILE;

	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN | SVF_BROADCAST;   // broadcast sound.  not multiplayer friendly, but for mortars it should be okay
	// bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;

	bolt->s.weapon = WP_MORTAR;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = G_GetWeaponDamage( WP_MORTAR ); // JPW NERVE
	bolt->splashDamage = G_GetWeaponDamage( WP_MORTAR ); // JPW NERVE
	bolt->splashRadius = 120;
	bolt->methodOfDeath = MOD_MORTAR;
	bolt->splashMethodOfDeath = MOD_MORTAR_SPLASH;
	bolt->clipmask = MASK_MISSILESHOT;

	bolt->s.pos.trType = TR_GRAVITY;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;     // move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
//	VectorScale( dir, 900, bolt->s.pos.trDelta );
	VectorCopy( dir, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );          // save net bandwidth
	VectorCopy( start, bolt->r.currentOrigin );

	return bolt;
}

