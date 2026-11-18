/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CapitalFillMgr.h"
#include "AreaDefines.h"
#include "GameTime.h"
#include "Map.h"
#include "MapMgr.h"
#include "MotionMaster.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "TravelMgr.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>

namespace
{
    constexpr float BOT_SOURCE_PLAYER_DISTANCE = 150.0f;  // same as RandomTeleport's anti-blink check
    constexpr float SPOT_MAX_GROUND_OFFSET = 3.0f;
    constexpr time_t PENDING_ARRIVAL_SECONDS = 30;
    constexpr uint32 SHATTRATH_MIN_LEVEL = 58;
    constexpr uint32 DALARAN_MIN_LEVEL = 68;

    uint32 CapitalMinLevel(uint32 zoneId)
    {
        uint32 minLevel = sPlayerbotAIConfig.CapitalFillMinBotLevel;
        if (zoneId == AREA_SHATTRATH_CITY)
            return std::max(minLevel, SHATTRATH_MIN_LEVEL);
        if (zoneId == AREA_DALARAN)
            return std::max(minLevel, DALARAN_MIN_LEVEL);
        return minLevel;
    }
}

void CapitalFillMgr::Update(uint32 diff)
{
    if (!sPlayerbotAIConfig.CapitalFillEnabled || !sPlayerbotAIConfig.Enabled)
        return;

    _timer += diff;
    if (_timer < sPlayerbotAIConfig.CapitalFillCheckIntervalSeconds * IN_MILLISECONDS)
        return;
    _timer = 0;

    time_t const now = GameTime::GetGameTime().count();
    time_t const cooldown = sPlayerbotAIConfig.CapitalFillReuseCooldownSeconds;
    for (auto it = _broughtIn.begin(); it != _broughtIn.end();)
        it = now - it->second.at >= cooldown ? _broughtIn.erase(it) : std::next(it);

    std::map<uint32, OccupiedCapital> occupied;
    for (Player* player : sRandomPlayerbotMgr.GetPlayers())
    {
        if (!IsRealPlayer(player) || !player->IsInWorld() || player->IsGameMaster())
            continue;

        uint32 const zoneId = player->GetZoneId();
        if (!TravelMgr::IsCapitalZone(zoneId))
            continue;

        occupied[zoneId].playerPositions.push_back(player->GetPosition());
    }

    if (occupied.empty())
        return;

    std::vector<Player*> candidates;
    for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* bot = it->second;
        if (!bot)
            continue;

        // A bot teleported in recently may not be in the world or in its new zone yet.
        uint32 zoneId = bot->IsInWorld() ? bot->GetZoneId() : 0;
        auto const brought = _broughtIn.find(bot->GetGUID());
        if (brought != _broughtIn.end() && now - brought->second.at < PENDING_ARRIVAL_SECONDS)
            zoneId = brought->second.zoneId;
        if (!zoneId)
            continue;

        auto capital = occupied.find(zoneId);
        if (capital != occupied.end())
            ++capital->second.botCount;
        else if (IsCandidate(bot, now))
            candidates.push_back(bot);
    }

    std::shuffle(candidates.begin(), candidates.end(), RandomEngine::Instance());
    for (auto const& [zoneId, capital] : occupied)
        FillCapital(zoneId, capital, candidates);
}

bool CapitalFillMgr::IsCandidate(Player* bot, time_t now) const
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI || botAI->GetMaster() || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;

    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->IsBeingTeleported() || bot->IsRooted() ||
        bot->HasUnitState(UNIT_STATE_IN_FLIGHT) || bot->HasUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT))
        return false;

    if (bot->GetGroup() || bot->InBattleground() || bot->InArena() || bot->InBattlefield() ||
        bot->InBattlegroundQueue() || bot->GetMap()->Instanceable())
        return false;

    if (bot->GetLevel() < sPlayerbotAIConfig.CapitalFillMinBotLevel)
        return false;

    auto const brought = _broughtIn.find(bot->GetGUID());
    return brought == _broughtIn.end() ||
           now - brought->second.at >= sPlayerbotAIConfig.CapitalFillReuseCooldownSeconds;
}

void CapitalFillMgr::FillCapital(uint32 zoneId, OccupiedCapital const& capital, std::vector<Player*>& candidates)
{
    if (capital.botCount >= sPlayerbotAIConfig.CapitalFillTargetBots)
        return;

    uint32 const wanted = std::min(sPlayerbotAIConfig.CapitalFillTargetBots - capital.botCount,
                                   sPlayerbotAIConfig.CapitalFillMaxBotsPerCheck);
    TeamId const team = TravelMgr::GetCapitalTeam(zoneId);
    uint32 const minLevel = CapitalMinLevel(zoneId);

    uint32 brought = 0;
    for (Player*& bot : candidates)
    {
        if (brought >= wanted)
            break;

        if (!bot || (team != TEAM_NEUTRAL && bot->GetTeamId() != team) || bot->GetLevel() < minLevel)
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || botAI->HasPlayerNearby(BOT_SOURCE_PLAYER_DISTANCE))
            continue;

        if (TeleportIn(bot, zoneId, capital))
            ++brought;
        bot = nullptr;  // used or unsuitable for this capital; either way not offered again this check
    }

    if (brought)
        LOG_DEBUG("playerbots", "CapitalFill: brought {} bots into zone {} ({} already there)", brought, zoneId,
                  capital.botCount);
}

bool CapitalFillMgr::TeleportIn(Player* bot, uint32 zoneId, OccupiedCapital const& capital)
{
    std::vector<WorldLocation> const& spots = sTravelMgr.GetCapitalSpots(zoneId);
    if (spots.empty())
        return false;

    float const minDistance = sPlayerbotAIConfig.CapitalFillSpotMinPlayerDistance;
    std::vector<WorldLocation const*> hidden;
    for (WorldLocation const& spot : spots)
    {
        bool const seen = std::any_of(capital.playerPositions.begin(), capital.playerPositions.end(),
                                      [&](Position const& pos) { return spot.GetExactDist2d(pos) < minDistance; });
        if (!seen)
            hidden.push_back(&spot);
    }

    WorldLocation const& spot =
        hidden.empty() ? spots[urand(0, spots.size() - 1)] : *hidden[urand(0, hidden.size() - 1)];

    Map* map = sMapMgr->FindMap(spot.GetMapId(), 0);
    if (!map)
        return false;

    float z = spot.GetPositionZ();
    float const ground = map->GetHeight(bot->GetPhaseMask(), spot.GetPositionX(), spot.GetPositionY(), z + 2.0f);
    if (ground > INVALID_HEIGHT && std::fabs(ground - z) < SPOT_MAX_GROUND_OFFSET)
        z = ground;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    bot->GetMotionMaster()->Clear();
    botAI->Reset(true);
    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
    if (!bot->TeleportTo(spot.GetMapId(), spot.GetPositionX(), spot.GetPositionY(), z + 0.05f,
                         spot.GetOrientation()))
        return false;
    bot->SendMovementFlagUpdate();
    botAI->rpgInfo.ChangeToWanderNpc();

    _broughtIn[bot->GetGUID()] = {GameTime::GetGameTime().count(), zoneId};
    return true;
}
