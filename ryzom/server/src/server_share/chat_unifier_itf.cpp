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

/////////////////////////////////////////////////////////////////
// WARNING : this is a generated file, don't change it !
/////////////////////////////////////////////////////////////////

#include "stdpch.h"
	
#include "chat_unifier_itf.h"

namespace CHATUNI
{

	/////////////////////////////////////////////////////////////////
	// WARNING : this is a generated file, don't change it !
	/////////////////////////////////////////////////////////////////
	

	const CChatUnifierSkel::TMessageHandlerMap &CChatUnifierSkel::getMessageHandlers() const
	{
		static TMessageHandlerMap handlers;
		static bool init = false;

		if (!init)
		{
			std::pair < TMessageHandlerMap::iterator, bool > res;
			
			res = handlers.insert(std::make_pair(std::string("CUSFT"), &CChatUnifierSkel::sendFarTell_skel));
			// if this assert, you have a doubly message name in your interface definition !
			nlassert(res.second);
			
			init = true;
		}

		return handlers;			
	}
	bool CChatUnifierSkel::fwdOnProcessModuleMessage(NLNET::IModuleProxy *sender, const NLNET::CMessage &message)
	{
		const TMessageHandlerMap &mh = getMessageHandlers();

		TMessageHandlerMap::const_iterator it(mh.find(message.getName()));

		if (it == mh.end())
		{
			return false;
		}

		TMessageHandler cmd = it->second;
		(this->*cmd)(sender, message);

		return true;
	}

	
	void CChatUnifierSkel::sendFarTell_skel(NLNET::IModuleProxy *sender, const NLNET::CMessage &__message)
	{
		H_AUTO(CChatUnifierSkel_sendFarTell_CUSFT);
		NLMISC::CEntityId	senderCharId;
			nlRead(__message, serial, senderCharId);
		bool	havePrivilege;
			nlRead(__message, serial, havePrivilege);
		std::string	destName;
			nlRead(__message, serial, destName);
		std::string	text;
			nlRead(__message, serial, text);
		sendFarTell(sender, senderCharId, havePrivilege, destName, text);
	}
		// IOS forward a tell message to the unifier
		// If IOS can't find the player localy, it forward
		// the tell to the unifier
	void CChatUnifierProxy::sendFarTell(NLNET::IModule *sender, const NLMISC::CEntityId &senderCharId, bool havePrivilege, const std::string &destName, const std::string &text)
	{
		if (_LocalModuleSkel && _LocalModule->isImmediateDispatchingSupported())
		{
			// immediate local synchronous dispatching
			_LocalModuleSkel->sendFarTell(_ModuleProxy->getModuleGateway()->getPluggedModuleProxy(sender), senderCharId, havePrivilege, destName, text);
		}
		else
		{
			// send the message for remote dispatching and execution or local queing 
			NLNET::CMessage __message;
			
			buildMessageFor_sendFarTell(__message, senderCharId, havePrivilege, destName, text);

			_ModuleProxy->sendModuleMessage(sender, __message);
		}
	}

	// Message serializer. Return the message received in reference for easier integration
	const NLNET::CMessage &CChatUnifierProxy::buildMessageFor_sendFarTell(NLNET::CMessage &__message, const NLMISC::CEntityId &senderCharId, bool havePrivilege, const std::string &destName, const std::string &text)
	{
		__message.setType("CUSFT");
			nlWrite(__message, serial, const_cast < NLMISC::CEntityId& > (senderCharId));
			nlWrite(__message, serial, havePrivilege);
			nlWrite(__message, serial, const_cast < std::string& > (destName));
			nlWrite(__message, serial, const_cast < std::string& > (text));


		return __message;
	}

	/////////////////////////////////////////////////////////////////
	// WARNING : this is a generated file, don't change it !
	/////////////////////////////////////////////////////////////////
	

	const CChatUnifierClientSkel::TMessageHandlerMap &CChatUnifierClientSkel::getMessageHandlers() const
	{
		static TMessageHandlerMap handlers;
		static bool init = false;

		if (!init)
		{
			std::pair < TMessageHandlerMap::iterator, bool > res;
			
			res = handlers.insert(std::make_pair(std::string("CURFTF"), &CChatUnifierClientSkel::recvFarTellFail_skel));
			// if this assert, you have a doubly message name in your interface definition !
			nlassert(res.second);
			
			res = handlers.insert(std::make_pair(std::string("CURFT"), &CChatUnifierClientSkel::recvFarTell_skel));
			// if this assert, you have a doubly message name in your interface definition !
			nlassert(res.second);
			
			res = handlers.insert(std::make_pair(std::string("CURFGC"), &CChatUnifierClientSkel::farGuildChat_skel));
			// if this assert, you have a doubly message name in your interface definition !
			nlassert(res.second);
			
			res = handlers.insert(std::make_pair(std::string("CURFGC2"), &CChatUnifierClientSkel::farGuildChat2_skel));
			// if this assert, you have a doubly message name in your interface definition !
			nlassert(res.second);
			
			res = handlers.insert(std::make_pair(std::string("CURFGC2E"), &CChatUnifierClientSkel::farGuildChat2Ex_skel));
			// if this assert, you have a doubly message name in your interface definition !
			nlassert(res.second);
			
			res = handlers.insert(std::make_pair(std::string("CURUB"), &CChatUnifierClientSkel::universeBroadcast_skel));
			// if this assert, you have a doubly message name in your interface definition !
			nlassert(res.second);
			
			res = handlers.insert(std::make_pair(std::string("CUDCB"), &CChatUnifierClientSkel::dynChanBroadcast_skel));
			// if this assert, you have a doubly message name in your interface definition !
			nlassert(res.second);
			
			res = handlers.insert(std::make_pair(std::string("CURBM"), &CChatUnifierClientSkel::recvBroadcastMessage_skel));
			// if this assert, you have a doubly message name in your interface definition !
			nlassert(res.second);
			
			init = true;
		}

		return handlers;			
	}
	bool CChatUnifierClientSkel::fwdOnProcessModuleMessage(NLNET::IModuleProxy *sender, const NLNET::CMessage &message)
	{
		const TMessageHandlerMap &mh = getMessageHandlers();

		TMessageHandlerMap::const_iterator it(mh.find(message.getName()));

		if (it == mh.end())
		{
			return false;
		}

		TMessageHandler cmd = it->second;
		(this->*cmd)(sender, message);

		return true;
	}

	
	void CChatUnifierClientSkel::recvFarTellFail_skel(NLNET::IModuleProxy *sender, const NLNET::CMessage &__message)
	{
		H_AUTO(CChatUnifierClientSkel_recvFarTellFail_CURFTF);
		NLMISC::CEntityId	senderCharId;
			nlRead(__message, serial, senderCharId);
		std::string	destName;
			nlRead(__message, serial, destName);
		TFailInfo	failInfo;
			nlRead(__message, serial, failInfo);
		recvFarTellFail(sender, senderCharId, destName, failInfo);
	}

	void CChatUnifierClientSkel::recvFarTell_skel(NLNET::IModuleProxy *sender, const NLNET::CMessage &__message)
	{
		H_AUTO(CChatUnifierClientSkel_recvFarTell_CURFT);
		NLMISC::CEntityId	senderCharId;
			nlRead(__message, serial, senderCharId);
		std::string	senderName;
			nlRead(__message, serial, senderName);
		bool	havePrivilege;
			nlRead(__message, serial, havePrivilege);
		std::string	destName;
			nlRead(__message, serial, destName);
		std::string	text;
			nlRead(__message, serial, text);
		recvFarTell(sender, senderCharId, senderName, havePrivilege, destName, text);
	}

	void CChatUnifierClientSkel::farGuildChat_skel(NLNET::IModuleProxy *sender, const NLNET::CMessage &__message)
	{
		H_AUTO(CChatUnifierClientSkel_farGuildChat_CURFGC);
		std::string	senderName;
			nlRead(__message, serial, senderName);
		uint32	guildId;
			nlRead(__message, serial, guildId);
		std::string	text;
			nlRead(__message, serial, text);
		farGuildChat(sender, senderName, guildId, text);
	}

	void CChatUnifierClientSkel::farGuildChat2_skel(NLNET::IModuleProxy *sender, const NLNET::CMessage &__message)
	{
		H_AUTO(CChatUnifierClientSkel_farGuildChat2_CURFGC2);
		std::string	senderName;
			nlRead(__message, serial, senderName);
		uint32	guildId;
			nlRead(__message, serial, guildId);
		std::string	phraseName;
			nlRead(__message, serial, phraseName);
		farGuildChat2(sender, senderName, guildId, phraseName);
	}

	void CChatUnifierClientSkel::farGuildChat2Ex_skel(NLNET::IModuleProxy *sender, const NLNET::CMessage &__message)
	{
		H_AUTO(CChatUnifierClientSkel_farGuildChat2Ex_CURFGC2E);
		std::string	senderName;
			nlRead(__message, serial, senderName);
		uint32	guildId;
			nlRead(__message, serial, guildId);
		uint32	phraseId;
			nlRead(__message, serial, phraseId);
		farGuildChat2Ex(sender, senderName, guildId, phraseId);
	}

	void CChatUnifierClientSkel::universeBroadcast_skel(NLNET::IModuleProxy *sender, const NLNET::CMessage &__message)
	{
		H_AUTO(CChatUnifierClientSkel_universeBroadcast_CURUB);
		std::string	senderName;
			nlRead(__message, serial, senderName);
		uint32	senderHomeSession;
			nlRead(__message, serial, senderHomeSession);
		std::string	text;
			nlRead(__message, serial, text);
		universeBroadcast(sender, senderName, senderHomeSession, text);
	}

	void CChatUnifierClientSkel::dynChanBroadcast_skel(NLNET::IModuleProxy *sender, const NLNET::CMessage &__message)
	{
		H_AUTO(CChatUnifierClientSkel_dynChanBroadcast_CUDCB);
		NLMISC::CEntityId	chanId;
			nlRead(__message, serial, chanId);
		std::string	senderName;
			nlRead(__message, serial, senderName);
		std::string	text;
			nlRead(__message, serial, text);
		dynChanBroadcast(sender, chanId, senderName, text);
	}

	void CChatUnifierClientSkel::recvBroadcastMessage_skel(NLNET::IModuleProxy *sender, const NLNET::CMessage &__message)
	{
		H_AUTO(CChatUnifierClientSkel_recvBroadcastMessage_CURBM);
		std::string	message;
			nlRead(__message, serial, message);
		recvBroadcastMessage(sender, message);
	}
		// SU send a far tell failure to IOS. This mean that the player is offline or unknow
	void CChatUnifierClientProxy::recvFarTellFail(NLNET::IModule *sender, const NLMISC::CEntityId &senderCharId, const std::string &destName, TFailInfo failInfo)
	{
		if (_LocalModuleSkel && _LocalModule->isImmediateDispatchingSupported())
		{
			// immediate local synchronous dispatching
			_LocalModuleSkel->recvFarTellFail(_ModuleProxy->getModuleGateway()->getPluggedModuleProxy(sender), senderCharId, destName, failInfo);
		}
		else
		{
			// send the message for remote dispatching and execution or local queing 
			NLNET::CMessage __message;
			
			buildMessageFor_recvFarTellFail(__message, senderCharId, destName, failInfo);

			_ModuleProxy->sendModuleMessage(sender, __message);
		}
	}
		// SU send a far tell to the IOS hosting the addresse character
	void CChatUnifierClientProxy::recvFarTell(NLNET::IModule *sender, const NLMISC::CEntityId &senderCharId, const std::string &senderName, bool havePrivilege, const std::string &destName, const std::string &text)
	{
		if (_LocalModuleSkel && _LocalModule->isImmediateDispatchingSupported())
		{
			// immediate local synchronous dispatching
			_LocalModuleSkel->recvFarTell(_ModuleProxy->getModuleGateway()->getPluggedModuleProxy(sender), senderCharId, senderName, havePrivilege, destName, text);
		}
		else
		{
			// send the message for remote dispatching and execution or local queing 
			NLNET::CMessage __message;
			
			buildMessageFor_recvFarTell(__message, senderCharId, senderName, havePrivilege, destName, text);

			_ModuleProxy->sendModuleMessage(sender, __message);
		}
	}
		// IOS forward a guild chat message to the IOS
	void CChatUnifierClientProxy::farGuildChat(NLNET::IModule *sender, const std::string &senderName, uint32 guildId, const std::string &text)
	{
		if (_LocalModuleSkel && _LocalModule->isImmediateDispatchingSupported())
		{
			// immediate local synchronous dispatching
			_LocalModuleSkel->farGuildChat(_ModuleProxy->getModuleGateway()->getPluggedModuleProxy(sender), senderName, guildId, text);
		}
		else
		{
			// send the message for remote dispatching and execution or local queing 
			NLNET::CMessage __message;
			
			buildMessageFor_farGuildChat(__message, senderName, guildId, text);

			_ModuleProxy->sendModuleMessage(sender, __message);
		}
	}
		// IOS forward a guild chat message to the IOS
	void CChatUnifierClientProxy::farGuildChat2(NLNET::IModule *sender, const std::string &senderName, uint32 guildId, const std::string &phraseName)
	{
		if (_LocalModuleSkel && _LocalModule->isImmediateDispatchingSupported())
		{
			// immediate local synchronous dispatching
			_LocalModuleSkel->farGuildChat2(_ModuleProxy->getModuleGateway()->getPluggedModuleProxy(sender), senderName, guildId, phraseName);
		}
		else
		{
			// send the message for remote dispatching and execution or local queing 
			NLNET::CMessage __message;
			
			buildMessageFor_farGuildChat2(__message, senderName, guildId, phraseName);

			_ModuleProxy->sendModuleMessage(sender, __message);
		}
	}
		// IOS forward a guild chat message to the IOS
	void CChatUnifierClientProxy::farGuildChat2Ex(NLNET::IModule *sender, const std::string &senderName, uint32 guildId, uint32 phraseId)
	{
		if (_LocalModuleSkel && _LocalModule->isImmediateDispatchingSupported())
		{
			// immediate local synchronous dispatching
			_LocalModuleSkel->farGuildChat2Ex(_ModuleProxy->getModuleGateway()->getPluggedModuleProxy(sender), senderName, guildId, phraseId);
		}
		else
		{
			// send the message for remote dispatching and execution or local queing 
			NLNET::CMessage __message;
			
			buildMessageFor_farGuildChat2Ex(__message, senderName, guildId, phraseId);

			_ModuleProxy->sendModuleMessage(sender, __message);
		}
	}
		// IOS forward a univers chat message to the IOSs
	void CChatUnifierClientProxy::universeBroadcast(NLNET::IModule *sender, const std::string &senderName, uint32 senderHomeSession, const std::string &text)
	{
		if (_LocalModuleSkel && _LocalModule->isImmediateDispatchingSupported())
		{
			// immediate local synchronous dispatching
			_LocalModuleSkel->universeBroadcast(_ModuleProxy->getModuleGateway()->getPluggedModuleProxy(sender), senderName, senderHomeSession, text);
		}
		else
		{
			// send the message for remote dispatching and execution or local queing 
			NLNET::CMessage __message;
			
			buildMessageFor_universeBroadcast(__message, senderName, senderHomeSession, text);

			_ModuleProxy->sendModuleMessage(sender, __message);
		}
	}
		// IOS forward a dyn chat chat message to the IOSs
	void CChatUnifierClientProxy::dynChanBroadcast(NLNET::IModule *sender, const NLMISC::CEntityId &chanId, const std::string &senderName, const std::string &text)
	{
		if (_LocalModuleSkel && _LocalModule->isImmediateDispatchingSupported())
		{
			// immediate local synchronous dispatching
			_LocalModuleSkel->dynChanBroadcast(_ModuleProxy->getModuleGateway()->getPluggedModuleProxy(sender), chanId, senderName, text);
		}
		else
		{
			// send the message for remote dispatching and execution or local queing 
			NLNET::CMessage __message;
			
			buildMessageFor_dynChanBroadcast(__message, chanId, senderName, text);

			_ModuleProxy->sendModuleMessage(sender, __message);
		}
	}
		// SU send a broadcast message to the IOS
	void CChatUnifierClientProxy::recvBroadcastMessage(NLNET::IModule *sender, const std::string &message)
	{
		if (_LocalModuleSkel && _LocalModule->isImmediateDispatchingSupported())
		{
			// immediate local synchronous dispatching
			_LocalModuleSkel->recvBroadcastMessage(_ModuleProxy->getModuleGateway()->getPluggedModuleProxy(sender), message);
		}
		else
		{
			// send the message for remote dispatching and execution or local queing 
			NLNET::CMessage __message;
			
			buildMessageFor_recvBroadcastMessage(__message, message);

			_ModuleProxy->sendModuleMessage(sender, __message);
		}
	}

	// Message serializer. Return the message received in reference for easier integration
	const NLNET::CMessage &CChatUnifierClientProxy::buildMessageFor_recvFarTellFail(NLNET::CMessage &__message, const NLMISC::CEntityId &senderCharId, const std::string &destName, TFailInfo failInfo)
	{
		__message.setType("CURFTF");
			nlWrite(__message, serial, const_cast < NLMISC::CEntityId& > (senderCharId));
			nlWrite(__message, serial, const_cast < std::string& > (destName));
			nlWrite(__message, serial, failInfo);


		return __message;
	}

	// Message serializer. Return the message received in reference for easier integration
	const NLNET::CMessage &CChatUnifierClientProxy::buildMessageFor_recvFarTell(NLNET::CMessage &__message, const NLMISC::CEntityId &senderCharId, const std::string &senderName, bool havePrivilege, const std::string &destName, const std::string &text)
	{
		__message.setType("CURFT");
			nlWrite(__message, serial, const_cast < NLMISC::CEntityId& > (senderCharId));
			nlWrite(__message, serial, const_cast < std::string& > (senderName));
			nlWrite(__message, serial, havePrivilege);
			nlWrite(__message, serial, const_cast < std::string& > (destName));
			nlWrite(__message, serial, const_cast < std::string& > (text));


		return __message;
	}

	// Message serializer. Return the message received in reference for easier integration
	const NLNET::CMessage &CChatUnifierClientProxy::buildMessageFor_farGuildChat(NLNET::CMessage &__message, const std::string &senderName, uint32 guildId, const std::string &text)
	{
		__message.setType("CURFGC");
			nlWrite(__message, serial, const_cast < std::string& > (senderName));
			nlWrite(__message, serial, guildId);
			nlWrite(__message, serial, const_cast < std::string& > (text));


		return __message;
	}

	// Message serializer. Return the message received in reference for easier integration
	const NLNET::CMessage &CChatUnifierClientProxy::buildMessageFor_farGuildChat2(NLNET::CMessage &__message, const std::string &senderName, uint32 guildId, const std::string &phraseName)
	{
		__message.setType("CURFGC2");
			nlWrite(__message, serial, const_cast < std::string& > (senderName));
			nlWrite(__message, serial, guildId);
			nlWrite(__message, serial, const_cast < std::string& > (phraseName));


		return __message;
	}

	// Message serializer. Return the message received in reference for easier integration
	const NLNET::CMessage &CChatUnifierClientProxy::buildMessageFor_farGuildChat2Ex(NLNET::CMessage &__message, const std::string &senderName, uint32 guildId, uint32 phraseId)
	{
		__message.setType("CURFGC2E");
			nlWrite(__message, serial, const_cast < std::string& > (senderName));
			nlWrite(__message, serial, guildId);
			nlWrite(__message, serial, phraseId);


		return __message;
	}

	// Message serializer. Return the message received in reference for easier integration
	const NLNET::CMessage &CChatUnifierClientProxy::buildMessageFor_universeBroadcast(NLNET::CMessage &__message, const std::string &senderName, uint32 senderHomeSession, const std::string &text)
	{
		__message.setType("CURUB");
			nlWrite(__message, serial, const_cast < std::string& > (senderName));
			nlWrite(__message, serial, senderHomeSession);
			nlWrite(__message, serial, const_cast < std::string& > (text));


		return __message;
	}

	// Message serializer. Return the message received in reference for easier integration
	const NLNET::CMessage &CChatUnifierClientProxy::buildMessageFor_dynChanBroadcast(NLNET::CMessage &__message, const NLMISC::CEntityId &chanId, const std::string &senderName, const std::string &text)
	{
		__message.setType("CUDCB");
			nlWrite(__message, serial, const_cast < NLMISC::CEntityId& > (chanId));
			nlWrite(__message, serial, const_cast < std::string& > (senderName));
			nlWrite(__message, serial, const_cast < std::string& > (text));


		return __message;
	}

	// Message serializer. Return the message received in reference for easier integration
	const NLNET::CMessage &CChatUnifierClientProxy::buildMessageFor_recvBroadcastMessage(NLNET::CMessage &__message, const std::string &message)
	{
		__message.setType("CURBM");
			nlWrite(__message, serial, const_cast < std::string& > (message));


		return __message;
	}

}
