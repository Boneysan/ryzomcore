// Ryzom - MMORPG Framework <http://dev.ryzom.com/projects/ryzom/>
// Copyright (C) 2010  Winch Gate Property Limited
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------------
// includes
//-----------------------------------------------------------------------------

#include "stdpch.h"

/////////////
// INCLUDE //
/////////////

// Misc
#include "nel/misc/hierarchical_timer.h"

// Game share
#include "game_share/msg_client_server.h"
#include "game_share/inventories.h"
#include "game_share/entity_types.h"
#include "game_share/pvp_mode.h"
#include "game_share/fame.h"
#include "game_share/send_chat.h"
#include "game_share/time_weather_season/time_and_season.h"
#include "game_share/utils.h"
#include "game_share/character_sync_itf.h"
#include "game_share/mainland_summary.h"
#include "game_share/roles.h"
#include "game_share/guild_grade.h"
#include "game_share/chat_group.h"
#include "game_share/interface_flags.h"
#include "game_share/visual_fx.h"

// Server share
#include "server_share/used_continent.h"
#include "server_share/stats_status.h"
#include "server_share/npc_description_messages.h"
#include "server_share/r2_vision.h"
#include "server_share/r2_variables.h"

// EGS sheets
#include "egs_sheets/egs_sheets.h"

// Player manager
#include "player_manager/character.h"
#include "player_manager/player_manager.h"
#include "player_manager/character_respawn_points.h"
#include "player_manager/player.h"

// Creature manager
#include "creature_manager/creature_manager.h"

// Team
#include "team_manager/team_manager.h"

// Phrase manager
#include "phrase_manager/phrase_manager.h"
#include "phrase_manager/phrase_utilities_functions.h"

// Progression
#include "progression/progression_pve.h"
#include "progression/progression_pvp.h"

// Zone
#include "zone_manager.h"

// Building
#include "building_manager/building_manager.h"

// PVP
#include "pvp_manager/pvp_manager_2.h"
#include "pvp_manager/pvp.h"
#include "pvp_manager/pvp_manager.h"
#include "pvp_manager/pvp_faction_reward_manager/pvp_faction_reward_manager.h"

// EGS
#include "entities_game_service.h"
#include "entities_game_service/egs_variables.h"

// Death penalties
#include "death_penalties.h"

// Other
#include "cdb_struct_banks.h"
#include "player_manager/cdb_branch.h"

//-----------------------------------------------------------------------------
///////////
// USING //
///////////
using namespace std;
using namespace NLMISC;
using namespace NLNET;
using namespace NLGEORGES;
using namespace EGSPD;

// from entities_game_service.cpp
extern bool TTSIsUp;

//-----------------------------------------------------------------------------
// Death / resurrection related methods moved here from character.cpp as pure
// file split (Strangler Fig, Phase 0.3). No logic changes.
//-----------------------------------------------------------------------------

void CCharacter::kill(TDataSetRow killerRowId)
{
	if (_IsDead)
		return;

	// force player to unmount
	if( TheDataset.isAccessible(_EntityMounted()) )
	{
		unmount();
	}

	std::vector< uint16 > usableRespawnPoints;
	getRespawnPoints().getUsableRespawnPoints(getCurrentContinent(), usableRespawnPoints);
	if ( usableRespawnPoints.empty() )
	{
		getRespawnPoints().addDefaultRespawnPoint(getCurrentContinent());
	}

	_IsDead = true;
	_Mode = MBEHAV::DEATH;

	removeAllSpells();
	_ForbidPowerDates.clearConsumable();

	_PhysScores._PhysicalScores[SCORES::hit_points].Current = -_PhysScores._PhysicalScores[SCORES::hit_points].Max / 2;
	setBars();

	_TimeDeath = CTickEventHandler::getGameTime() + 5.0;

	// stop all temp inventory actions
	sendCloseTempInventoryImpulsion();

	// end quartering if in progress
	endHarvest();

	// stop all temp inventory actions
	clearTempInventory();

	// Testing tools report
	if( TTSIsUp )
	{
		CMessage msgout("TTS_REPORT_ACTOR_DEAD");
		msgout.serial( _Id );
		CUnifiedNetwork::getInstance()->send( "TTS", msgout );
	}

	_ContextualProperty.directAccessForStructMembers().talkableTo( false );
	_ContextualProperty.setChanged();

	CPhraseManager::getInstance().removeEntity(_EntityRowId, false);

	if (_TpTicketSlot != INVENTORIES::INVALID_INVENTORY_SLOT)
	{
		unLockItem(INVENTORIES::bag, _TpTicketSlot,1);
		resetTpTicketSlot();
	}

	// Output Stats
	string placeName = string("None");
	string regionName = string("None");
	if ( !_Places.empty() )
	{
		CPlace * p = CZoneManager::getInstance().getPlaceFromId( _Places[0] );
		if( p )
			placeName = p->getName();
	}

	const CRegion * r=nullptr;
	CZoneManager::getInstance().getRegion( this, &r);
	if( r )
	{
		regionName = r->getName();
	}

	// if best skill of player is too low then he doesn't get death penalty
	if( getBestChildSkillValue(SKILLS::any) < DeathPenaltyMinLevel )
	{
		setNextDeathPenaltyFactor(0.0f);
	}

	// check killer and do things accordingly
	CEntityId killerId;
	CSheetId killerSheet;
	CEntityBase * e = CEntityBaseManager::getEntityBasePtr(killerRowId);
	if( e )
	{
		// clear XP gain for this player
		PROGRESSIONPVE::CCharacterProgressionPVE::getInstance()->clearAllXpForPlayer(_EntityRowId, _TeamId, false);
		if( e->getId().getType() == RYZOMID::player )
		{
			PROGRESSIONPVP::CCharacterProgressionPVP::getInstance()->playerDeath(this, (CCharacter*)e);
		}

		killerId = e->getId();
		killerSheet = e->getType();

		if ( ! (getPVPInterface().isValid() &&
			    getPVPInterface().killedBy( e )) )
		{
			// if killed by a creature, check if death penalty should be applied or not
			if ( killerId.getType() == RYZOMID::creature || killerId.getType() == RYZOMID::npc )
			{
				CCreature *creature = dynamic_cast<CCreature*> (e);
				if (creature && creature->getForm())
				{
					if ( creature->getForm()->getXPGainOnCreature() == 0 )
					{
						setNextDeathPenaltyFactor(0.0f);
					}
				}
			}
		}

		if( killerId.getType() == RYZOMID::player )
		{
			if( getDuelOpponent() )
			{
				if( getDuelOpponent()->getId() != killerId )
				{
					setNextDeathPenaltyFactor(0.0f);
				}
			}
			else
			{
				setNextDeathPenaltyFactor(0.0f);
			}
		}
	}
	//Bsi.append( StatPath, NLMISC::toString("[PJM] %s %s %s %s %s %s", _Id.toString().c_str(), CONTINENT::toString(_CurrentContinent).c_str(), regionName.c_str(), placeName.c_str(), KillerId.toString().c_str(), KillerSheet.toString().c_str()) );
	//EgsStat.displayNL("[PJM] %s %s %s %s %s %s", _Id.toString().c_str(), CONTINENT::toString(_CurrentContinent).c_str(), regionName.c_str(), placeName.c_str(), KillerId.toString().c_str(), KillerSheet.toString().c_str());
//	EGSPD::pCDead(_Id, CONTINENT::toString(_CurrentContinent), regionName, placeName, killerId, killerSheet.toString());
} // kill //


//---------------------------------------------------
// character is dead
//
//---------------------------------------------------
void CCharacter::deathOccurs( void )
{
	H_AUTO(DeathOccursCharacter);

	if( currentHp() > 0 )
	{
		resurrected();
		return;
	}

	if ( getPVPInterface().isValid() )
	{
		// ignore PVP death
		const bool cancelRespawn = getPVPInterface().doCancelRespawn();

		getPVPInterface().leavePVP(IPVP::Death);

		if (cancelRespawn)
			return;
	}

	CPVPManager2::getInstance()->playerDies(this);

	CBuildingManager::getInstance()->removeTriggerRequest(getEntityRowId());

	if( _TimeDeath < CTickEventHandler::getGameTime() )
	{
		if (_Mode.getValue().Mode == MBEHAV::DEATH)
		{
			// Cancel all action during death
			cancelStaticActionInProgress();

			if( _Mode.getValue().Mode == MBEHAV::DEATH && _IsDead == true )
			{
				_TimeDeath = CTickEventHandler::getGameTime() + CommaDelayBeforeDeath;
				CPhraseManager::getInstance().removeEntity(TheDataset.getDataSetRow(_Id), false);
			}
			else
			{
				//todo make necessary for stop vision of character
			}
		}
	}

	// update regen
	if( !_IsInAComa )
	{
		resetCharacterModifier();
		computeMaxValue();

		// negative regen giving healing times for resurrect character
		for( uint32 i = 0; i < SCORES::NUM_SCORES; ++i )
		{
			if( i == SCORES::hit_points )
			{
				float currentRegen = - _PhysScores._PhysicalScores[ i ].Max / (CommaDelayBeforeDeath * 0.2f );
				_PhysScores._PhysicalScores[ i ].CurrentRegenerate = currentRegen;
				_PhysScores._PhysicalScores[ i ].Current = - _PhysScores._PhysicalScores[ i ].Max / 2;
			}
			else
			{
				float currentRegen = - _PhysScores._PhysicalScores[ i ].Current / ( CommaDelayBeforeDeath * 0.1f );
				_PhysScores._PhysicalScores[ i ].CurrentRegenerate = currentRegen;
			}
		}
		_IsInAComa = true;
	}

	for( uint32 i = 0; i < SCORES::NUM_SCORES; ++i )
	{
		sint32 oldCurrent = _PhysScores._PhysicalScores[ i ].Current;
		if( i == SCORES::hit_points )
		{
			if( _PhysScores._PhysicalScores[ i ].Current > - _PhysScores._PhysicalScores[ i ].Max )
			{
				_PhysScores._PhysicalScores[ i ].KeepRegenerateDecimal += _PhysScores._PhysicalScores[ i ].CurrentRegenerate * ( CTickEventHandler::getGameCycle() - _PhysScores._PhysicalScores[ i ].RegenerateTickUpdate ) / 10.0f;
				_PhysScores._PhysicalScores[ i ].Current = (sint32) ( _PhysScores._PhysicalScores[ i ].Current + (sint32) _PhysScores._PhysicalScores[ i ].KeepRegenerateDecimal );
				_PhysScores._PhysicalScores[ i ].KeepRegenerateDecimal -= (sint32) _PhysScores._PhysicalScores[ i ].KeepRegenerateDecimal;
			}
			if( _PhysScores._PhysicalScores[ i ].Current < -_PhysScores._PhysicalScores[ i ].Max )
			{
				_PhysScores._PhysicalScores[ i ].Current = - _PhysScores._PhysicalScores[ i ].Max;
			}
			_PhysScores._PhysicalScores[ i ].RegenerateTickUpdate = CTickEventHandler::getGameCycle();
		}
		else
		{
			if( _PhysScores._PhysicalScores[ i ].Current > 0 )
			{
				_PhysScores._PhysicalScores[ i ].KeepRegenerateDecimal += _PhysScores._PhysicalScores[ i ].CurrentRegenerate * ( CTickEventHandler::getGameCycle() - _PhysScores._PhysicalScores[ i ].RegenerateTickUpdate ) / 10.0f;
				_PhysScores._PhysicalScores[ i ].Current = (sint32) ( _PhysScores._PhysicalScores[ i ].Current + (sint32) _PhysScores._PhysicalScores[ i ].KeepRegenerateDecimal );
				_PhysScores._PhysicalScores[ i ].KeepRegenerateDecimal -= (sint32) _PhysScores._PhysicalScores[ i ].KeepRegenerateDecimal;
			}
			if( _PhysScores._PhysicalScores[ i ].Current < 0 )
			{
				_PhysScores._PhysicalScores[ i ].Current = 0;
			}
			_PhysScores._PhysicalScores[ i ].RegenerateTickUpdate = CTickEventHandler::getGameCycle();
		}
	}
	setBars();
}


//---------------------------------------------------
// player choose a re-spawn for his death character
//
//---------------------------------------------------
void CCharacter::respawn( uint16 index )
{
	// ignore message if player isn't dead
	if (!_IsDead)
	{
		return;
	}

	PROGRESSIONPVP::CCharacterProgressionPVP::getInstance()->playerRespawn(this);

	sint32 x,y,z;
	float heading;

	if( getRespawnPoints().getRingAdventuresRespawnPoint( x, y ) )
	{
		z = 0;
		heading = 0.0f;
	}
	else
	{
		vector<uint16> points;
		getRespawnPoints().getUsableRespawnPoints(getCurrentContinent(), points);
		if ( index >= points.size() )
		{
			nlwarning("<RESPAWN_POINT> invalid point %u for user %s ( count = %u)",index,_Id.toString().c_str(),points.size());
			index = 0;
			getRespawnPoints().addDefaultRespawnPoint(getCurrentContinent());
			getRespawnPoints().getUsableRespawnPoints(getCurrentContinent(), points);

			if ( index >= points.size() )
			{
				nlwarning("<RESPAWN_POINT> invalid default point %u for user %s. CurrentContinent %d x = %d, y = %d",index,_Id.toString().c_str(), getCurrentContinent(), getState().X(),getState().Y() );
				return;
			}
		}

		// get the tp coords
		const CTpSpawnZone* zone = CZoneManager::getInstance().getTpSpawnZone( points[index] );
		if ( !zone )
		{
			nlwarning("<RESPAWN_POINT> invalid point %u for user %s ( count = %u) ( nullptr zone returned )",index,_Id.toString().c_str(),points.size());
			return;
		}
		zone->getRandomPoint(x,y,z,heading);
	}

	// remove character of vision of other PC
	CMessage msgout("ENTITY_TELEPORTATION");
	msgout.serial( _Id );
	if (IsRingShard)
	{
		nlinfo("Asking GPMS to TP character %s to (0,0) for respawn",_Id.toString().c_str());
	}
	sendMessageViaMirror("GPMS", msgout);

	forbidNearPetTp();

	// set player to intangible state
	_IntangibleEndDate = ~0;

	applyRespawnEffects();

	// tpWanted() sends message CAIPlayerRespawnMsg to AIS
	tpWanted( x, y, z, true, heading );

	// give spire effect if needed
	CPVPFactionRewardManager::getInstance().giveTotemsEffects( this );

	_RegionKilledInPvp = 0xffff;
}

//---------------------------------------------------
// apply respawn effects
//
//---------------------------------------------------
void CCharacter::applyRespawnEffects()
{
	if ( _NextDeathPenaltyFactor != 0 )
		_DeathPenalties->addDeath(*this, _NextDeathPenaltyFactor);
	resetNextDeathPenaltyFactor();

	_PhysScores._PhysicalScores[ SCORES::hit_points ].Current = _PhysScores._PhysicalScores[ SCORES::hit_points ].Base / 10;
	_PhysScores._PhysicalScores[ SCORES::stamina ].Current = _PhysScores._PhysicalScores[ SCORES::stamina ].Base / 10;
	_PhysScores._PhysicalScores[ SCORES::sap ].Current = _PhysScores._PhysicalScores[ SCORES::sap ].Base / 10;
	_PhysScores._PhysicalScores[ SCORES::focus ].Current = _PhysScores._PhysicalScores[ SCORES::focus ].Base / 10;
	_Mode = MBEHAV::NORMAL;
	_Behaviour = MBEHAV::IDLE;
	_IsDead = false;
	_IsInAComa = false;
}

//---------------------------------------------------
// player accept resurrection by other character
//
//---------------------------------------------------
void CCharacter::resurrected()
{
	_Mode = MBEHAV::NORMAL;
	_Behaviour = MBEHAV::IDLE;
	_IsDead = false;
	_IsInAComa = false;
	resetNextDeathPenaltyFactor();

	PROGRESSIONPVP::CCharacterProgressionPVP::getInstance()->playerResurrected(this);

	// give spire effect if needed
	CPVPFactionRewardManager::getInstance().giveTotemsEffects( this );

	_RegionKilledInPvp = 0xffff;
}


//---------------------------------------------------
// revive
// player revives at full health at his location without death penalty
//---------------------------------------------------
void CCharacter::revive()
{
	_Mode = MBEHAV::NORMAL;
	_Behaviour = MBEHAV::IDLE;
	_IsDead = false;
	_IsInAComa = false;
	_RegionKilledInPvp = 0xffff;

	_PhysScores._PhysicalScores[ SCORES::hit_points ].Current = _PhysScores._PhysicalScores[ SCORES::hit_points ].Base;
	_PhysScores._PhysicalScores[ SCORES::stamina ].Current = _PhysScores._PhysicalScores[ SCORES::stamina ].Base;
	_PhysScores._PhysicalScores[ SCORES::sap ].Current = _PhysScores._PhysicalScores[ SCORES::sap ].Base;
	_PhysScores._PhysicalScores[ SCORES::focus ].Current = _PhysScores._PhysicalScores[ SCORES::focus ].Base;
}
