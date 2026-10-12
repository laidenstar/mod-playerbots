/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "QuestObjectiveSpawns.h"

#include "Log.h"
#include "LootValues.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Timer.h"

void QuestObjectiveSpawns::Initialize()
{
    uint32 const oldMSTime = getMSTime();
    _spawns.clear();

    // creature entry (> 0) or -gameobject entry -> keys of the objectives it serves
    std::unordered_map<int32, std::vector<uint64>> entryObjectives;
    // required item -> keys of the objectives collecting it
    std::unordered_map<uint32, std::vector<uint64>> itemObjectives;

    for (auto const& [questId, quest] : sObjectMgr->GetQuestTemplates())
    {
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            if (int32 const npcOrGo = quest->RequiredNpcOrGo[i])
                entryObjectives[npcOrGo].push_back(MakeKey(questId, i));
        }

        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            if (uint32 const itemId = quest->RequiredItemId[i])
                itemObjectives[itemId].push_back(MakeKey(questId, QUEST_OBJECTIVES_COUNT + i));
        }
    }

    // Creatures and gameobjects dropping a required item serve that item's objectives. Read the loot
    // tables directly: the shared "drop map" value cannot be built this early in startup.
    auto addDropper = [&](int32 entry, ObjectGuid guid)
    {
        LootTemplateAccess const* lootTemplate = DropMapValue::GetLootTemplate(guid, LOOT_CORPSE);
        if (!lootTemplate)
            return;

        for (auto const& lootItem : lootTemplate->Entries)
        {
            auto const itr = itemObjectives.find(lootItem->itemid);
            if (itr == itemObjectives.end())
                continue;

            std::vector<uint64>& keys = entryObjectives[entry];
            keys.insert(keys.end(), itr->second.begin(), itr->second.end());
        }
    };

    for (auto const& [entry, creatureTemplate] : *sObjectMgr->GetCreatureTemplates())
        addDropper(int32(entry), ObjectGuid::Create<HighGuid::Unit>(entry, uint32(1)));

    for (auto const& [entry, goTemplate] : *sObjectMgr->GetGameObjectTemplates())
        addDropper(-int32(entry), ObjectGuid::Create<HighGuid::GameObject>(entry, uint32(1)));

    auto addSpawn = [&](int32 entry, uint32 mapId, float x, float y, float z)
    {
        auto const itr = entryObjectives.find(entry);
        if (itr == entryObjectives.end())
            return;

        for (uint64 key : itr->second)
            _spawns[key].push_back({ mapId, Position(x, y, z) });
    };

    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        // creature_multispawn alternates can also fill this spawn point
        for (uint32 entry : { data.id, data.id2, data.id3 })
        {
            if (entry)
                addSpawn(int32(entry), data.mapid, data.posX, data.posY, data.posZ);
        }
    }

    for (auto const& [spawnId, data] : sObjectMgr->GetAllGOData())
        addSpawn(-int32(data.id), data.mapid, data.posX, data.posY, data.posZ);

    LOG_INFO("server.loading", ">> Loaded spawn positions for {} quest objectives in {} ms", _spawns.size(),
             GetMSTimeDiffToNow(oldMSTime));
}

bool QuestObjectiveSpawns::FindNearestSpawn(uint32 questId, int32 objectiveIdx, uint32 mapId, float x, float y,
                                            float maxDistance, Position& spawn) const
{
    if (objectiveIdx < 0)
        return false;

    auto const itr = _spawns.find(MakeKey(questId, objectiveIdx));
    if (itr == _spawns.end())
        return false;

    float bestDistSq = maxDistance * maxDistance;
    bool found = false;
    for (Spawn const& candidate : itr->second)
    {
        if (candidate.mapId != mapId)
            continue;

        float const dx = candidate.pos.GetPositionX() - x;
        float const dy = candidate.pos.GetPositionY() - y;
        float const distSq = dx * dx + dy * dy;
        if (distSq <= bestDistSq)
        {
            bestDistSq = distSq;
            spawn = candidate.pos;
            found = true;
        }
    }

    return found;
}
