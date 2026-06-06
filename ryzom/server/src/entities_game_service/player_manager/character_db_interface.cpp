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
#include "nel/misc/bit_mem_stream.h"

// Game share
#include "game_share/character_sync_itf.h"

// Player manager
#include "player_manager/character.h"
#include "player_manager/player_manager.h"
#include "player_manager/cdb_branch.h"

// DB
#include "cdb_struct_banks.h"
#include "cdb_check_sum.h"

// EGS
#include "entities_game_service.h"
#include "entities_game_service/egs_variables.h"

// For DBOutput global and IShardUnifierEvent
#include "modules/shard_unifier_client.h"

//-----------------------------------------------------------------------------
///////////
// USING //
///////////
using namespace std;
using namespace NLMISC;
using namespace NLNET;
using namespace EGSPD;

extern NLMISC::CBitMemStream DBOutput; // global to avoid reallocation (from main character.cpp)

// Local copies of some vars used in the moved code (to make the TU self contained)
const uint32 MaxBonusMalusDisplayed = 12;
extern CVariable<uint32> DefaultWeightHands;

void CCharacter::initDatabase()
{
	// Load the database, and prepare database outbox
//	_PropertyDatabase.init( CDBPlayer );
	_PropertyDatabase.init( );

	// Target
//	_PropertyDatabase.setProp( _DataIndexReminder->TARGET.UID, CLFECOMMON::INVALID_CLIENT_DATASET_INDEX );
	CBankAccessor_PLR::getTARGET().getBARS().setUID(_PropertyDatabase, CLFECOMMON::INVALID_CLIENT_DATASET_INDEX );


	// set player max bulk
//	_PropertyDatabase.setProp( "STATIC_DATA:BAG_BULK_MAX", MaxPlayerBulk / 1000 );
	CBankAccessor_PLR::getSTATIC_DATA().setBAG_BULK_MAX(_PropertyDatabase, MaxPlayerBulk / 1000 );

	// set player room max bulk
//	_PropertyDatabase.setProp( "INVENTORY:ROOM:BULK_MAX", BasePlayerRoomBulk / 1000 );
	CBankAccessor_PLR::getINVENTORY().getROOM().setBULK_MAX(_PropertyDatabase, BasePlayerRoomBulk / 1000 );

	// GROUP is empty
	for (uint i = 0 ; i < 8 ; ++i)
	{
//		_PropertyDatabase.setProp( NLMISC::toString("GROUP:%d:PRESENT",i), 0 );
		CBankAccessor_PLR::getGROUP().getArray(i).setPRESENT(_PropertyDatabase, false );
//		_PropertyDatabase.setProp( NLMISC::toString("GROUP:%d:NAME",i), 0 );
		CBankAccessor_PLR::getGROUP().getArray(i).setNAME(_PropertyDatabase, 0 );
//		_PropertyDatabase.setProp( NLMISC::toString("GROUP:%d:UID",i), CLFECOMMON::INVALID_CLIENT_DATASET_INDEX );
		CBankAccessor_PLR::getGROUP().getArray(i).setUID(_PropertyDatabase, CLFECOMMON::INVALID_CLIENT_DATASET_INDEX );
	}

	// PACK_ANIMAL is empty
	for (uint i = 0 ; i < MAX_INVENTORY_ANIMAL; ++i)
	{
//		_PropertyDatabase.setProp( NLMISC::toString("PACK_ANIMAL:BEAST%d:UID",i), CLFECOMMON::INVALID_CLIENT_DATASET_INDEX );
		CBankAccessor_PLR::getPACK_ANIMAL().getBEAST(i).setUID(_PropertyDatabase, CLFECOMMON::INVALID_CLIENT_DATASET_INDEX );
	}

//	_PropertyDatabase.setPropButDontSend( "BUILDING_SENTENCE:COUNTER", 0 );

	// modifiers
	for (uint i = 0 ; i < MaxBonusMalusDisplayed ; ++i)
	{
		CBankAccessor_PLR::TMODIFIERS::TMALUS::TArray &malusElem = CBankAccessor_PLR::getMODIFIERS().getMALUS().getArray(i);
		CBankAccessor_PLR::TMODIFIERS::TBONUS::TArray &bonusElem = CBankAccessor_PLR::getMODIFIERS().getBONUS().getArray(i);
//		_PropertyDatabase.setProp( _DataIndexReminder->Modifiers.Malus.Sheet[i], 0);
		malusElem.setSHEET(_PropertyDatabase, CSheetId::Unknown);
//		_PropertyDatabase.setProp( _DataIndexReminder->Modifiers.Malus.Disable[i], 0);
		malusElem.setDISABLED(_PropertyDatabase, false);
//		_PropertyDatabase.setProp( _DataIndexReminder->Modifiers.Malus.DisableTime[i], 0);
		malusElem.setDISABLED_TIME(_PropertyDatabase, 0);
//		_PropertyDatabase.setProp( _DataIndexReminder->Modifiers.Bonus.Sheet[i], 0);
		bonusElem.setSHEET(_PropertyDatabase, CSheetId::Unknown);
//		_PropertyDatabase.setProp( _DataIndexReminder->Modifiers.Bonus.Disable[i], 0);
		bonusElem.setDISABLED(_PropertyDatabase, false);
//		_PropertyDatabase.setProp( _DataIndexReminder->Modifiers.Bonus.DisableTime[i], 0);
		bonusElem.setDISABLED_TIME(_PropertyDatabase, 0);
	}

	//money
//	_PropertyDatabase.setProp( "INVENTORY:MONEY", _Money );
	CBankAccessor_PLR::getINVENTORY().setMONEY( _PropertyDatabase, _Money );

	//Temporary until managed by AI
//	_PropertyDatabase.setProp( _DataIndexReminder->TARGET.CONTEXT_VAL, 0xffff );
	CBankAccessor_PLR::getTARGET().setCONTEXT_VAL(_PropertyDatabase, 0xffff );

	// interfaces flags
//	_PropertyDatabase.setProp( "INTERFACES:FLAGS", 0);
	CBankAccessor_PLR::getINTERFACES().setFLAGS(_PropertyDatabase, 0);

	// combat flags
	//_ForbidPowerDates.writeUsablePowerFlags(_UsablePowerFlags);
	setPowerFlagDates();
	setAuraFlagDates();
	updateBrickFlagsDBEntry();

	// defense interface
	for (uint i = 0 ; i < 6 ; ++i)
	{
//		_PropertyDatabase.setProp( NLMISC::toString("DEFENSE:SLOTS:%d:MODIFIER",i), 0 );
		CBankAccessor_PLR::getDEFENSE().getSLOTS().getArray(i).setMODIFIER(_PropertyDatabase, 0 );
	}

	// init death malus
//	_PropertyDatabase.setProp( "USER:DEATH_XP_MALUS", 255 );
	CBankAccessor_PLR::getUSER().setDEATH_XP_MALUS(_PropertyDatabase, 255 );

	// dodge and parry levels
//	_PropertyDatabase.setProp(_DataIndexReminder->CHARACTER_INFO.DodgeBase, _BaseDodgeLevel);
	CBankAccessor_PLR::getCHARACTER_INFO().getDODGE().setBase(_PropertyDatabase, checkedCast<uint16>(_BaseDodgeLevel));
//	_PropertyDatabase.setProp(_DataIndexReminder->CHARACTER_INFO.DodgeCurrent, _CurrentDodgeLevel);
	CBankAccessor_PLR::getCHARACTER_INFO().getDODGE().setCurrent(_PropertyDatabase, checkedCast<uint16>(_CurrentDodgeLevel));
//	_PropertyDatabase.setProp(_DataIndexReminder->CHARACTER_INFO.ParryBase, _BaseParryLevel);
	CBankAccessor_PLR::getCHARACTER_INFO().getPARRY().setBase(_PropertyDatabase, checkedCast<uint16>(_BaseParryLevel));
//	_PropertyDatabase.setProp(_DataIndexReminder->CHARACTER_INFO.ParryCurrent, _CurrentParryLevel);
	CBankAccessor_PLR::getCHARACTER_INFO().getPARRY().setCurrent(_PropertyDatabase, checkedCast<uint16>(_CurrentParryLevel));

//	_PropertyDatabase.setProp( "USER:SPEED_FACTOR", sint64(100) );
	CBankAccessor_PLR::getUSER().setSPEED_FACTOR(_PropertyDatabase, 100 );

	// consumable modifiers
	parrySuccessModifier(0);
	dodgeSuccessModifier(0);
	craftSuccessModifier(0);
	meleeSuccessModifier(0);
	rangeSuccessModifier(0);
	magicSuccessModifier(0);
	_ForageSuccessModifiers.resize( ECOSYSTEM::NUM_ECOSYSTEM );
	for(uint8 i = 0; i < (uint8)ECOSYSTEM::NUM_ECOSYSTEM; ++i )
		forageSuccessModifier((ECOSYSTEM::EECosystem)i,0);

//	_PropertyDatabase.setProp("USER:DEFAULT_WEIGHT_HANDS", DefaultWeightHands);
	CBankAccessor_PLR::getUSER().setDEFAULT_WEIGHT_HANDS(_PropertyDatabase, DefaultWeightHands);
} // initDatabase //
void CCharacter::databaseUpdate()
{
	// Write the inventory updates
	_InventoryUpdater.sendAllUpdates( _Id ); // must be before the sending of _PropertyDatabase, because it tests _PropertyDatabase.notSentYet()

	// Write the character's database delta (for comment numbers, see tutorial in cdb_group.h)
	if ( _PropertyDatabase.getChangedPropertyCount() != 0 ) // ensures writeDelta() will return true
	{
		DBOutput.resetBufPos();
		bool hasContentToSend = true;
		if ( _PropertyDatabase.notSentYet() )
		{
			// The first message has a different name, because the client must know that it's the
			// first message to inhibit its oberver callbacks, although it is not garanteed that
			// it's the first message to arrive on the client (see impulsion channels on the FS).
			GenericMsgManager.pushNameToStream( "DB_INIT:PLR", DBOutput );
			// write the server tick, to ensure old DB update are not applied after newer
			TGameCycle	serverTick= CTickEventHandler::getGameCycle();
			DBOutput.serial(serverTick);
			// write the delta DB
			_PropertyDatabase.writeDelta( DBOutput, ~0 ); // no size limit for first sending
			//egs_chinfo( "Sending 1st database packet" );
		}
		else
		{
			uint16 databaseImpulseWindowBitSize = _AvailImpulseBitsize.isReadable() ? _AvailImpulseBitsize() : 91*8;
			sint32 characterBankMaxBitSize = ((sint32)databaseImpulseWindowBitSize);
			if ( characterBankMaxBitSize > 0 )
			{
				// Write using bandwith limit
				GenericMsgManager.pushNameToStream( "DB_UPD_PLR", DBOutput );
				// write the server tick, to ensure old DB update are not applied after newer
				TGameCycle	serverTick= CTickEventHandler::getGameCycle();
				DBOutput.serial(serverTick);
				// write the delta DB
				_PropertyDatabase.writeDelta( DBOutput, (uint32)characterBankMaxBitSize );
			}
			else
				hasContentToSend = false;
		}

		// "Client only" property changes
		/*bool hasCOPropChanges = _PropertyDatabase.hasClientonlyPropertyChanges();
		DBOutput.serialBit( hasCOPropChanges );
		if ( hasCOPropChanges )
			_PropertyDatabase.writeClientonlyPropertyChanges( DBOutput );*/

		// Send impulsion to front-end service
		if ( hasContentToSend )
		{
			CMessage msgout( "CDB_IMPULSION" );
			msgout.serial( _Id );
			msgout.serialBufferWithSize( (uint8*)DBOutput.buffer(), DBOutput.length() );
			CUnifiedNetwork::getInstance()->send( NLNET::TServiceId(_Id.getDynamicId()), msgout );
		}
	}

} // databaseUpdate //
void CCharacter::fillCharInfo(CHARSYNC::TCharInfo &charInfo) const
{
	charInfo.setCharEId(getId());
	charInfo.setCharName(getName());
	charInfo.setHomeSessionId(getHomeMainlandSessionId());
	charInfo.setBestCombatLevel(max(getBestChildSkillValue(SKILLS::SF), getBestChildSkillValue(SKILLS::SMO)));
	charInfo.setGuildId(getGuildId());
	charInfo.setRace((CHARSYNC::TRace::TValues)getRace());
	std::pair<PVP_CLAN::TPVPClan, PVP_CLAN::TPVPClan> allegiance = getAllegiance();
	std::pair<CHARSYNC::TCult, CHARSYNC::TCivilisation> charSyncAll = IShardUnifierEvent::convertAllegiance(allegiance);
	charInfo.setCult(charSyncAll.first);
	charInfo.setCivilisation(charSyncAll.second);
	charInfo.setRespawnPoints(getRespawnPoints().buildRingPoints());
	charInfo.setNewcomer(isNewbie());
}
