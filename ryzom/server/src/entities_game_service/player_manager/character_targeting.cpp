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

// Server share
#include "server_share/used_continent.h"
#include "server_share/r2_vision.h"
#include "server_share/r2_variables.h"

// EGS sheets
#include "egs_sheets/egs_sheets.h"

// Player manager
#include "player_manager/character.h"
#include "player_manager/player_manager.h"
#include "player_manager/player.h"

// Creature manager
#include "creature_manager/creature_manager.h"

// Phrase
#include "phrase_manager/phrase_manager.h"
#include "phrase_manager/phrase_utilities_functions.h"

// Mission
#include "mission_manager/mission_manager.h"
#include "mission_manager/mission_solo.h"
#include "mission_manager/mission_team.h"

// Team
#include "team_manager/team_manager.h"

// PVP
#include "pvp_manager/pvp_manager_2.h"
#include "pvp_manager/pvp_faction_reward_manager/pvp_faction_reward_manager.h"

// EGS
#include "entities_game_service.h"
#include "entities_game_service/egs_variables.h"

// R2
#include "modules/r2_give_item.h"

// Outpost (for botchat outpost code in setTargetBotchatProgramm)
#include "outpost_manager/outpost_manager.h"

// Guild modules (for CGuildMemberModule in context menu code)
#include "guild_manager/guild_member_module.h"
#include "guild_manager/guild_manager.h"

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

void CCharacter::setTarget( const CEntityId &targetId, bool sendMessage )
{
	// If target is the same, do not send message
	if ( _Target() == TheDataset.getDataSetRow( targetId ) && !IsRingShard )
		return;

	CR2GiveItem::getInstance().onUntarget( this, _Target() );

	removeTargetingChar( _Target() );

	CEntityBase * target = CEntityBaseManager::getEntityBasePtr( targetId );
	if( target )
	{
		if( !R2_VISION::isEntityVisibleToPlayers(target->getWhoSeesMe()) )
			return;
	}

	//check entity exists and is targetable
	if (targetId != CEntityId::Unknown)
	{
		// get data set row
		TDataSetRow rowId = TheDataset.getDataSetRow(targetId);
		if ( TheDataset.isAccessible( rowId ) )
		{
			addTargetingChar(rowId);
			// get contextual properties and check targetable
			const CMirrorPropValue<TYPE_CONTEXTUAL> contextualProperties(TheDataset, rowId, DSPropertyCONTEXTUAL );
			const CProperties prop(contextualProperties.getValue());
			if (!prop.selectable())
			{
				return;
			}
		}
	}

	//if targeting an entity which is already targeted, set the mirror value to "invalid" before, so that
	// the onTarget callback is sent.
	// On a RingShard,We want that a targeted entity can be targeted again without selecting another entity.
	if (IsRingShard)
	{
		CEntityId invalidTarget = CEntityId::Unknown;
		CEntityBase::setTarget( invalidTarget );
	}

	CEntityBase::setTarget( targetId );

	// Forage source are not handled by CEntityBaseManager
	if ( targetId.getType() == RYZOMID::forageSource )
	{
		CEntityBase::setTarget( targetId );
		// TODO: mission event?
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.UID, CLFECOMMON::INVALID_CLIENT_DATASET_INDEX );
		CBankAccessor_PLR::getTARGET().getBARS().setUID(_PropertyDatabase, CLFECOMMON::INVALID_CLIENT_DATASET_INDEX );
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.HP, 0 );
		CBankAccessor_PLR::getTARGET().getBARS().setHP(_PropertyDatabase, 0 );
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.SAP, 0 );
		CBankAccessor_PLR::getTARGET().getBARS().setSAP(_PropertyDatabase, 0 );
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.STA, 0 );
		CBankAccessor_PLR::getTARGET().getBARS().setSTA(_PropertyDatabase, 0 );
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.FOCUS, 0 );
		CBankAccessor_PLR::getTARGET().getBARS().setFOCUS(_PropertyDatabase, 0 );

		//_PropertyDatabase.setProp( "TARGET:AGGRESSIVE", 0 );
//		_PropertyDatabase.setProp( "TARGET:FORCE_RATIO", 0 );
		CBankAccessor_PLR::getTARGET().setFORCE_RATIO(_PropertyDatabase, 0 );
		return;
	}

	//uint agressiveness = 0;
	uint rangeLevel = 0;

	// reset combat event flags
	resetCombatEventFlags();

	// Get target, his Hp, and set TARGET HP in the database
	target = CEntityBaseManager::getEntityBasePtr( _Target() );
	if( target )
	{
		CCreature * creature = dynamic_cast< CCreature *>(target);
		if( creature )
		{
			const CStaticCreatures * form = creature->getForm();
			if( form )
			{
				if (form->getLevel() == 0)
					rangeLevel = 0;
				else
				{
					rangeLevel = ( ((form->getLevel() - 1) / 5) << 1) + ( ((form->getLevel()-1) % 5) >= 2 ? 2 : 1 );

					if (rangeLevel > 11)
						rangeLevel = 11;
				}
			}
		}

		if ( sendMessage )
		{
			if (targetId == _Id)
			{
				PHRASE_UTILITIES::sendDynamicSystemMessage( _EntityRowId, "TARGET_SELF");
			}
			else if( targetId != CEntityId::Unknown )
			{
				SM_STATIC_PARAMS_1(params, STRING_MANAGER::entity);
				params[0].setEIdAIAlias( targetId, CAIAliasTranslator::getInstance()->getAIAlias(targetId) );
				PHRASE_UTILITIES::sendDynamicSystemMessage( _EntityRowId, "TARGET_NEW", params);
			}
		}

		// Process mission event "target" until all steps "target" of all missions have been processed
		CMissionEventTarget event( target->getEntityRowId() );
		processMissionMultipleEvent( event );

		// set botchat programm and enable filter is needed
		setTargetBotchatProgramm( target, targetId );

		// UID
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.UID, target->getEntityRowId().getCompressedIndex() );
		CBankAccessor_PLR::getTARGET().getBARS().setUID(_PropertyDatabase, target->getEntityRowId().getCompressedIndex() );

		sint8 percent;
		// Hp
		if( target->getPhysScores()._PhysicalScores[ SCORES::hit_points ].Max == 0 )
		{
			percent = 0;
		}
		else
		{
			percent = sint8( (127.0 * ( target->getPhysScores()._PhysicalScores[ SCORES::hit_points ].Current ) ) / ( target->getPhysScores()._PhysicalScores[ SCORES::hit_points ].Max ) );
		}
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.HP, percent );
		CBankAccessor_PLR::getTARGET().getBARS().setHP(_PropertyDatabase, percent );

		// Sap
		if( target->getPhysScores()._PhysicalScores[ SCORES::sap ].Max == 0 )
		{
			percent = 0;
		}
		else
		{
			sint8 percentTmp = sint8( (127.0 * ( target->getPhysScores()._PhysicalScores[ SCORES::sap ].Current ) ) / ( target->getPhysScores()._PhysicalScores[ SCORES::sap ].Max ) );
			if( percentTmp < 0 )
				percent = 0;
			else
				percent = percentTmp;
		}
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.SAP, percent );
		CBankAccessor_PLR::getTARGET().getBARS().setSAP(_PropertyDatabase, percent );

		// Stamina
		if( target->getPhysScores()._PhysicalScores[ SCORES::stamina ].Max == 0 )
		{
			percent = 0;
		}
		else
		{
			sint8 percentTmp = sint8( (127.0 * ( target->getPhysScores()._PhysicalScores[ SCORES::stamina ].Current ) ) / ( target->getPhysScores()._PhysicalScores[ SCORES::stamina ].Max ) );
			if( percentTmp < 0 )
				percent = 0;
			else
				percent = percentTmp;
		}
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.STA, percent );
		CBankAccessor_PLR::getTARGET().getBARS().setSTA(_PropertyDatabase, percent );

		// Focus
		if( target->getPhysScores()._PhysicalScores[ SCORES::focus].Max == 0 )
		{
			percent = 0;
		}
		else
		{
			sint8 percentTmp = sint8( (127.0 * ( target->getPhysScores()._PhysicalScores[ SCORES::focus ].Current ) ) / ( target->getPhysScores()._PhysicalScores[ SCORES::focus ].Max ) );
			if( percentTmp < 0 )
				percent = 0;
			else
				percent = percentTmp;
		}
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.FOCUS, percent );
		CBankAccessor_PLR::getTARGET().getBARS().setFOCUS(_PropertyDatabase, percent );

		// Validate properties of target
		CProperties prop;
		// set all flags to true as this bifield is used as a AND mask on the client side
		prop.setAllFlags();
		// set the mountable property
		if( target->getContextualProperty().directAccessForStructMembers().mountable() )
		{
			if( (getPlayerPet( target->getEntityRowId() ) == -1) || TheDataset.getEntityId( _EntityMounted() ) == target->getId() )
			{
				prop.mountable( false );
			}
		}
		if ( target->getId().getType() == RYZOMID::player )
		{
			CCharacter * c = dynamic_cast<CCharacter *>(target);
			if (c)
			{
				// Set the invitable property
				if ( ! TeamManager.isInvitableBy(c, this) )
					prop.invitable( false );
				// if any of the character is god, don't allow to team
				if (c->godMode() || godMode())
					prop.invitable( false );

				// Set the level in database for ForceRegion/ForceLevel deduction
				sint32 skillBaseValue = c->getSkillBaseValue( c->getBestSkill() );
//				_PropertyDatabase.setProp( _DataIndexReminder->TARGET.PLAYER_LEVEL, skillBaseValue );
				CBankAccessor_PLR::getTARGET().getBARS().setPLAYER_LEVEL(_PropertyDatabase, checkedCast<uint8>(skillBaseValue) );
			}
			else
			{
				nlwarning("Entity %s type is player but dynamic_cast in CCharacter * returns nullptr ?!", target->getId().toString().c_str());
			}
		}

		if ( CPVPFactionRewardManager::getInstance().isAttackable( this, target ) )
		{
			prop.attackable( true );
		}

//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.CONTEXT_VAL, (uint16) prop );
		CBankAccessor_PLR::getTARGET().setCONTEXT_VAL(_PropertyDatabase, prop );

//*** Removed by Sadge ***
//		// Ask information about target to AI service
//		CreatureNpcInformation.Character.push_back( _EntityRowId );
//		CreatureNpcInformation.Creature.push_back( target->getEntityRowId() );
//*** ***
	}
	else // target == nullptr
	{
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.UID, CLFECOMMON::INVALID_CLIENT_DATASET_INDEX );
		CBankAccessor_PLR::getTARGET().getBARS().setUID(_PropertyDatabase, CLFECOMMON::INVALID_CLIENT_DATASET_INDEX );
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.HP, 0 );
		CBankAccessor_PLR::getTARGET().getBARS().setHP(_PropertyDatabase, 0 );
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.SAP, 0 );
		CBankAccessor_PLR::getTARGET().getBARS().setSAP(_PropertyDatabase, 0 );
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.STA, 0 );
		CBankAccessor_PLR::getTARGET().getBARS().setSTA(_PropertyDatabase, 0 );
//		_PropertyDatabase.setProp( _DataIndexReminder->TARGET.FOCUS, 0 );
		CBankAccessor_PLR::getTARGET().getBARS().setFOCUS(_PropertyDatabase, 0 );

//		PHRASE_UTILITIES::sendDynamicSystemMessage( _Id, "TARGET_NONE");
	}
	//_PropertyDatabase.setProp( "TARGET:AGGRESSIVE", agressiveness );
//	_PropertyDatabase.setProp( "TARGET:FORCE_RATIO", rangeLevel );
	CBankAccessor_PLR::getTARGET().setFORCE_RATIO(_PropertyDatabase, rangeLevel );
} // setTarget //


//---------------------------------------------------
// setTargetBotchatProgramm:
//---------------------------------------------------
void CCharacter::setTargetBotchatProgramm( CEntityBase * target, const CEntityId& targetId )
{
	uint32 programm = 0;
	// set bot chat programms and npcs special options
	CCreature * c = nullptr;
	if (targetId.getType() == RYZOMID::npc)
	{
		c = dynamic_cast<CCreature *>(target);
		if (c == nullptr)
		{
			nlwarning("This dynamic_cast should not return nullptr");
		}
	}
	if (c)
	{
		programm = c->getBotChatProgram();
		if( programm & ( (uint32)1 << uint32(BOTCHATTYPE::TradeItemFlag) ) )
		{
			enableAppropriateFiltersForSeller( c );
		}

		// guild special features
		if (_GuildId != 0)
		{
			programm &= ~( (uint32)1 << uint32(BOTCHATTYPE::CreateGuildFlag) );
		}
		if (c->getOutpostBuilding() != nullptr)
		{
			bool hasRightsToTradeOutpostBuilding = false;
			// if the target is an outpost building check we have rights to build/construct
			CGuild *pGuild = CGuildManager::getInstance()->getGuildFromId(_GuildId);
			if (pGuild != nullptr)
			{
				CGuildMember *pMember = pGuild->getMemberFromEId(_Id);
				if (pMember != nullptr)
				{
					if ((pMember->getGrade() == EGSPD::CGuildGrade::Leader) ||
						(pMember->getGrade() == EGSPD::CGuildGrade::HighOfficer))
					{
						// Ok the user is a leader or a high officer
						// check that the outpost belongs to its guild
						const COutpost *pO = c->getOutpostBuilding()->getParent();
						if (pO != nullptr)
							if ((pO->isBelongingToAGuild()) &&
								(pO->getOwnerGuild() == _GuildId))
								hasRightsToTradeOutpostBuilding = true;
					}
				}
			}
			if (!hasRightsToTradeOutpostBuilding)
				programm &= ~( (uint32)1 << uint32(BOTCHATTYPE::TradeOutpostBuilding) );
		}
		// solo mission
		uint i  = 0;
		for ( map<TAIAlias, CMission*>::iterator it = getMissionsBegin(); it != getMissionsEnd(); ++it )
		{
			vector< pair< bool, uint32 > > texts;
			(*it).second->sendContextTexts( _EntityRowId, c->getEntityRowId(),texts );
			for ( uint k = 0; k < texts.size(); k++)
			{
				if(i >= NB_CONTEXT_DYN_TEXTS) break; // no more room in the context menu, don't fill more or it'll assert
//				_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:TITLE",i) , texts[k].second );
				CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setTITLE(_PropertyDatabase, texts[k].second );
//				_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:PLAYER_GIFT_NEEDED",i),texts[k].first  );
				CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setPLAYER_GIFT_NEEDED(_PropertyDatabase, texts[k].first );
//				_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:PRIORITY",i), 3  );
				CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setPRIORITY(_PropertyDatabase, 3 );
				i++;
			}
		}

		// group mission
		CTeam * team = TeamManager.getRealTeam(_TeamId);
		if ( team )
		{
			for (uint j = 0 ; j < team->getMissions().size(); j++ )
			{
				vector< pair< bool, uint32 > > texts;
				team->getMissions()[j]->sendContextTexts( _EntityRowId, c->getEntityRowId(),texts );
				for ( uint k = 0; k < texts.size(); k++)
				{
					if(i >= NB_CONTEXT_DYN_TEXTS) break; // no more room in the context menu, don't fill more or it'll assert
//					_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:TITLE",i) , texts[k].second );
					CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setTITLE(_PropertyDatabase, texts[k].second );
//					_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:PLAYER_GIFT_NEEDED",i),texts[k].first  );
					CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setPLAYER_GIFT_NEEDED(_PropertyDatabase, texts[k].first );
//					_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:PRIORITY",i), 3  );
					CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setPRIORITY(_PropertyDatabase, 3 );
					i++;
				}
			}
		}

		//send special contextual texts
		if ( !c->getContextTexts().empty() )
		{
			TVectorParamCheck vect;
			STRING_MANAGER::TParam param;

			param.Type = STRING_MANAGER::player;
			param.setEIdAIAlias( _Id, CAIAliasTranslator::getInstance()->getAIAlias( _Id) );
			vect.push_back( param );

			param.Type = STRING_MANAGER::bot;
			param.setEIdAIAlias( targetId, CAIAliasTranslator::getInstance()->getAIAlias( targetId) );

			vect.push_back( param );

			for ( uint j = 0; j < c->getContextTexts().size(); j++ )
			{
				if(i >= NB_CONTEXT_DYN_TEXTS) break; // no more room in the context menu, don't fill more or it'll assert
				uint32 text = STRING_MANAGER::sendStringToClient(_EntityRowId, c->getContextTexts()[j].first.c_str(),vect );
//				_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:TITLE",i) , text );
				CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setTITLE(_PropertyDatabase, text);
//				_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:PLAYER_GIFT_NEEDED",i) , 0 );
				CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setPLAYER_GIFT_NEEDED(_PropertyDatabase, 0 );
//				_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:PRIORITY",i), 0  );
				CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setPRIORITY(_PropertyDatabase, 2 );
				i++;
			}
		}

		// send auto missions
		for ( uint j = 0; j < c->getMissionVector().size(); j++ )
		{
			const CMissionTemplate * templ = CMissionManager::getInstance()->getTemplate( c->getMissionVector()[j] );
			if ( (templ != nullptr) && !templ->AutoText.empty() )
			{
				if (templ->testPrerequisits(this, false) == MISSION_DESC::PreReqSuccess)
				{
					if(i >= NB_CONTEXT_DYN_TEXTS) break; // no more room in the context menu, don't fill more or it'll assert
					uint32 text = templ->sendAutoText(_EntityRowId,_CurrentInterlocutor);
//					_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:TITLE",i) , text );
					CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setTITLE(_PropertyDatabase, text );
//					_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:PLAYER_GIFT_NEEDED",i) , 0 );
					CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setPLAYER_GIFT_NEEDED(_PropertyDatabase, 0 );
//					_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:PRIORITY",i), 3 );
					CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setPRIORITY(_PropertyDatabase, 3 );
					i++;
				}
			}
		}

		for (; i < NB_CONTEXT_DYN_TEXTS; i++ )
		{
//			_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:TITLE",i) , 0 );
			CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setTITLE(_PropertyDatabase, 0 );
//			_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:PLAYER_GIFT_NEEDED",i) , 0 );
			CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setPLAYER_GIFT_NEEDED(_PropertyDatabase, 0 );
//			_PropertyDatabase.setProp( toString("TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:%u:PRIORITY",i), 0 );
			CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(i).setPRIORITY(_PropertyDatabase, 0 );
		}

		// WebPage Title
		if( programm & (1<<BOTCHATTYPE::WebPageFlag) )
		{
			// send the web page title
			uint32 text;
			if (NLMISC::startsWith(c->getWebPageName(), "MENU_")) // TODO: What is this?
			{
				text = STRING_MANAGER::sendStringToClient(_EntityRowId, c->getWebPageName(), TVectorParamCheck() );
			}
			else
			{
				SM_STATIC_PARAMS_1(params, STRING_MANAGER::literal);
				params[0].Literal= c->getWebPageName();
				text = STRING_MANAGER::sendStringToClient(_EntityRowId, "LITERAL", params );
			}
//          _PropertyDatabase.setProp( "TARGET:CONTEXT_MENU:WEB_PAGE_TITLE" , text );
			CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().setWEB_PAGE_TITLE(_PropertyDatabase, text );

			// send the web page url
			SM_STATIC_PARAMS_1(params, STRING_MANAGER::literal);
#ifdef RYZOM_FORGE
			params[0].Literal = c->getWebPage();
#else
			string url = c->getWebPage();

			url += "&player_eid="+getId().toString();

			// add cheksum : pnj eid
			url += "&teid="+c->getId().toString();

			string defaultSalt = toString(getLastConnectedDate());
			nlinfo(defaultSalt.c_str());
			nlinfo(url.c_str());
			string control = "&hmac="+NLMISC::getHMacSHA1((uint8*)&url[0], (uint32)url.size(), (uint8*)&defaultSalt[0], (uint32)defaultSalt.size()).toString();

			params[0].Literal = url + control;
#endif

			text = STRING_MANAGER::sendStringToClient(_EntityRowId, "LITERAL", params );
//			_PropertyDatabase.setProp( "TARGET:CONTEXT_MENU:WEB_PAGE_URL" , text );
			CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().setWEB_PAGE_URL(_PropertyDatabase, text );
		}
		else
		{
//			_PropertyDatabase.setProp( "TARGET:CONTEXT_MENU:WEB_PAGE_TITLE" , 0 );
			CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().setWEB_PAGE_TITLE(_PropertyDatabase, 0 );
//			_PropertyDatabase.setProp( "TARGET:CONTEXT_MENU:WEB_PAGE_URL" , 0 );
			CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().setWEB_PAGE_URL(_PropertyDatabase, 0 );
		}
		// Outpost
		if( programm & (1<<BOTCHATTYPE::OutpostFlag) )
		{
//			_PropertyDatabase.setProp( "TARGET:CONTEXT_MENU:OUTPOST" , c->getBotChatOutpost().asInt() );
			CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().setOUTPOST(_PropertyDatabase, c->getBotChatOutpost() );
		}
		else
		{
//			_PropertyDatabase.setProp( "TARGET:CONTEXT_MENU:OUTPOST" , 0 );
			CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().setOUTPOST(_PropertyDatabase, CSheetId::Unknown );
		}
	}
	else
	{
		if ( target->getId().getType() == RYZOMID::player )
		{
			CCharacter * c = dynamic_cast<CCharacter *>(target);
			if (c)
			{
				CGuildMemberModule * module;
				if ( _ModulesCont->getModule( module ) )
				{
					if ( c->getGuildId() == 0 && module->canInvite() )
						programm |= 1 << BOTCHATTYPE::GuildInviteFlag;
				}
			}
			else
			{
				nlwarning("Entity %s type is player but dynamic_cast in CCharacter * returns nullptr ?!", target->getId().toString().c_str());
			}
		}
//		_PropertyDatabase.setProp( "TARGET:CONTEXT_MENU:PROGRAMMES", programm );
		CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().setPROGRAMMES(_PropertyDatabase, programm );
//		_PropertyDatabase.setProp( "TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:0:TITLE", 0 );
		CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(0).setTITLE(_PropertyDatabase, 0 );
//		_PropertyDatabase.setProp( "TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:0:PLAYER_GIFT_NEEDED", 0 );
		CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(0).setPLAYER_GIFT_NEEDED(_PropertyDatabase, 0 );
//		_PropertyDatabase.setProp( "TARGET:CONTEXT_MENU:MISSIONS_OPTIONS:0:PRIORITY", 0 );
		CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().getMISSIONS_OPTIONS().getArray(0).setPRIORITY(_PropertyDatabase, 0 );
	}

	// Can attack another player or a npc/creature helping players?
	bool invulnerable = target->getContextualProperty().directAccessForStructMembers().invulnerable();
	if( CPVPManager2::getInstance()->isOffensiveActionValid( this, target, true ) && !invulnerable )
	{
		programm |= 1 << BOTCHATTYPE::Attackable;
	}
	// otherwise if target is a creature/npc, check fame attackable
	else if( c && c->checkFactionAttackable(_Id) )
	{
		programm |= 1 << BOTCHATTYPE::Attackable;
	}
	else if( isEntityAnOutpostEnemy(targetId) && !invulnerable )
	{
		programm |= 1 << BOTCHATTYPE::Attackable;
	}

	if ( CPVPFactionRewardManager::getInstance().isAttackable( this, target ) )
		programm |= 1 << BOTCHATTYPE::Attackable;

//	_PropertyDatabase.setProp( "TARGET:CONTEXT_MENU:PROGRAMMES", programm, true );
	CBankAccessor_PLR::getTARGET().getCONTEXT_MENU().setPROGRAMMES(_PropertyDatabase, programm, true );
}
