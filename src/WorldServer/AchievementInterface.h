/*
 * Arctic MMORPG Server Software
 * Copyright (c) 2008-2014 Arctic Server Team
 * See COPYING for license details.
 */

#pragma once

#include "AchievementDefines.h"

struct AchievementData
{
	uint32 id;
	bool completed;
	uint32 date;
	uint32 groupid;
	uint32 num_criterias;
	uint32 counter[32];
	uint32 completionTimeLast;
	bool m_isDirty;
};

inline uint32 unixTimeToTimeBitfields(time_t secs)
{
	tm* lt = localtime(&secs);
	return (lt->tm_year - 100) << 24 | lt->tm_mon  << 20 | (lt->tm_mday - 1) << 14 | lt->tm_wday << 11 | lt->tm_hour << 6 | lt->tm_min;
}

typedef std::set<AchievementCriteriaEntry*>							AchievementCriteriaSet;
typedef std::map<uint32, AchievementCriteriaSet*>					AchievementCriteriaMap;

class SERVER_DECL AchievementInterface
{
	Player* m_player;
	map<uint32,AchievementData*> m_achivementDataMap;
private:
	void GiveRewardsForAchievement(AchievementEntry * ae);
	void EventAchievementEarned(AchievementData * pData);
	void SendCriteriaUpdate(AchievementData * ad, uint32 idx);
	bool CanCompleteAchievement(AchievementData * ad);
	bool HandleBeforeChecks(AchievementData * ad);
	AchievementData* CreateAchievementDataEntryForAchievement(AchievementEntry * ae);

	// Gets AchievementData struct. If there is none, one will be created.
	AchievementData* GetAchievementDataByAchievementID(uint32 ID);

	WorldPacket* m_achievementInspectPacket;

public:
	AchievementInterface(Player* plr);
	~AchievementInterface();

	void LoadFromDB( QueryResult * pResult );
	void SaveToDB(QueryBuffer * buffer);

	WorldPacket* BuildAchievementEarned(AchievementData * pData);
	WorldPacket* BuildAchievementData(bool forInspect = false);

	bool HasAchievements() { return m_achivementDataMap.size() > 0; }

	void HandleAchievementCriteriaConditionDeath();

	// Handlers for misc events
	void HandleAchievementCriteriaKillCreature(uint32 killedMonster);
	void HandleAchievementCriteriaWinBattleground(uint32 bgMapId, uint32 scoreMargin, uint32 time, CBattleground* bg);
	void HandleAchievementCriteriaRequiresAchievement(uint32 achievementId);
	void HandleAchievementCriteriaLevelUp(uint32 level);
	void HandleAchievementCriteriaOwnItem(uint32 itemId, uint32 stack = 1);
	void HandleAchievementCriteriaLootItem(uint32 itemId, uint32 stack = 1);
	void HandleAchievementCriteriaQuestCount(uint32 questCount);
	void HandleAchievementCriteriaHonorableKillClass(uint32 classId);
	void HandleAchievementCriteriaHonorableKillRace(uint32 raceId);
	void HandleAchievementCriteriaHonorableKill();
	void HandleAchievementCriteriaTalentResetCount();
	void HandleAchievementCriteriaTalentResetCostTotal(uint32 cost);
	void HandleAchievementCriteriaBuyBankSlot(bool retroactive = false);
	void HandleAchievementCriteriaFlightPathsTaken();
	void HandleAchievementCriteriaExploreArea(uint32 areaId, uint32 explorationFlags);
	void HandleAchievementCriteriaDoEmote(uint32 emoteId, Unit* pTarget);
	void HandleAchievementCriteriaCompleteQuestsInZone(uint32 zoneId);
	void HandleAchievementCriteriaReachSkillLevel(uint32 skillId, uint32 skillLevel);
	void HandleAchievementCriteriaWinDuel();
	void HandleAchievementCriteriaLoseDuel();
	void HandleAchievementCriteriaKilledByCreature(uint32 creatureEntry);
	void HandleAchievementCriteriaKilledByPlayer();
	void HandleAchievementCriteriaDeath();
	void HandleAchievementCriteriaDeathAtMap(uint32 mapId);
};

