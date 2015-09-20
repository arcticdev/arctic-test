/*
 * Arctic MMORPG Server Software
 * Copyright (c) 2008-2014 Arctic Server Team
 * See COPYING for license details.
 */

#include "StdAfx.h"

void WorldSession::HandleRepopRequestOpcode( WorldPacket & recv_data )
{
	DEBUG_LOG( "WORLD"," Recvd CMSG_REPOP_REQUEST Message" );
	if(_player->m_CurrentTransporter)
		_player->m_CurrentTransporter->RemovePlayer(_player);

	if(_player->m_CurrentVehicle)
		_player->m_CurrentVehicle->RemovePassenger(_player);

	GetPlayer()->RepopRequestedPlayer();
}

void WorldSession::HandleAutostoreLootItemOpcode( WorldPacket & recv_data )
{
	if(!_player->IsInWorld() || !_player->GetLootGUID()) return;
//	uint8 slot = 0;
	uint32 itemid = 0;
	uint32 amt = 1;
	uint8 lootSlot = 0;
	uint8 error = 0;
	SlotResult slotresult;

	Item* add;

	if(_player->isCasting())
		_player->InterruptCurrentSpell();

	GameObject* pGO = NULL;
	Object* pLootObj;

	// handle item loot
	uint64 guid = _player->GetLootGUID();
	if( GET_TYPE_FROM_GUID(guid) == HIGHGUID_TYPE_ITEM )
		pLootObj = _player->GetItemInterface()->GetItemByGUID(guid);
	else
		pLootObj = _player->GetMapMgr()->_GetObject(guid);

	if( pLootObj == NULL )
		return;

	if( pLootObj->GetTypeId() == TYPEID_GAMEOBJECT )
		pGO = TO_GAMEOBJECT(pLootObj);

	recv_data >> lootSlot;
	if (lootSlot >= pLootObj->m_loot.items.size())
		return;

	amt = pLootObj->m_loot.items.at(lootSlot).iItemsCount;
	if( pLootObj->m_loot.items.at(lootSlot).roll != NULL )
		return;

	if (!pLootObj->m_loot.items.at(lootSlot).ffa_loot)
	{
		if (!amt)//Test for party loot
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL,INV_ERR_ALREADY_LOOTED);
			return;
		}
	}
	else
	{
		//make sure this player can still loot it in case of ffa_loot
		LooterSet::iterator itr = pLootObj->m_loot.items.at(lootSlot).has_looted.find(_player->GetLowGUID());

		if (pLootObj->m_loot.items.at(lootSlot).has_looted.end() != itr)
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL,INV_ERR_ALREADY_LOOTED);
			return;
		}
	}

	itemid = pLootObj->m_loot.items.at(lootSlot).item.itemproto->ItemId;
	ItemPrototype* it = pLootObj->m_loot.items.at(lootSlot).item.itemproto;

	if((error = _player->GetItemInterface()->CanReceiveItem(it, 1, NULL)))
	{
		_player->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, error);
		return;
	}

	add = GetPlayer()->GetItemInterface()->FindItemLessMax(itemid, amt, false);

	if (!add)
	{
		slotresult = GetPlayer()->GetItemInterface()->FindFreeInventorySlot(it);
		if(!slotresult.Result)
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_INVENTORY_FULL);
			return;
		}

		DEBUG_LOG("HandleAutostoreItem","AutoLootItem %u",itemid);
		Item* item = objmgr.CreateItem( itemid, GetPlayer());

		item->SetUInt32Value(ITEM_FIELD_STACK_COUNT,amt);
		if(pLootObj->m_loot.items.at(lootSlot).iRandomProperty!=NULL)
		{
			item->SetRandomProperty(pLootObj->m_loot.items.at(lootSlot).iRandomProperty->ID);
			item->ApplyRandomProperties(false);
		}
		else if(pLootObj->m_loot.items.at(lootSlot).iRandomSuffix != NULL)
		{
			item->SetRandomSuffix(pLootObj->m_loot.items.at(lootSlot).iRandomSuffix->id);
			item->ApplyRandomProperties(false);
		}

		if( GetPlayer()->GetItemInterface()->SafeAddItem(item,slotresult.ContainerSlot, slotresult.Slot) )
		{
			sQuestMgr.OnPlayerItemPickup(GetPlayer(),item);
			_player->GetSession()->SendItemPushResult(item,false,true,true,true,slotresult.ContainerSlot,slotresult.Slot,1);
		}
		else
		{
			item->Destructor();
		}
	}
	else
	{
		add->SetCount(add->GetUInt32Value(ITEM_FIELD_STACK_COUNT) + amt);
		add->m_isDirty = true;

		sQuestMgr.OnPlayerItemPickup(GetPlayer(), add);
		_player->GetSession()->SendItemPushResult(add, false, true, true, false, _player->GetItemInterface()->GetBagSlotByGuid(add->GetGUID()), 0xFFFFFFFF, amt);
	}

	// in case of ffa_loot update only the player who recives it.
	if (!pLootObj->m_loot.items.at(lootSlot).ffa_loot)
	{
		pLootObj->m_loot.items.at(lootSlot).iItemsCount = 0;

		// this gets sent to all looters
		WorldPacket data(1);
		data.SetOpcode(SMSG_LOOT_REMOVED);
		data << lootSlot;
		Player* plr;
		for(LooterSet::iterator itr = pLootObj->m_loot.looters.begin(); itr != pLootObj->m_loot.looters.end(); itr++)
		{
			plr = _player->GetMapMgr()->GetPlayer((*itr));
			if( plr != NULL )
				plr->GetSession()->SendPacket(&data);
		}
	}
	else
	{
		pLootObj->m_loot.items.at(lootSlot).has_looted.insert(_player->GetLowGUID());
		WorldPacket data(1);
		data.SetOpcode(SMSG_LOOT_REMOVED);
		data << lootSlot;
		_player->GetSession()->SendPacket(&data);
	}

	/* any left yet? (for fishing bobbers) */
	if(pGO && pGO->GetEntry() ==GO_FISHING_BOBBER)
	{
		for(vector<__LootItem>::iterator itr = pLootObj->m_loot.items.begin(); itr != pLootObj->m_loot.items.end(); itr++)
		{
			if( itr->iItemsCount > 0 )
				return;
		}

		pGO->ExpireAndDelete();
	}
}

void WorldSession::HandleLootMoneyOpcode( WorldPacket & recv_data )
{
	// sanity checks
	CHECK_INWORLD_RETURN;

	// lookup the object we will be looting
	// TODO: Handle item guids
	Object* pLootObj = _player->GetMapMgr()->_GetObject(_player->GetLootGUID());
	Player* plr;
	if( pLootObj == NULL )
		return;

	// is there any left? :o
	if( pLootObj->m_loot.gold == 0 )
		return;

	uint32 money = pLootObj->m_loot.gold;
	for(LooterSet::iterator itr = pLootObj->m_loot.looters.begin(); itr != pLootObj->m_loot.looters.end(); itr++)
	{
		if((plr = _player->GetMapMgr()->GetPlayer(*itr)))
			plr->GetSession()->OutPacket(SMSG_LOOT_CLEAR_MONEY);
	}

	if(!_player->InGroup())
	{
		if((_player->GetUInt32Value(PLAYER_FIELD_COINAGE) + money) >= PLAYER_MAX_GOLD)
			return;

		_player->ModUnsigned32Value( PLAYER_FIELD_COINAGE , money);
		pLootObj->m_loot.gold = 0;
	}
	else
	{
		Group* party = _player->GetGroup();
		pLootObj->m_loot.gold = 0;

		vector<Player*  > targets;
		targets.reserve(party->MemberCount());

		SubGroup * sgrp;
		GroupMembersSet::iterator itr;
		party->getLock().Acquire();
		for(uint32 i = 0; i < party->GetSubGroupCount(); i++)
		{
			sgrp = party->GetSubGroup(i);
			for(itr = sgrp->GetGroupMembersBegin(); itr != sgrp->GetGroupMembersEnd(); itr++)
			{
				if((*itr)->m_loggedInPlayer && (*itr)->m_loggedInPlayer->GetZoneId() == _player->GetZoneId() && _player->GetInstanceID() == (*itr)->m_loggedInPlayer->GetInstanceID())
					targets.push_back((*itr)->m_loggedInPlayer);
			}
		}
		party->getLock().Release();

		if(!targets.size())
			return;

		uint32 share = money / uint32(targets.size());

		uint8 databuf[50];
		StackPacket pkt(SMSG_LOOT_MONEY_NOTIFY, databuf, 50);
		pkt << share;

		for(vector<Player*  >::iterator itr = targets.begin(); itr != targets.end(); itr++)
		{
			if(((*itr)->GetUInt32Value(PLAYER_FIELD_COINAGE) + share) >= PLAYER_MAX_GOLD)
				continue;

			(*itr)->ModUnsigned32Value(PLAYER_FIELD_COINAGE, share);
			(*itr)->GetSession()->SendPacket(&pkt);
		}
	}
}

void WorldSession::HandleLootOpcode( WorldPacket & recv_data )
{
	CHECK_INWORLD_RETURN;

	uint64 guid;
	recv_data >> guid;

	if(_player->isCasting())
		_player->InterruptCurrentSpell();

	if(_player->InGroup() && !_player->m_bg)
	{
		Group * party = _player->GetGroup();
		if(party)
		{
			if(party->GetMethod() == PARTY_LOOT_MASTER)
			{
				uint8 databuf[1000];
				StackPacket data(SMSG_LOOT_MASTER_LIST, databuf, 1000);

				data << (uint8)party->MemberCount();
				uint32 real_count = 0;
				SubGroup *s;
				GroupMembersSet::iterator itr;
				party->Lock();
				for(uint32 i = 0; i < party->GetSubGroupCount(); ++i)
				{
					s = party->GetSubGroup(i);
					for(itr = s->GetGroupMembersBegin(); itr != s->GetGroupMembersEnd(); ++itr)
					{
						if((*itr)->m_loggedInPlayer && _player->GetZoneId() == (*itr)->m_loggedInPlayer->GetZoneId())
						{
							data << (*itr)->m_loggedInPlayer->GetGUID();
							++real_count;
						}
					}
				}
				party->Unlock();
				data.GetBufferPointer()[0] = real_count;

				party->SendPacketToAll(&data);
			}
		}
	}

	_player->SendLoot(guid, LOOT_CORPSE);
}


void WorldSession::HandleLootReleaseOpcode( WorldPacket & recv_data )
{
	CHECK_INWORLD_RETURN;
	uint64 guid;

	recv_data >> guid;

	WorldPacket data(SMSG_LOOT_RELEASE_RESPONSE, 9);
	data << guid << uint8( 1 );
	SendPacket( &data );

	_player->SetLootGUID(0);
	_player->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOOTING);
	_player->m_currentLoot = 0;

	// special case
	if( GET_TYPE_FROM_GUID( guid ) == HIGHGUID_TYPE_GAMEOBJECT )
	{
		GameObject* pGO = _player->GetMapMgr()->GetGameObject( GET_LOWGUID_PART(guid) );
		if( pGO == NULL )
			return;

		pGO->m_loot.looters.erase(_player->GetLowGUID());
        switch( pGO->GetByte(GAMEOBJECT_BYTES_1, 1) )
		{
		case GAMEOBJECT_TYPE_FISHINGNODE:
			{
				if(pGO->IsInWorld())
				{
					pGO->RemoveFromWorld(true);
				}
				pGO->Destructor();
			}break;
		case GAMEOBJECT_TYPE_CHEST:
			{
                pGO->m_loot.looters.erase( _player->GetLowGUID() );
                //check for locktypes

                Lock* pLock = dbcLock.LookupEntry( pGO->GetInfo()->SpellFocus );
                if( pLock )
                {
                    for( uint32 i = 0; i < 5; ++i )
                    {
                        if( pLock->locktype[i] )
                        {
                            if( pLock->locktype[i] == 2 ) //locktype;
                            {
                                //herbalism and mining;
                                if( pLock->lockmisc[i] == LOCKTYPE_MINING || pLock->lockmisc[i] == LOCKTYPE_HERBALISM )
                                {
                                    //we still have loot inside.
                                    if( pGO->m_loot.HasItems(_player) )
                                    {
                                        pGO->SetByte(GAMEOBJECT_BYTES_1, 0, 1 );
										// TODO : redo this temporary fix, because for some reason hasloot is true even when we loot everything
										// my guess is we need to set up some even that rechecks the GO in 10 seconds or something
										//pGO->Despawn( 600000 + ( RandomUInt( 300000 ) ) );
                                        return;
                                    }

                                    if( pGO->CanMine() )
                                    {
										pGO->ClearLoot();
                                        pGO->UseMine();
										return;
                                    }
                                    else
									{
    									pGO->CalcMineRemaining( true );
										pGO->Despawn( 600000 + ( RandomUInt( 180000 ) ) );
										return;
									}
                                }
                                else //other type of locks that i dont bother to split atm ;P
                                {
                                    if(pGO->m_loot.HasItems(_player))
                                    {
                                        pGO->SetByte(GAMEOBJECT_BYTES_1,GAMEOBJECT_BYTES_STATE, 1);
                                        return;
                                    }

									pGO->CalcMineRemaining(true);

									//Don't interfere with scripted GO's
									if(!sEventMgr.HasEvent(pGO, EVENT_GMSCRIPT_EVENT))
									{
										uint32 DespawnTime = 0;
										if(sQuestMgr.GetGameObjectLootQuest(pGO->GetEntry()))
											DespawnTime = 120000;	   // 5 min for quest GO,
										else
											DespawnTime = 900000;	   // 15 for else

										pGO->Despawn(DespawnTime);
									}
									return;
                                }
                            }
                        }
                    }
                }
                else
                {
                    if( pGO->m_loot.HasItems(_player) )
                    {
                        pGO->SetByte(GAMEOBJECT_BYTES_1,GAMEOBJECT_BYTES_STATE, 1);
                        return;
                    }
                    uint32 DespawnTime = 0;
			        if(sQuestMgr.GetGameObjectLootQuest(pGO->GetEntry()))
				        DespawnTime = 120000;	   // 5 min for quest GO,
			        else
			        {
				        DespawnTime = 900000;	   // 15 for else
			        }


					pGO->Despawn(DespawnTime);

                }
            }
        }
		return;
	}

	else if( GET_TYPE_FROM_GUID(guid) == HIGHGUID_TYPE_ITEM )
	{
		// if we have no items left, destroy the item.
		Item* pItem = _player->GetItemInterface()->GetItemByGUID(guid);
		if( pItem != NULL )
		{
			if( !pItem->m_loot.HasItems(_player) )
				_player->GetItemInterface()->SafeFullRemoveItemByGuid(guid);
		}

		return;
	}

	else if( GET_TYPE_FROM_GUID(guid) == HIGHGUID_TYPE_UNIT )
	{
		Unit* pLootTarget = _player->GetMapMgr()->GetUnit(guid);
		if( pLootTarget != NULL )
		{
			pLootTarget->m_loot.looters.erase(_player->GetLowGUID());
			if( !pLootTarget->m_loot.HasLoot(_player) )
			{
				TO_CREATURE(pLootTarget)->UpdateLootAnimation(_player);

				// skinning
				if( lootmgr.IsSkinnable( pLootTarget->GetEntry() ) )
				{
					pLootTarget->BuildFieldUpdatePacket( _player, UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE );
				}
			}
		}
	}

	else if( GET_TYPE_FROM_GUID(guid) == HIGHGUID_TYPE_CORPSE )
	{
		Corpse* pCorpse = objmgr.GetCorpse((uint32)guid);
		if( pCorpse != NULL && !pCorpse->m_loot.HasLoot(_player) )
			pCorpse->Despawn();
	}
}

void WorldSession::HandleWhoOpcode( WorldPacket & recv_data )
{
	uint32 min_level;
	uint32 max_level;
	uint32 class_mask;
	uint32 race_mask;
	uint32 zone_count;
	uint32 * zones = 0;
	uint32 name_count;
	string * names = 0;
	string chatname;
	string unkstr;
	bool cname;
	uint32 i;

	if( ((uint32)UNIXTIME - m_lastWhoTime) < 10 && !GetPlayer()->bGMTagOn )
		return;

	m_lastWhoTime = (uint32)UNIXTIME;
	recv_data >> min_level >> max_level;
	recv_data >> chatname >> unkstr >> race_mask >> class_mask;
	recv_data >> zone_count;

	if(zone_count > 0 && zone_count < 10)
	{
		zones = new uint32[zone_count];

		for(i = 0; i < zone_count; ++i)
			recv_data >> zones[i];
	}
	else
	{
		zone_count = 0;
	}

	recv_data >> name_count;
	if(name_count > 0 && name_count < 10)
	{
		names = new string[name_count];

		for(i = 0; i < name_count; ++i)
			recv_data >> names[i];
	}
	else
	{
		name_count = 0;
	}

	if(chatname.length() > 0)
		cname = true;
	else
		cname = false;

	DEBUG_LOG( "WORLD"," Recvd CMSG_WHO Message with %u zones and %u names", zone_count, name_count );

	bool gm = false;
	uint32 team = _player->GetTeam();
	if(HasGMPermissions())
		gm = true;

	uint32 sent_count = 0;
	uint32 total_count = 0;

#ifdef CLUSTERING
	HM_NAMESPACE::hash_map<uint32,RPlayerInfo*>::const_iterator itr,iend;
	RPlayerInfo* plr;
#else
	PlayerStorageMap::const_iterator itr,iend;
	Player* plr;
#endif

	uint32 lvl;
	bool add;
	WorldPacket data;
	data.SetOpcode(SMSG_WHO);
	data << uint64(0);

#ifdef CLUSTERING
	sClusterInterface.m_onlinePlayerMapMutex.Acquire();
	iend = sClusterInterface._onlinePlayers.end();
	itr = sClusterInterface._onlinePlayers.begin();
	DEBUG_LOG( "WORLD","Recvd CMSG_WHO Message, there are currently %u _onlinePlayers", sClusterInterface._onlinePlayers.size());
	while(itr !=iend && sent_count < 50)
	{
		plr = itr->second;
		itr++;
		bool queriedPlayerIsGM = false;
		if(plr->GMPermissions.find("a"))
		{
			queriedPlayerIsGM = true;
		}

		if(!sWorld.show_gm_in_who_list && !gm && queriedPlayerIsGM)
		{
			continue;
		}

		if(!gm && plr->Team != team && !queriedPlayerIsGM)
		{
			continue;
		}

		++total_count;

		// Add by default, if we don't have any checks
		add = true;

		// Chat name
		if(cname && chatname != plr->Name)
			continue;

		// Level check
		lvl = plr->Level;
		if(min_level && max_level)
		{
			// skip players outside of level range
			if(lvl < min_level || lvl > max_level)
			{
				continue;
			}
		}

		// Zone id compare
		if(zone_count)
		{
			// people that fail the zone check don't get added
			add = false;
			for(i = 0; i < zone_count; ++i)
			{
				if(zones[i] == plr->ZoneId)
				{
					add = true;
					break;
				}
			}
		}

		if(!(class_mask & plr->getClassMask()) || !(race_mask & plr->getRaceMask()))
		{
			add = false;
		}

		// skip players that fail zone check
		if(!add)
			continue;

		// name check
		if(name_count)
		{
			// people that fail name check don't get added
			add = false;
			for(i = 0; i < name_count; ++i)
			{
				if(!strnicmp(names[i].c_str(), plr->Name.c_str(), names[i].length()))
				{
					add = true;
					break;
				}
			}
		}

		if(!add)
			continue;

		// if we're here, it means we've passed all testing
		// so add the names :)
		data << plr->Name; //needs to be .c_str() ??
		if(plr->GuildId)
		{
			Guild * guild = objmgr.GetGuild(plr->GuildId);
			if(guild)
				data << objmgr.GetGuild(plr->GuildId)->GetGuildName();
			else
				data << uint8(0);	   // Guild name
		}
		else
			data << uint8(0);	   // Guild name

		data << plr->Level;
		data << uint32(plr->Class);
		data << uint32(plr->Race);
		data << plr->Gender;
		data << uint32(plr->ZoneId);
		++sent_count;
	}
	sClusterInterface.m_onlinePlayerMapMutex.Release();
	data.wpos(0);
	data << sent_count;
	data << sent_count;

#else

	objmgr._playerslock.AcquireReadLock();
	iend=objmgr._players.end();
	itr=objmgr._players.begin();
	while(itr !=iend && sent_count < 50)
	{
		plr = itr->second;
		itr++;

		if(!plr->GetSession() || !plr->IsInWorld())
			continue;

		if(!sWorld.show_gm_in_who_list && !HasGMPermissions())
		{
			if(plr->GetSession()->HasGMPermissions())
				continue;
		}

		// Team check
		if(!gm && plr->GetTeam() != team && !plr->GetSession()->HasGMPermissions())
			continue;

		++total_count;

		// Add by default, if we don't have any checks
		add = true;

		// Chat name
		if(cname && chatname != *plr->GetNameString())
			continue;

		// Level check
		lvl = plr->m_uint32Values[UNIT_FIELD_LEVEL];
		if(min_level && max_level)
		{
			// skip players outside of level range
			if(lvl < min_level || lvl > max_level)
				continue;
		}

		// Zone id compare
		if(zone_count)
		{
			// people that fail the zone check don't get added
			add = false;
			for(i = 0; i < zone_count; ++i)
			{
				if(zones[i] == plr->GetZoneId())
				{
					add = true;
					break;
				}
			}
		}

		if(!(class_mask & plr->getClassMask()) || !(race_mask & plr->getRaceMask()))
			add = false;

		// skip players that fail zone check
		if(!add)
			continue;

		// name check
		if(name_count)
		{
			// people that fail name check don't get added
			add = false;
			for(i = 0; i < name_count; ++i)
			{
				if(!strnicmp(names[i].c_str(), plr->GetName(), names[i].length()))
				{
					add = true;
					break;
				}
			}
		}

		if(!add)
			continue;

		// if we're here, it means we've passed all testing
		// so add the names :)
		data << plr->GetName();
		if(plr->m_playerInfo->guild)
			data << plr->m_playerInfo->guild->GetGuildName();
		else
			data << uint8(0);	   // Guild name

		data << plr->m_uint32Values[UNIT_FIELD_LEVEL];
		data << uint32(plr->getClass());
		data << uint32(plr->getRace());
		data << plr->getGender();
		data << uint32(plr->GetZoneId());
		++sent_count;
	}
	objmgr._playerslock.ReleaseReadLock();
	data.wpos(0);
	data << sent_count;
	data << sent_count;

#endif

	SendPacket(&data);

	// free up used memory
	if(zones)
		delete [] zones;
	if(names)
		delete [] names;
}

void WorldSession::HandleLogoutRequestOpcode( WorldPacket & recv_data )
{
	Player* pPlayer = GetPlayer();
	WorldPacket data(SMSG_LOGOUT_RESPONSE, 9);

	DEBUG_LOG( "WORLD"," Recvd CMSG_LOGOUT_REQUEST Message" );

	if(pPlayer)
	{
		sHookInterface.OnLogoutRequest(pPlayer);
		if(pPlayer->m_isResting ||	  // We are resting so log out instantly
			pPlayer->GetTaxiState() ||  // or we are on a taxi
			HasGMPermissions())		   // or we are a gm
		{
			SetLogoutTimer(1);
			return;
		}

		if(pPlayer->DuelingWith != NULL || pPlayer->CombatStatus.IsInCombat())
		{
			//can't quit still dueling or attacking
			data << uint32(0x1); //Filler
			data << uint8(0); //Logout accepted
			SendPacket( &data );
			return;
		}

		data << uint32(0); //Filler
		data << uint8(0); //Logout accepted
		SendPacket( &data );

		//stop player from moving
		pPlayer->SetMovement(MOVE_ROOT,1);

		// Set the "player locked" flag, to prevent movement
		pPlayer->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOCK_PLAYER);

		//make player sit
		pPlayer->SetStandState(STANDSTATE_SIT);
		SetLogoutTimer(PLAYER_LOGOUT_DELAY);
	}
	/*
	> 0 = You can't Logout Now
	*/
}

void WorldSession::HandlePlayerLogoutOpcode( WorldPacket & recv_data )
{
	DEBUG_LOG( "WORLD"," Recvd CMSG_PLAYER_LOGOUT Message" );
	if(!HasGMPermissions())
	{
		// send "You do not have permission to use this"
		SendNotification(NOTIFICATION_MESSAGE_NO_PERMISSION);
	} else {
		LogoutPlayer(true);
	}
}

void WorldSession::HandleLogoutCancelOpcode( WorldPacket & recv_data )
{
	DEBUG_LOG( "WORLD"," Recvd CMSG_LOGOUT_CANCEL Message" );

	Player* pPlayer = GetPlayer();
	if(!pPlayer || !_logoutTime)
		return;

	//Cancel logout Timer
	SetLogoutTimer(0);

	//tell client about cancel
	OutPacket(SMSG_LOGOUT_CANCEL_ACK);

	//unroot player
	pPlayer->SetMovement(MOVE_UNROOT,5);

	// Remove the "player locked" flag, to allow movement
	pPlayer->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOCK_PLAYER);

	//make player stand
	pPlayer->SetStandState(STANDSTATE_STAND);

	DEBUG_LOG( "WORLD"," sent SMSG_LOGOUT_CANCEL_ACK Message" );
}

void WorldSession::HandleZoneUpdateOpcode( WorldPacket & recv_data )
{
	uint32 newZone;
	WPAssert(GetPlayer());

	recv_data >> newZone;

	if (GetPlayer()->GetZoneId() == newZone)
		return;

	sWeatherMgr.SendWeather(GetPlayer());
	_player->ZoneUpdate(newZone);

	//clear buyback
	_player->GetItemInterface()->EmptyBuyBack();
}

void WorldSession::HandleSetTargetOpcode( WorldPacket & recv_data )
{
	// obselete?
}

void WorldSession::HandleSetSelectionOpcode( WorldPacket & recv_data )
{
	uint64 guid;
	recv_data >> guid;

	_player->SetTargetGUID(guid);
	_player->SetSelection(guid);

	if(_player->m_comboPoints)
		_player->UpdateComboPoints();
}

void WorldSession::HandleStandStateChangeOpcode( WorldPacket & recv_data )
{
	uint32 animstate;
	recv_data >> animstate;

	_player->SetStandState(int8(animstate));
}

void WorldSession::HandleBugOpcode( WorldPacket & recv_data )
{
	uint32 suggestion, contentlen;
	std::string content;
	uint32 typelen;
	std::string type;

	recv_data >> suggestion >> contentlen >> content >> typelen >> type;

	if( suggestion == 0 )
		DEBUG_LOG( "WORLD"," Received CMSG_BUG [Bug Report]" );
	else
		DEBUG_LOG( "WORLD"," Received CMSG_BUG [Suggestion]" );

	OUT_DEBUG( type.c_str( ) );
	OUT_DEBUG( content.c_str( ) );
}

void WorldSession::HandleCorpseReclaimOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN
	OUT_DEBUG("WORLD: Received CMSG_RECLAIM_CORPSE");

	uint64 guid;
	recv_data >> guid;

	if(guid == 0)
		return;

	Corpse* pCorpse = objmgr.GetCorpse((uint32)guid);
	if(pCorpse == NULL)
		return;

	// Check that we're reviving from a corpse, and that corpse is associated with us.
	if( pCorpse->GetUInt32Value( CORPSE_FIELD_OWNER ) != _player->GetLowGUID() && pCorpse->GetUInt32Value( CORPSE_FIELD_FLAGS ) == 5 )
	{
		WorldPacket data(SMSG_RESURRECT_FAILED, 4);
		data << uint32(1); // this is a real guess!
		SendPacket(&data);
		return;
	}

	// Check we are actually in range of our corpse
	if(pCorpse->GetDistance2dSq(_player) > CORPSE_MINIMUM_RECLAIM_RADIUS_SQ)
	{
		WorldPacket data(SMSG_RESURRECT_FAILED, 4);
		data << uint32(1);
		SendPacket(&data);
		return;
	}

	// Check death clock before resurrect they must wait for release to complete
	if(time(NULL) < pCorpse->GetDeathClock() + CORPSE_RECLAIM_TIME)
	{
		WorldPacket data(SMSG_RESURRECT_FAILED, 4);
		data << uint32(1);
		SendPacket(&data);
		return;
	}

	GetPlayer()->ResurrectPlayer(NULL);
	GetPlayer()->SetUInt32Value(UNIT_FIELD_HEALTH, GetPlayer()->GetUInt32Value(UNIT_FIELD_MAXHEALTH)/2 );
}

void WorldSession::HandleResurrectResponseOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN;
	OUT_DEBUG("WORLD: Received CMSG_RESURRECT_RESPONSE");

	if(GetPlayer()->isAlive())
		return;

	uint64 guid;
	uint8 status;
	recv_data >> guid;
	recv_data >> status;

	// check
	if( guid != 0 && _player->resurrector != (uint32)guid )
	{
		// error
		return;
	}

	// need to check guid
	Player* pl = _player->GetMapMgr()->GetPlayer((uint32)guid);
	if(!pl)
		pl = objmgr.GetPlayer((uint32)guid);

	// reset resurrector
	_player->resurrector = 0;

	if(pl == 0 || status != 1)
	{
		_player->m_resurrectHealth = 0;
		_player->m_resurrectMana = 0;
		_player->resurrector = 0;
		return;
	}

	_player->SetMovement(MOVE_UNROOT, 1);
	_player->ResurrectPlayer(pl);
	_player->m_resurrectHealth = 0;
	_player->m_resurrectMana = 0;

}

void WorldSession::HandleUpdateAccountData(WorldPacket &recv_data)
{
	//OUT_DEBUG("WORLD: Received CMSG_UPDATE_ACCOUNT_DATA");

	uint32 uiID;
	if(!sWorld.m_useAccountData)
		return;

	recv_data >> uiID;

	if(uiID > 8)
	{
		// Shit..
		sLog.outString("WARNING: Accountdata > 8 (%d) was requested to be updated by %s of account %d!", uiID, GetPlayer()->GetName(), this->GetAccountId());
		return;
	}

	uint32 uiDecompressedSize;
	recv_data >> uiDecompressedSize;
	uLongf uid = uiDecompressedSize;

	// client wants to 'erase' current entries
	if(uiDecompressedSize == 0)
	{
		SetAccountData(uiID, NULL, false,0);
		return;
	}

	if(uiDecompressedSize>100000)
	{
		Disconnect();
		return;
	}

	if(uiDecompressedSize >= 65534)
	{
		// BLOB fields can't handle any more than this.
		return;
	}

	size_t ReceivedPackedSize = recv_data.size() - 8;
	char* data = new char[uiDecompressedSize+1];
	memset(data,0,uiDecompressedSize+1);	/* fix umr here */

	if(uiDecompressedSize > ReceivedPackedSize) // if packed is compressed
	{
		int32 ZlibResult;

		ZlibResult = uncompress((uint8*)data, &uid, recv_data.contents() + 8, (uLong)ReceivedPackedSize);

		switch (ZlibResult)
		{
		case Z_OK:				  //0 no error decompression is OK
			SetAccountData(uiID, data, false,uiDecompressedSize);
			OUT_DEBUG("WORLD: Successfully decompressed account data %d for %s, and updated storage array.", uiID, GetPlayer()->GetName());
			break;

		case Z_ERRNO:			   //-1
		case Z_STREAM_ERROR:		//-2
		case Z_DATA_ERROR:		  //-3
		case Z_MEM_ERROR:		   //-4
		case Z_BUF_ERROR:		   //-5
		case Z_VERSION_ERROR:	   //-6
		{
			delete [] data;
			sLog.outString("WORLD WARNING: Decompression of account data %d for %s FAILED.", uiID, GetPlayer()->GetName());
			break;
		}

		default:
			delete [] data;
			sLog.outString("WORLD WARNING: Decompression gave a unknown error: %x, of account data %d for %s FAILED.", ZlibResult, uiID, GetPlayer()->GetName());
			break;
		}
	}
	else
	{
		memcpy(data,recv_data.contents() + 8,uiDecompressedSize);
		SetAccountData(uiID, data, false,uiDecompressedSize);
	}
}

void WorldSession::HandleRequestAccountData(WorldPacket& recv_data)
{
	//OUT_DEBUG("WORLD: Received CMSG_REQUEST_ACCOUNT_DATA");

	uint32 id;
	if(!sWorld.m_useAccountData)
		return;
	recv_data >> id;

	if(id > 8)
	{
		// Shit..
		sLog.outString("WARNING: Accountdata > 8 (%d) was requested by %s of account %d!", id, GetPlayer()->GetName(), this->GetAccountId());
		return;
	}

	AccountDataEntry* res = GetAccountData(id);
		WorldPacket data ;
		data.SetOpcode(SMSG_UPDATE_ACCOUNT_DATA);
		data << id;
	// if red does not exists if ID == 7 and if there is no data send 0
	if (!res || !res->data) // if error, send a NOTHING packet
	{
		data << (uint32)0;
	}
	else
	{
		data << res->sz;
		uLongf destsize;
		if(res->sz>200)
		{
			data.resize( res->sz+800 );  // give us plenty of room to work with..

			if ( ( compress(const_cast<uint8*>(data.contents()) + (sizeof(uint32)*2), &destsize, (const uint8*)res->data, res->sz)) != Z_OK)
			{
				OUT_DEBUG("Error while compressing ACCOUNT_DATA");
				return;
			}

			data.resize(destsize+8);
		}
		else
			data.append(	res->data,res->sz);
	}

	SendPacket(&data);
}

void WorldSession::HandleSetActionButtonOpcode(WorldPacket& recv_data)
{
	DEBUG_LOG( "WORLD"," Received CMSG_SET_ACTION_BUTTON" );
	uint8 button, misc, type;
	uint16 action;
	recv_data >> button >> action >> misc >> type;
	OUT_DEBUG( "BUTTON: %u ACTION: %u TYPE: %u MISC: %u", button, action, type, misc );
	if(action==0)
	{
		OUT_DEBUG( "MISC: Remove action from button %u", button );
		//remove the action button from the db
		GetPlayer()->setAction(button, 0, 0, 0);
	}
	else
	{
		if(button >= 120)
			return;

		if(type == 64 || type == 65)
		{
			OUT_DEBUG( "MISC: Added Macro %u into button %u", action, button );
			GetPlayer()->setAction(button,action,type,misc);
		}
		else if(type == 128)
		{
			OUT_DEBUG( "MISC: Added Item %u into button %u", action, button );
			GetPlayer()->setAction(button,action,type,misc);
		}
		else if(type == 0)
		{
			OUT_DEBUG( "MISC: Added Spell %u into button %u", action, button );
			GetPlayer()->setAction(button,action,type,misc);
		}
	}
}

void WorldSession::HandleSetWatchedFactionIndexOpcode(WorldPacket &recvPacket)
{
	uint32 factionid;
	recvPacket >> factionid;
	GetPlayer()->SetUInt32Value(PLAYER_FIELD_WATCHED_FACTION_INDEX, factionid);
}

void WorldSession::HandleTogglePVPOpcode(WorldPacket& recv_data)
{
	_player->PvPToggle();
}

void WorldSession::HandleAmmoSetOpcode(WorldPacket & recv_data)
{
	uint32 ammoId;
	recv_data >> ammoId;

	if(!ammoId)
		return;

	ItemPrototype * xproto=ItemPrototypeStorage.LookupEntry(ammoId);
	if(!xproto)
		return;

	if(xproto->Class != ITEM_CLASS_PROJECTILE || GetPlayer()->GetItemInterface()->GetItemCount(ammoId) == 0)
	{
		sCheatLog.writefromsession(GetPlayer()->GetSession(), "Definately cheating. tried to add %u as ammo.", ammoId);
		GetPlayer()->GetSession()->Disconnect();
		return;
	}

	if(xproto->RequiredLevel)
	{
		if(GetPlayer()->getLevel() < xproto->RequiredLevel)
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL,INV_ERR_ITEM_RANK_NOT_ENOUGH);
			_player->SetUInt32Value(PLAYER_AMMO_ID, 0);
			_player->CalcDamage();
			return;
		}
	}
	if(xproto->RequiredSkill)
	{
		if(!GetPlayer()->_HasSkillLine(xproto->RequiredSkill))
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL,INV_ERR_ITEM_RANK_NOT_ENOUGH);
			_player->SetUInt32Value(PLAYER_AMMO_ID, 0);
			_player->CalcDamage();
			return;
		}

		if(xproto->RequiredSkillRank)
		{
			if(_player->_GetSkillLineCurrent(xproto->RequiredSkill, false) < xproto->RequiredSkillRank)
			{
				GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL,INV_ERR_ITEM_RANK_NOT_ENOUGH);
				_player->SetUInt32Value(PLAYER_AMMO_ID, 0);
				_player->CalcDamage();
				return;
			}
		}
	}
	_player->SetUInt32Value(PLAYER_AMMO_ID, ammoId);
	_player->CalcDamage();
}

#define OPEN_CHEST 11437
void WorldSession::HandleGameObjectUse(WorldPacket & recv_data)
{
 	CHECK_INWORLD_RETURN;

	uint64 guid;
	recv_data >> guid;
	SpellCastTargets targets;
	Spell* spell = NULL;
	SpellEntry *spellInfo = NULL;
	OUT_DEBUG("WORLD: CMSG_GAMEOBJ_USE: [GUID %d]", guid);

	GameObject* obj = _player->GetMapMgr()->GetGameObject(GET_LOWGUID_PART(guid));
	if (!obj)
		return;

	GameObjectInfo *goinfo= obj->GetInfo();
	if (!goinfo)
		return;

	Player* plyr = GetPlayer();

	CALL_GO_SCRIPT_EVENT(obj, OnActivate)(_player);
	CALL_INSTANCE_SCRIPT_EVENT( _player->GetMapMgr(), OnGameObjectActivate )( obj, _player );

	uint32 type = obj->GetByte(GAMEOBJECT_BYTES_1, GAMEOBJECT_BYTES_TYPE_ID);
	switch (type)
	{
		case GAMEOBJECT_TYPE_CHAIR:
		{
            // if players are mounted they are not able to sit on a chair
            if( plyr->IsMounted() )
				plyr->RemoveAura( plyr->m_MountSpellId );

			plyr->SafeTeleport( plyr->GetMapId(), plyr->GetInstanceID(), obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), obj->GetOrientation() );
			plyr->SetStandState(STANDSTATE_SIT_MEDIUM_CHAIR);
		}break;
		case GAMEOBJECT_TYPE_CHEST: // cast da spell
		{
			spellInfo = dbcSpell.LookupEntry( OPEN_CHEST );
			spell = (new Spell(plyr, spellInfo, true, NULL));
			_player->m_currentSpell = spell;
			targets.m_unitTarget = obj->GetGUID();
			spell->prepare(&targets);
		}break;
		case GAMEOBJECT_TYPE_FISHINGNODE:
		{
			obj->UseFishingNode(plyr);
		}break;
		case GAMEOBJECT_TYPE_DOOR:
		{
			// door
			if((obj->GetByte(GAMEOBJECT_BYTES_1, GAMEOBJECT_BYTES_STATE) == 1) && (obj->GetUInt32Value(GAMEOBJECT_FLAGS) == 33))
				obj->EventCloseDoor();
			else
			{
				obj->SetUInt32Value(GAMEOBJECT_FLAGS, 33);
				obj->SetByte(GAMEOBJECT_BYTES_1,GAMEOBJECT_BYTES_STATE, 0);
				sEventMgr.AddEvent(obj,&GameObject::EventCloseDoor,EVENT_GAMEOBJECT_DOOR_CLOSE,20000,1,0);
			}
		}break;
		case GAMEOBJECT_TYPE_FLAGSTAND:
		{
			// battleground/warsong gulch flag
			if(plyr->m_bg)
			{
				if( plyr->m_stealth )
					plyr->RemoveAura( plyr->m_stealth );

				if( plyr->m_MountSpellId )
					plyr->RemoveAura( plyr->m_MountSpellId );

				if( plyr->m_CurrentVehicle )
					plyr->m_CurrentVehicle->RemovePassenger( plyr );

				if(!plyr->m_bgFlagIneligible)
					plyr->m_bg->HookFlagStand(plyr, obj);
			}

		}break;
		case GAMEOBJECT_TYPE_FLAGDROP:
		{
			// Dropped flag
			if(plyr->m_bg)
			{
				if( plyr->m_stealth )
					plyr->RemoveAura( plyr->m_stealth );

				if( plyr->m_MountSpellId )
					plyr->RemoveAura( plyr->m_MountSpellId );

				if( plyr->m_CurrentVehicle )
					plyr->m_CurrentVehicle->RemovePassenger( plyr );

				plyr->m_bg->HookFlagDrop(plyr, obj);
			}

		}break;
		case GAMEOBJECT_TYPE_QUESTGIVER:
		{
			// Questgiver
			if(obj->HasQuests())
				sQuestMgr.OnActivateQuestGiver(obj, plyr);
			// Gossip Script
			else if(obj->GetInfo()->gossip_script)
				obj->GetInfo()->gossip_script->GossipHello(obj, plyr, true);

		}break;
		case GAMEOBJECT_TYPE_SPELLCASTER:
		{
			SpellEntry *info = dbcSpell.LookupEntry(goinfo->SpellFocus);
			if(!info)
				break;
			Spell* spell = new Spell(plyr, info, false, NULL);
			// spell->SpellByOther = true;
			SpellCastTargets targets;
			targets.m_unitTarget = plyr->GetGUID();
			spell->prepare(&targets);
			if(obj->charges>0 && !--obj->charges)
				obj->ExpireAndDelete();
		}break;
		case GAMEOBJECT_TYPE_RITUAL:
		{
			// store the members in the ritual, cast sacrifice spell, and summon.
			uint32 i = 0;
			if(!obj->m_ritualmembers || !obj->m_ritualspell || !obj->m_ritualcaster /*|| !obj->m_ritualtarget*/)
				return;

			for(i = 0; i < goinfo->SpellFocus; i++)
			{
				if(!obj->m_ritualmembers[i])
				{
					obj->m_ritualmembers[i] = plyr->GetLowGUID();
					plyr->SetChannelSpellTargetGUID(obj->GetGUID());
					plyr->SetUInt32Value(UNIT_CHANNEL_SPELL, obj->m_ritualspell);
					break;
				}else if(obj->m_ritualmembers[i] == plyr->GetLowGUID())
				{
					// we're deselecting :(
					obj->m_ritualmembers[i] = 0;
					plyr->SetUInt32Value(UNIT_CHANNEL_SPELL, 0);
					plyr->SetChannelSpellTargetGUID(0);
					return;
				}
			}

			if(i == goinfo->SpellFocus - 1)
			{
				obj->m_ritualspell = 0;
				Player* plr;
				for(i = 0; i < goinfo->SpellFocus; i++)
				{
					plr = _player->GetMapMgr()->GetPlayer(obj->m_ritualmembers[i]);
					if(plr!=NULL)
					{
						plr->SetChannelSpellTargetGUID(0);
						plr->SetUInt32Value(UNIT_CHANNEL_SPELL, 0);
					}
				}

				SpellEntry *info = NULL;
				switch( goinfo->ID )
				{
				case 36727:// summon portal
					{
						if(!obj->m_ritualtarget)
							return;
						info = dbcSpell.LookupEntry(goinfo->sound1);
						if(!info)
							break;
						Player* target = _player->GetMapMgr()->GetPlayer(obj->m_ritualtarget);
						if(!target)
							return;

					spell = (new Spell(obj,info,true,NULL));
					SpellCastTargets targets;
					targets.m_unitTarget = target->GetGUID();
					spell->prepare(&targets);
					}break;
				case 177193:// doom portal
					{
					Player* psacrifice = NULL;
					Spell* spell = NULL;

					// kill the sacrifice player
					psacrifice = _player->GetMapMgr()->GetPlayer(obj->m_ritualmembers[(int)(rand()%(goinfo->SpellFocus-1))]);
					Player* pCaster = obj->GetMapMgr()->GetPlayer(obj->m_ritualcaster);
					if(!psacrifice || !pCaster)
						return;

					info = dbcSpell.LookupEntry(goinfo->sound4);
					if(!info)
						break;
					spell = (new Spell(psacrifice, info, true, NULL));
					targets.m_unitTarget = psacrifice->GetGUID();
					spell->prepare(&targets);

					// summons demon
					info = dbcSpell.LookupEntry(goinfo->sound1);
					spell = (new Spell(pCaster, info, true, NULL));
					SpellCastTargets targets;
					targets.m_unitTarget = pCaster->GetGUID();
					spell->prepare(&targets);
					}break;
				case 179944:// Summoning portal for meeting stones
					{
					Player* plr = _player->GetMapMgr()->GetPlayer(obj->m_ritualtarget);
					if(!plr)
						return;

					Player* pleader = _player->GetMapMgr()->GetPlayer(obj->m_ritualcaster);
					if(!pleader)
						return;

					info = dbcSpell.LookupEntry(goinfo->sound1);
					Spell* spell = new Spell(pleader, info, true, NULL);
					SpellCastTargets targets(plr->GetGUID());
					spell->prepare(&targets);

					/* expire the GameObject* */
					obj->ExpireAndDelete();
					}break;
				case 194108:// Ritual of Summoning portal for warlocks
					{
					Player* pleader = _player->GetMapMgr()->GetPlayer(obj->m_ritualcaster);
					if(!pleader)
						return;

					info = dbcSpell.LookupEntry(goinfo->sound1);
					Spell* spell = new Spell(pleader, info, true, NULL);
					SpellCastTargets targets(pleader->GetGUID());
					spell->prepare(&targets);

					obj->ExpireAndDelete();
					pleader->InterruptCurrentSpell();
					}break;
				case 186811://Ritual of Refreshment
				case 193062:
					{
						Player* pleader = _player->GetMapMgr()->GetPlayer(obj->m_ritualcaster);
						if(!pleader)
							return;

						info = dbcSpell.LookupEntry(goinfo->sound1);
						Spell* spell = new Spell(pleader, info, true, NULL);
						SpellCastTargets targets(pleader->GetGUID());
						spell->prepare(&targets);

						obj->ExpireAndDelete();
						pleader->InterruptCurrentSpell();
					}break;
				case 181622://Ritual of Souls
				case 193168:
					{
						Player* pleader = _player->GetMapMgr()->GetPlayer(obj->m_ritualcaster);
						if(!pleader)
							return;

						info = dbcSpell.LookupEntry(goinfo->sound1);
						Spell* spell = new Spell(pleader, info, true, NULL);
						SpellCastTargets targets(pleader->GetGUID());
						spell->prepare(&targets);
					}break;
				}
			}
		}break;
		case GAMEOBJECT_TYPE_GOOBER:
		{
			//Quest related mostly
		}
		case GAMEOBJECT_TYPE_CAMERA://eye of azora
		{
			/*WorldPacket pkt(SMSG_TRIGGER_CINEMATIC,4);
			pkt << (uint32)1;//i ve found only on such item,id =1
			SendPacket(&pkt);*/

			/* these are usually scripted effects. but in the case of some, (e.g. orb of translocation) the spellid is located in unknown1 */
			SpellEntry * sp = dbcSpell.LookupEntryForced(goinfo->Unknown1);
			if(sp != NULL)
				_player->CastSpell(_player,sp,true);
		}break;
		case GAMEOBJECT_TYPE_MEETINGSTONE:	// Meeting Stone
		{
			/* Use selection */
			Player* pPlayer = objmgr.GetPlayer((uint32)_player->GetSelection());
			if(!pPlayer || _player->GetGroup() != pPlayer->GetGroup() || !_player->GetGroup())
				return;

			GameObjectInfo * info = GameObjectNameStorage.LookupEntry(179944);
			if(!info)
				return;

			/* Create the summoning portal */
			GameObject* pGo = _player->GetMapMgr()->CreateGameObject(179944);
			if( pGo == NULL || !pGo->CreateFromProto(179944, _player->GetMapId(), _player->GetPositionX(), _player->GetPositionY(), _player->GetPositionZ(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f))
				return;

			// dont allow to spam them
			GameObject* gobj = _player->GetMapMgr()->GetInterface()->GetGameObjectNearestCoords(_player->GetPositionX(), _player->GetPositionY(), _player->GetPositionZ(), 179944);
			if( gobj )
				gobj->ExpireAndDelete();

			pGo->m_ritualcaster = _player->GetLowGUID();
			pGo->m_ritualtarget = pPlayer->GetLowGUID();
			pGo->m_ritualspell = 61994;	// meh
			pGo->PushToWorld(_player->GetMapMgr());

			/* member one: the (w00t) caster */
			pGo->m_ritualmembers[0] = _player->GetLowGUID();
			_player->SetChannelSpellTargetGUID(pGo->GetGUID());
			_player->SetUInt32Value(UNIT_CHANNEL_SPELL, pGo->m_ritualspell);

			/* expire after 2mins*/
			sEventMgr.AddEvent(pGo, &GameObject::_Expire, EVENT_GAMEOBJECT_EXPIRE, 120000, 1,0);
		}break;
		case GAMEOBJECT_TYPE_BARBER_CHAIR:
		{
			plyr->SafeTeleport( plyr->GetMapId(), plyr->GetInstanceID(), obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), obj->GetOrientation() );
			plyr->SetStandState(STANDSTATE_SIT_MEDIUM_CHAIR);
			plyr->m_lastRunSpeed = 0; //counteract mount-bug; reset speed to zero to force update SetPlayerSpeed in next line.
			plyr->SetPlayerSpeed(RUN,plyr->m_base_runSpeed);
			WorldPacket data(SMSG_ENABLE_BARBER_SHOP, 0);
			plyr->GetSession()->SendPacket(&data);
		}break;
	}
}

void WorldSession::HandleTutorialFlag( WorldPacket & recv_data )
{
	uint32 iFlag;
	recv_data >> iFlag;

	uint32 wInt = (iFlag / 32);
	uint32 rInt = (iFlag % 32);

	if(wInt >= 7)
	{
		Disconnect();
		return;
	}

	uint32 tutflag = GetPlayer()->GetTutorialInt( wInt );
	tutflag |= (1 << rInt);
	GetPlayer()->SetTutorialInt( wInt, tutflag );

	DEBUG_LOG("WorldSession","Received Tutorial Flag Set {%u}.", iFlag);
}

void WorldSession::HandleTutorialClear( WorldPacket & recv_data )
{
	for ( uint32 iI = 0; iI < 8; iI++)
		GetPlayer()->SetTutorialInt( iI, 0xFFFFFFFF );
}

void WorldSession::HandleTutorialReset( WorldPacket & recv_data )
{
	for ( uint32 iI = 0; iI < 8; iI++)
		GetPlayer()->SetTutorialInt( iI, 0x00000000 );
}

void WorldSession::HandleSetSheathedOpcode( WorldPacket & recv_data )
{
	uint32 active;
	recv_data >> active;
	_player->SetByte(UNIT_FIELD_BYTES_2,0,(uint8)active);
}

void WorldSession::HandlePlayedTimeOpcode(WorldPacket & recv_data)
{
	CHECK_INWORLD_RETURN;

	uint32 playedt = (uint32)UNIXTIME - _player->m_playedtime[2];
	uint8 displayinui = 0;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// As of 3.2.0a this is what the client sends to poll the /played time
	//
	// {CLIENT} Packet: (0x01CC) CMSG_PLAYED_TIME PacketSize = 1 TimeStamp = 691943484
	// 01
	//
	// Structure:
	// uint8 displayonui   -  1 when it should be printed on the screen, 0 when it shouldn't
	//
	//////////////////////////////////////////////////////////////////////////////////////////////////

	recv_data >> displayinui;

	DEBUG_LOG("WorldSession","Recieved CMSG_PLAYED_TIME.");
	DEBUG_LOG("WorldSession","displayinui: %lu", displayinui);

	if(playedt)
	{
		_player->m_playedtime[0] += playedt;
		_player->m_playedtime[1] += playedt;
		_player->m_playedtime[2] += playedt;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	//
	// As of 3.2.0a the server sends this as a response to the client /played time packet
	//
	//  {SERVER} Packet: (0x01CD) SMSG_PLAYED_TIME PacketSize = 9 TimeStamp = 691944000
	//  FE 0C 00 00 FE 0C 00 00 01
	//
	//
	// Structure:
	//
	// uint32 playedtotal      -   total time played in seconds
	// uint32 playedlevel      -   time played on this level in seconds
	// uint32 displayinui      -   1 when it should be printed on the screen, 0 when it shouldn't
	//
	//////////////////////////////////////////////////////////////////////////////////////////////////

	WorldPacket data(SMSG_PLAYED_TIME, 9); // again, an Aspire trick, with an uint8(0) -- I hate packet structure changes...
	data << (uint32)_player->m_playedtime[1];
	data << (uint32)_player->m_playedtime[0];
	data << uint8(displayinui);
	SendPacket(&data);

	DEBUG_LOG("WorldSession","Sent SMSG_PLAYED_TIME.");
	DEBUG_LOG("WorldSession","Total: %lu level: %lu", _player->m_playedtime[1], _player->m_playedtime[0]);
}

void WorldSession::HandleInspectOpcode( WorldPacket & recv_data )
{
	CHECK_PACKET_SIZE( recv_data, 8 );
	CHECK_INWORLD_RETURN;

	uint64 guid;
	uint32 talent_points = 61;
	recv_data >> guid;

	Player* player = _player->GetMapMgr()->GetPlayer( (uint32)guid );

	if( player == NULL )
		return;

	WorldPacket data(SMSG_INSPECT_TALENT, 1000);
	data << player->GetNewGUID();
	player->BuildPlayerTalentsInfo(&data, false);
    SendPacket( &data );

	// build items inspect part. could be sent separately as SMSG_INSPECT
	uint32 slotUsedMask = 0;
	uint16 enchantmentMask = 0;
	size_t maskPos = data.wpos();
	data << uint32(slotUsedMask);	// will be replaced later
	for(uint32 slot = 0; slot < EQUIPMENT_SLOT_END; slot++)
	{
		Item* item = player->GetItemInterface()->GetInventoryItem(slot);
		if( item )
		{
			slotUsedMask |= 1 << slot;
			data << uint32(item->GetEntry());
			size_t maskPosEnch = data.wpos();
			enchantmentMask = 0;
			data << uint16(enchantmentMask); // will be replaced later
			for(uint32 ench = 0; ench < 12; ench++)
			{
				uint16 enchId = (uint16) item->GetUInt32Value( ITEM_FIELD_ENCHANTMENT_1_1 + ench * 3);
				if( enchId )
				{
					enchantmentMask |= 1 << ench;
					data << uint16(enchId);
				}
			}
			*(uint16*)&data.contents()[maskPosEnch] = enchantmentMask;
			data << uint16(0);	// unk
			FastGUIDPack(data, item->GetUInt32Value(ITEM_FIELD_CREATOR));
			data << uint32(0);	// unk
		}
	}
	*(uint16*)&data.contents()[maskPos] = slotUsedMask;
	SendPacket( &data );
}

void WorldSession::HandleSetActionBarTogglesOpcode(WorldPacket &recvPacket)
{
	uint8 cActionBarId;
	recvPacket >> cActionBarId;
	DEBUG_LOG("WorldSession","Received CMSG_SET_ACTIONBAR_TOGGLES for actionbar id %d.", cActionBarId);
	GetPlayer()->SetByte(PLAYER_FIELD_BYTES,2, cActionBarId);
}

// Handlers for acknowledgement opcodes (removes some 'unknown opcode' flood from the logs)
void WorldSession::HandleAcknowledgementOpcodes( WorldPacket & recv_data )
{
	uint64 guid;
	CHECK_INWORLD_RETURN;

	recv_data >> guid;

	// not us? don't change our stuff.
	if( guid != _player->GetGUID() )
		return;

	switch(recv_data.GetOpcode())
	{
	case CMSG_MOVE_WATER_WALK_ACK:
		_player->m_waterwalk = _player->m_setwaterwalk;
		break;

	case CMSG_MOVE_SET_CAN_FLY_ACK:
		_player->FlyCheat = _player->m_setflycheat;
		break;

	case CMSG_FORCE_RUN_SPEED_CHANGE_ACK:
	case CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK:
	case CMSG_FORCE_SWIM_SPEED_CHANGE_ACK:
	case CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK:
	case CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK:
	case CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK:
	if(_player->m_speedChangeInProgress)
		{
			_player->ResetHeartbeatCoords();
			_player->DelaySpeedHack( 5000 );			// give the client a chance to fall/catch up
			_player->m_speedChangeInProgress = false;
		}
		break;
	}
}

void WorldSession::HandleSelfResurrectOpcode(WorldPacket& recv_data)
{
	uint32 self_res_spell = _player->GetUInt32Value(PLAYER_SELF_RES_SPELL);
	if(self_res_spell)
	{
		SpellEntry * sp = dbcSpell.LookupEntry(self_res_spell);
		Spell* s(new Spell(_player,sp,false,NULL));
		SpellCastTargets tgt;
		tgt.m_unitTarget=_player->GetGUID();
		s->prepare(&tgt);
	}
}

void WorldSession::HandleRandomRollOpcode(WorldPacket &recv_data)
{
	uint32 min, max;
	recv_data >> min >> max;

	OUT_DEBUG("WORLD: Received MSG_RANDOM_ROLL: %u-%u", min, max);

	WorldPacket data(20);
	data.SetOpcode(MSG_RANDOM_ROLL);
	data << min << max;

	if(max < min)
		return;

	uint32 roll;

	// generate number
	roll = RandomUInt(max - min) + min + 1;

	// append to packet, and guid
	data << roll << _player->GetGUID();

	// send to set
    if(_player->InGroup())
		_player->GetGroup()->SendPacketToAll(&data);
	else
	    GetPlayer()->SendMessageToSet(&data, true, true);
}

void WorldSession::HandleLootMasterGiveOpcode(WorldPacket& recv_data)
{
	CHECK_INWORLD_RETURN;
	uint32 itemid = 0;
	uint32 amt = 1;
	uint8 error = 0;
	SlotResult slotresult;

	Creature* pCreature = NULL;
	Loot *pLoot = NULL;
	/* struct:
	{CLIENT} Packet: (0x02A3) CMSG_LOOT_MASTER_GIVE PacketSize = 17
	|------------------------------------------------|----------------|
	|00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F |0123456789ABCDEF|
	|------------------------------------------------|----------------|
	|39 23 05 00 81 02 27 F0 01 7B FC 02 00 00 00 00 |9#....'..{......|
	|00											  |.			   |
	-------------------------------------------------------------------

		uint64 creatureguid
		uint8  slotid
		uint64 target_playerguid

	*/
	uint64 creatureguid, target_playerguid;
	uint8 slotid;
	recv_data >> creatureguid >> slotid >> target_playerguid;

	if(_player->GetGroup() == NULL || _player->GetGroup()->GetLooter() != _player->m_playerInfo)
		return;

	Player* player = _player->GetMapMgr()->GetPlayer((uint32)target_playerguid);
	if(!player)
		return;

	// cheaterz!
	if(_player->GetLootGUID() != creatureguid)
		return;

	//now its time to give the loot to the target player
	if(GET_TYPE_FROM_GUID(GetPlayer()->GetLootGUID()) == HIGHGUID_TYPE_UNIT)
	{
		pCreature = _player->GetMapMgr()->GetCreature(GET_LOWGUID_PART(creatureguid));
		if (!pCreature)return;
		pLoot=&pCreature->m_loot;
	}

	if(!pLoot) return;

	if (slotid >= pLoot->items.size())
	{
		OUT_DEBUG("AutoLootItem: Player %s might be using a hack! (slot %d, size %d)",
						GetPlayer()->GetName(), slotid, pLoot->items.size());
		return;
	}

	amt = pLoot->items.at(slotid).iItemsCount;

	if (!pLoot->items.at(slotid).ffa_loot)
	{
		if (!amt)//Test for party loot
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL,INV_ERR_ALREADY_LOOTED);
			return;
		}
	}
	else
	{
		//make sure this player can still loot it in case of ffa_loot
		LooterSet::iterator itr = pLoot->items.at(slotid).has_looted.find(player->GetLowGUID());

		if (pLoot->items.at(slotid).has_looted.end() != itr)
		{
			GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL,INV_ERR_ALREADY_LOOTED);
			return;
		}
	}

	itemid = pLoot->items.at(slotid).item.itemproto->ItemId;
	ItemPrototype* it = pLoot->items.at(slotid).item.itemproto;

	if((error = player->GetItemInterface()->CanReceiveItem(it, 1, 0)))
	{
		_player->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, error);
		return;
	}

	slotresult = player->GetItemInterface()->FindFreeInventorySlot(it);
	if(!slotresult.Result)
	{
		GetPlayer()->GetItemInterface()->BuildInventoryChangeError(NULL, NULL, INV_ERR_INVENTORY_FULL);
		return;
	}

	Item* item = objmgr.CreateItem( itemid, player);

	item->SetUInt32Value(ITEM_FIELD_STACK_COUNT,amt);
	if(pLoot->items.at(slotid).iRandomProperty!=NULL)
	{
		item->SetRandomProperty(pLoot->items.at(slotid).iRandomProperty->ID);
		item->ApplyRandomProperties(false);
	}
	else if(pLoot->items.at(slotid).iRandomSuffix != NULL)
	{
		item->SetRandomSuffix(pLoot->items.at(slotid).iRandomSuffix->id);
		item->ApplyRandomProperties(false);
	}

	if( player->GetItemInterface()->SafeAddItem(item,slotresult.ContainerSlot, slotresult.Slot) )
	{
		player->GetSession()->SendItemPushResult(item,false,true,true,true,slotresult.ContainerSlot,slotresult.Slot,1);
		sQuestMgr.OnPlayerItemPickup(player,item);
	}
	else
	{
		item->Destructor();
	}

	pLoot->items.at(slotid).iItemsCount = 0;

	// this gets sent to all looters
	if (!pLoot->items.at(slotid).ffa_loot)
	{
		pLoot->items.at(slotid).iItemsCount = 0;

		// this gets sent to all looters
		WorldPacket data(1);
		data.SetOpcode(SMSG_LOOT_REMOVED);
		data << slotid;
		Player* plr;
		for(LooterSet::iterator itr = pLoot->looters.begin(); itr != pLoot->looters.end(); itr++)
		{
			if((plr = _player->GetMapMgr()->GetPlayer(*itr)))
				plr->GetSession()->SendPacket(&data);
		}
	}
	else
	{
		pLoot->items.at(slotid).has_looted.insert(player->GetLowGUID());
	}
}

void WorldSession::HandleLootRollOpcode(WorldPacket& recv_data)
{
	CHECK_INWORLD_RETURN;
	/* struct:

	{CLIENT} Packet: (0x02A0) CMSG_LOOT_ROLL PacketSize = 13
	|------------------------------------------------|----------------|
	|00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F |0123456789ABCDEF|
	|------------------------------------------------|----------------|
	|11 4D 0B 00 BD 06 01 F0 00 00 00 00 02          |.M...........   |
	-------------------------------------------------------------------

	uint64 creatureguid
	uint32 slotid
	uint8  choice

	*/
	uint64 creatureguid;
	uint32 slotid;
	uint8 choice;
	recv_data >> creatureguid >> slotid >> choice;

	LootRoll* li = NULL;

	uint32 guidtype = GET_TYPE_FROM_GUID(creatureguid);
	if (guidtype == HIGHGUID_TYPE_GAMEOBJECT)
	{
		GameObject* pGO = _player->GetMapMgr()->GetGameObject(GET_LOWGUID_PART(creatureguid));
		if (!pGO)
			return;
		if (slotid >= pGO->m_loot.items.size() || pGO->m_loot.items.size() == 0)
			return;
		if (pGO->GetInfo() && pGO->GetInfo()->Type == GAMEOBJECT_TYPE_CHEST)
			li = pGO->m_loot.items[slotid].roll;
	}
	else if (guidtype == HIGHGUID_TYPE_UNIT)
	{
		// Creatures
		Creature* pCreature = _player->GetMapMgr()->GetCreature(GET_LOWGUID_PART(creatureguid));
		if (!pCreature)
			return;

		if (slotid >= pCreature->m_loot.items.size() || pCreature->m_loot.items.size() == 0)
			return;

		li = pCreature->m_loot.items[slotid].roll;
	}
	else
		return;

	if(!li)
		return;

	// li->PlayerRolled(_player, choice); N0000B!!111
}

void WorldSession::HandleOpenItemOpcode(WorldPacket &recv_data)
{
	CHECK_INWORLD_RETURN;
	CHECK_PACKET_SIZE(recv_data, 2);
	int8 slot, containerslot;
	recv_data >> containerslot >> slot;

	Item* pItem = _player->GetItemInterface()->GetInventoryItem(containerslot, slot);
	if(!pItem)
		return;

	// gift wrapping handler
	if(pItem->GetUInt32Value(ITEM_FIELD_GIFTCREATOR) && pItem->wrapped_item_id)
	{
		ItemPrototype * it = ItemPrototypeStorage.LookupEntry(pItem->wrapped_item_id);
		if(it == NULL)
			return;

		pItem->SetUInt32Value(ITEM_FIELD_GIFTCREATOR,0);
		pItem->SetUInt32Value(OBJECT_FIELD_ENTRY,pItem->wrapped_item_id);
		pItem->wrapped_item_id = 0;
		pItem->SetProto(it);

		if(it->Bonding==ITEM_BIND_ON_PICKUP)
			pItem->SetUInt32Value(ITEM_FIELD_FLAGS,1);
		else
			pItem->SetUInt32Value(ITEM_FIELD_FLAGS,0);

		if(it->MaxDurability)
		{
			pItem->SetUInt32Value(ITEM_FIELD_DURABILITY,it->MaxDurability);
			pItem->SetUInt32Value(ITEM_FIELD_MAXDURABILITY,it->MaxDurability);
		}

		pItem->m_isDirty=true;
		pItem->SaveToDB(containerslot,slot, false, NULL);
		return;
	}

	Lock *lock = dbcLock.LookupEntry( pItem->GetProto()->LockId );

	uint32 removeLockItems[5] = {0,0,0,0,0};

	if(lock) // locked item
	{
		for(int i = 0; i < 5; i++)
		{
			if(lock->locktype[i] == 1 && lock->lockmisc[i] > 0)
			{
				int8 slot = _player->GetItemInterface()->GetInventorySlotById(lock->lockmisc[i]);
				if(slot != ITEM_NO_SLOT_AVAILABLE && slot >= INVENTORY_SLOT_ITEM_START && slot < INVENTORY_SLOT_ITEM_END)
				{
					removeLockItems[i] = lock->lockmisc[i];
				}
				else
				{
					_player->GetItemInterface()->BuildInventoryChangeError(pItem,NULL,INV_ERR_ITEM_LOCKED);
					return;
				}
			}
			else if(lock->locktype[i] == 2 && pItem->locked)
			{
				_player->GetItemInterface()->BuildInventoryChangeError(pItem,NULL,INV_ERR_ITEM_LOCKED);
				return;
			}
		}
		for(int i = 0; i < 5; i++)
			if(removeLockItems[i])
				_player->GetItemInterface()->RemoveItemAmt(removeLockItems[i],1);
	}

	// fill loot
	_player->SetLootGUID(pItem->GetGUID());
	if( !pItem->m_looted )
	{
		// delete item from database, so we can't cheat
		pItem->DeleteFromDB();
		lootmgr.FillItemLoot(&pItem->m_loot, pItem->GetEntry());
		pItem->m_looted = true;
	}

	_player->SendLoot(pItem->GetGUID(), LOOT_DISENCHANTING);
}

void WorldSession::HandleCompleteCinematic(WorldPacket &recv_data)
{
	// when a Cinematic is started the player is going to sit down, when its finished its standing up.
	_player->SetStandState(STANDSTATE_STAND);
};

void WorldSession::HandleResetInstanceOpcode(WorldPacket& recv_data)
{
	sInstanceMgr.ResetSavedInstances(_player);
}

void EncodeHex(const char* source, char* dest, uint32 size)
{
	char temp[5];
	for(uint32 i = 0; i < size; ++i)
	{
		snprintf(temp, 5, "%02X", source[i]);
		strcat(dest, temp);
	}
}

void DecodeHex(const char* source, char* dest, uint32 size)
{
	char temp;
	char* acc = const_cast<char*>(source);
	for(uint32 i = 0; i < size; ++i)
	{
		sscanf("%02X", &temp);
		acc = ((char*)&source[2]);
		strcat(dest, &temp);
	}
}

void WorldSession::HandleToggleCloakOpcode(WorldPacket &recv_data)
{
	CHECK_INWORLD_RETURN;

	if(_player->HasFlag(PLAYER_FLAGS, PLAYER_FLAG_NOCLOAK))
		_player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAG_NOCLOAK);
	else
		_player->SetFlag(PLAYER_FLAGS, PLAYER_FLAG_NOCLOAK);
}

void WorldSession::HandleToggleHelmOpcode(WorldPacket &recv_data)
{
	CHECK_INWORLD_RETURN;

	if(_player->HasFlag(PLAYER_FLAGS, PLAYER_FLAG_NOHELM))
		_player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAG_NOHELM);
	else
		_player->SetFlag(PLAYER_FLAGS, PLAYER_FLAG_NOHELM);
}

void WorldSession::HandleDungeonDifficultyOpcode(WorldPacket& recv_data)
{
    uint32 data;
    recv_data >> data;

    if(_player->GetGroup() && _player->IsGroupLeader())
    {
        WorldPacket pData;
        pData.Initialize(MSG_SET_DUNGEON_DIFFICULTY);
        pData << data;

        _player->iInstanceType = data;
        sInstanceMgr.ResetSavedInstances(_player);

        Group * m_Group = _player->GetGroup();

        m_Group->Lock();

		for(uint32 i = 0; i < m_Group->GetSubGroupCount(); ++i)
		{
			for(GroupMembersSet::iterator itr = m_Group->GetSubGroup(i)->GetGroupMembersBegin(); itr != m_Group->GetSubGroup(i)->GetGroupMembersEnd(); itr++)
			{
				if((*itr)->m_loggedInPlayer)
				{
                    (*itr)->m_loggedInPlayer->iInstanceType = data;
					(*itr)->m_loggedInPlayer->GetSession()->SendPacket(&pData);
				}
			}
		}
		m_Group->Unlock();
    }
    else if(!_player->GetGroup())
    {
        _player->iInstanceType = data;
        sInstanceMgr.ResetSavedInstances(_player);
    }
}

void WorldSession::HandleRaidDifficultyOpcode(WorldPacket& recv_data)
{
	uint32 data;
	recv_data >> data;

	if(_player->GetGroup() && _player->IsGroupLeader())
	{
		WorldPacket pData;
		pData.Initialize(MSG_SET_RAID_DIFFICULTY);
		pData << data;

		_player->iRaidType = data;
		Group * m_Group = _player->GetGroup();

		m_Group->SetRaidDifficulty(data);
		m_Group->Lock();
		for(uint32 i = 0; i < m_Group->GetSubGroupCount(); ++i)
		{
			for(GroupMembersSet::iterator itr = m_Group->GetSubGroup(i)->GetGroupMembersBegin(); itr != m_Group->GetSubGroup(i)->GetGroupMembersEnd(); ++itr)
			{
				if((*itr)->m_loggedInPlayer)
				{
					(*itr)->m_loggedInPlayer->iRaidType = data;
					(*itr)->m_loggedInPlayer->GetSession()->SendPacket(&pData);
				}
			}
		}
		m_Group->Unlock();
	}
	else if(!_player->GetGroup())
		_player->iRaidType = data;
}

void WorldSession::HandleSummonResponseOpcode(WorldPacket & recv_data)
{
	uint64 summonguid;
	bool agree;
	recv_data >> summonguid;
	recv_data >> agree;

	// Do we have a summoner?
	if(!_player->m_summoner)
	{
		SendNotification("Summoner guid has changed or does not exist.");
		return;
	}

	// Summoner changed?
	if(_player->m_summoner->GetGUID() != summonguid)
	{
		SendNotification("Summoner guid has changed or does not exist.");
		return;
	}

	// not during combat
	if(_player->CombatStatus.IsInCombat())
		return;

	// Map checks. 
	MapInfo * inf = WorldMapInfoStorage.LookupEntry(_player->m_summonMapId);
	if(!inf)
		return;

	// are we summoning from witin the same instance?
	if( _player->m_summonInstanceId != _player->GetInstanceID() )
	{
		// if not, are we allowed on the summoners map?
		uint8 pReason = CheckTeleportPrerequsites(NULL, this, _player, inf->mapid);
		if( pReason )
		{
			SendNotification(NOTIFICATION_MESSAGE_NO_PERMISSION);
			return;
		}
	}
	if(agree)
	{
		if(!_player->SafeTeleport(_player->m_summonMapId, _player->m_summonInstanceId, _player->m_summonPos))
			SendNotification(NOTIFICATION_MESSAGE_FAILURE);

		_player->m_summoner = NULL;
		_player->m_summonInstanceId = _player->m_summonMapId = 0;
		return;
	}
	else
	{
		// Null-out the summoner
		_player->m_summoner = NULL;
		_player->m_summonInstanceId = _player->m_summonMapId = 0;
		return;
	}
}

void WorldSession::HandleDismountOpcode(WorldPacket& recv_data)
{
	DEBUG_LOG( "WORLD"," Received CMSG_DISMOUNT"  );

	if( !_player->IsInWorld() || _player->GetTaxiState())
		return;

	if( _player->IsMounted() )
		TO_UNIT(_player)->Dismount();
}

void WorldSession::HandleSetAutoLootPassOpcode(WorldPacket & recv_data)
{
	uint32 on;
	recv_data >> on;

	if( _player->IsInWorld() )
		_player->BroadcastMessage("Auto loot passing is now %s.", on ? "on" : "off");

	_player->m_passOnLoot = (on!=0) ? true : false;
}

void WorldSession::HandleRemoveGlyph(WorldPacket & recv_data)
{
	uint32 glyphSlot;
	recv_data >> glyphSlot;
	_player->UnapplyGlyph(glyphSlot);
	if(glyphSlot < GLYPHS_COUNT)
		_player->m_specs[_player->m_talentActiveSpec].glyphs[glyphSlot] = 0;
}

void WorldSession::HandleWorldStateUITimerUpdate(WorldPacket& recv_data)
{
	WorldPacket data(SMSG_WORLD_STATE_UI_TIMER_UPDATE, 4);
	data << (uint32)UNIXTIME;
	SendPacket(&data);
}

void WorldSession::HandleGameobjReportUseOpCode( WorldPacket& recv_data )
{
	if(!_player->IsInWorld())
	{
		SKIP_READ_PACKET(recv_data);
		return;
	}

	uint64 guid;
	recv_data >> guid;
	GameObject* gameobj = _player->GetMapMgr()->GetGameObject(GET_LOWGUID_PART(guid));
	if(gameobj != NULL)
	{
		if(gameobj->CanActivate())
			sQuestMgr.OnGameObjectActivate(_player, gameobj);
	}
}

void WorldSession::HandleReadyForAccountDataTimes(WorldPacket& recv_data)
{
	DEBUG_LOG( "WORLD","Received CMSG_READY_FOR_ACCOUNT_DATA_TIMES" );

	// account data == UI config
	WorldPacket data(SMSG_ACCOUNT_DATA_TIMES, 4+1+4+8*4);
	data << uint32(UNIXTIME) << uint8(1) << uint32(0x15);
	for (int i = 0; i < 8; i++)
	{
		if(0x15 & (1 << i))
		{
			data << uint32(0);
		}
	}
	SendPacket(&data);
}

void WorldSession::HandleFarsightOpcode(WorldPacket& recv_data)
{
	uint8 type;
	recv_data >> type;

	GetPlayer()->UpdateVisibility();
}
