/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_QUESTOBJECTIVESPAWNS_H
#define PLAYERBOTS_QUESTOBJECTIVESPAWNS_H

#include "Define.h"
#include "Position.h"
#include <unordered_map>
#include <vector>

// Spawn positions of every quest objective's targets: the creatures/gameobjects to kill or use, and those
// that drop a required item. Objective indexes match QuestPOI::ObjectiveIndex (0-3 kill/use, 4-7 items).
// Built once on the world thread at startup and read-only afterwards, so map threads can share it.
class QuestObjectiveSpawns
{
public:
    static QuestObjectiveSpawns& Instance()
    {
        static QuestObjectiveSpawns instance;

        return instance;
    }

    void Initialize();

    // Spawn of the objective's targets on mapId nearest (2D) to (x, y) within maxDistance.
    bool FindNearestSpawn(uint32 questId, int32 objectiveIdx, uint32 mapId, float x, float y, float maxDistance,
                          Position& spawn) const;

private:
    QuestObjectiveSpawns() = default;
    ~QuestObjectiveSpawns() = default;

    QuestObjectiveSpawns(QuestObjectiveSpawns const&) = delete;
    QuestObjectiveSpawns& operator=(QuestObjectiveSpawns const&) = delete;

    QuestObjectiveSpawns(QuestObjectiveSpawns&&) = delete;
    QuestObjectiveSpawns& operator=(QuestObjectiveSpawns&&) = delete;

    struct Spawn
    {
        uint32 mapId;
        Position pos;
    };

    static uint64 MakeKey(uint32 questId, int32 objectiveIdx) { return (uint64(questId) << 8) | uint8(objectiveIdx); }

    std::unordered_map<uint64, std::vector<Spawn>> _spawns;
};

#endif
