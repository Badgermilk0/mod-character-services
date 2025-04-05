/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "Chat.h"
#include "ScriptedGossip.h"
#include "IndividualProgression.h"

class CharacterServices : public CreatureScript
{
  public:
    CharacterServices() : CreatureScript("character_services") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
      if (!sConfigMgr->GetOption<bool>("CharacterServices.Enable", false))
       return false;

      if (!player || !player->GetSession())
          return false;

      if (player->HasAtLoginFlag(AT_LOGIN_RENAME) || player->HasAtLoginFlag(AT_LOGIN_CUSTOMIZE) || player->HasAtLoginFlag(AT_LOGIN_CHANGE_RACE) || player->HasAtLoginFlag(AT_LOGIN_CHANGE_FACTION))
      {
        ChatHandler(player->GetSession()).PSendSysMessage("You already have a pending service. Please log out to use it.");
        CloseGossipMenuFor(player);
        return false;
      }

      if (sConfigMgr->GetOption<bool>("CharacterServices.NameChange.Enable", false))
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "|TInterface/Icons/inv_misc_note_02:50:50|tChange My Name", GOSSIP_SENDER_MAIN, 1, "Confirm Name Change.", GetServiceCost(player, 1), false);
      if (sConfigMgr->GetOption<bool>("CharacterServices.AppearanceChange.Enable", false))
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "|TInterface/Icons/ability_rogue_disguise:50:50|tChange My Appearance", GOSSIP_SENDER_MAIN, 2, "Confirm Appearance Change.", GetServiceCost(player, 2), false);
      if (sConfigMgr->GetOption<bool>("CharacterServices.RaceChange.Enable", false))
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "|TInterface/Icons/inv_misc_grouplooking:50:50|tChange My Race", GOSSIP_SENDER_MAIN, 3, "Confirm Race Change.", GetServiceCost(player, 3), false);
      if (sConfigMgr->GetOption<bool>("CharacterServices.FactionChange.Enable", false))
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "|TInterface/Icons/inv_bannerpvp_03:50:50|tChange My Faction", GOSSIP_SENDER_MAIN, 4, "Confirm Faction Change.", GetServiceCost(player, 4), false);
      
      // Add progression tier purchase option if enabled
      if (sConfigMgr->GetOption<bool>("CharacterServices.ProgressionTier.Enable", false))
      {
        uint8 currentTier = player->GetPlayerSetting("mod-individual-progression", SETTING_PROGRESSION_STATE).value;
        uint8 accountMaxTier = sIndividualProgression->GetAccountProgression(player->GetSession()->GetAccountId());
        uint8 maxAllowedTier = sConfigMgr->GetOption<uint8>("CharacterServices.ProgressionTier.MaxApplicableTier", 0);
        
        // If MaxApplicableTier is set (non-zero), use it as a cap
        if (maxAllowedTier > 0 && accountMaxTier > maxAllowedTier)
        {
          accountMaxTier = maxAllowedTier;
        }
        
        // Only offer if there's an upgrade available
        if (accountMaxTier > currentTier)
        {
          // Get the next progression tier name
          uint8 nextTier = currentTier + 1;
          std::string nextTierName = GetProgressionTierName(nextTier);
          std::string tierIcon = GetTierIcon(nextTier);
          
          AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, 
            "|T" + tierIcon + ":50:50|tProgress to " + nextTierName, 
            GOSSIP_SENDER_MAIN, 100 + nextTier, // Use 100 + tier as the action ID
            "Purchase progression to: " + nextTierName, 
            GetProgressionTierCost(player, nextTier), false);
        }
        else if (currentTier < PROGRESSION_WOTLK_TIER_5) // Not at max progression
        {
          // Inform player they need to progress on another character first
          ChatHandler(player->GetSession()).PSendSysMessage("You must first complete this progression tier on another character before you can purchase it here.");
        }
      }

      SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());

      return true;
    }

    bool OnGossipSelect(Player* player, Creature* /*creature*/, uint32 /*sender*/, uint32 action) override
    {
      if (!player || !player->GetSession())
          return false;
          
      ClearGossipMenuFor(player);
      
      // Handle progression tier purchases (action IDs 100+)
      if (action >= 100)
      {
        uint8 tierToPurchase = action - 100;
        uint32 cost = GetProgressionTierCost(player, tierToPurchase);
        
        if (!player->HasEnoughMoney(cost))
        {
          ChatHandler(player->GetSession()).PSendSysMessage("You do not have enough money for this service.");
          CloseGossipMenuFor(player);
          return false;
        }
        
        player->ModifyMoney(-cost);
        ProcessProgressionTierPurchase(player, tierToPurchase);
        CloseGossipMenuFor(player);
        return true;
      }
      
      // Handle standard services (action IDs 1-4)
      if (!player->HasEnoughMoney(GetServiceCost(player, action)))
      {
        ChatHandler(player->GetSession()).PSendSysMessage("You do not have enough money for this service.");
        CloseGossipMenuFor(player);
        return false;
      }

      player->ModifyMoney(-GetServiceCost(player, action));

      switch (action)
      {
          case 1:
            player->SetAtLoginFlag(AT_LOGIN_RENAME);
            ChatHandler(player->GetSession()).PSendSysMessage("{} flagged for name change at the character screen. Please log out.", player->GetPlayerName());
            break;
          case 2:
            player->SetAtLoginFlag(AT_LOGIN_CUSTOMIZE);
            ChatHandler(player->GetSession()).PSendSysMessage("{} flagged for appearance change at the character screen. Please log out.", player->GetPlayerName());
            break;
          case 3:
            player->SetAtLoginFlag(AT_LOGIN_CHANGE_RACE);
            ChatHandler(player->GetSession()).PSendSysMessage("{} flagged for race change at the character screen. Please log out.", player->GetPlayerName());
            break;
          case 4:
            player->SetAtLoginFlag(AT_LOGIN_CHANGE_FACTION);
            ChatHandler(player->GetSession()).PSendSysMessage("{} flagged for faction change at the character screen. Please log out.", player->GetPlayerName());
            break;
      }
      CloseGossipMenuFor(player);

      return true;
    }

    void ProcessProgressionTierPurchase(Player* player, uint8 newTier)
    {
      if (!player || !player->GetSession())
          return;
          
      uint8 currentTier = player->GetPlayerSetting("mod-individual-progression", SETTING_PROGRESSION_STATE).value;
      uint8 accountMaxTier = sIndividualProgression->GetAccountProgression(player->GetSession()->GetAccountId());
      uint8 maxAllowedTier = sConfigMgr->GetOption<uint8>("CharacterServices.ProgressionTier.MaxApplicableTier", 0);
      
      // Check against max allowed tier if configured
      if (maxAllowedTier > 0)
      {
        // Cap the accountMaxTier by the configured maximum
        if (accountMaxTier > maxAllowedTier)
        {
            accountMaxTier = maxAllowedTier;
        }
        
        // Also ensure we don't let players purchase beyond the max allowed tier
        if (newTier > maxAllowedTier)
        {
            ChatHandler(player->GetSession()).PSendSysMessage("This progression tier is not available for purchase.");
            return;
        }
      }
      
      // Safety checks
      if (newTier <= currentTier)
      {
        ChatHandler(player->GetSession()).PSendSysMessage("You are already at or beyond this progression tier.");
        return;
      }
      
      if (newTier > accountMaxTier)
      {
        ChatHandler(player->GetSession()).PSendSysMessage("You cannot progress beyond your account's maximum tier.");
        return;
      }
      
      // Only allow progression to the next tier (preventing skipping tiers)
      if (newTier > currentTier + 1)
      {
        ChatHandler(player->GetSession()).PSendSysMessage("You can only purchase the next progression tier.");
        return;
      }
      
      // Progress to the new tier
      sIndividualProgression->ForceUpdateProgressionState(player, static_cast<ProgressionState>(newTier));
      
      std::string tierName = GetProgressionTierName(newTier);
      ChatHandler(player->GetSession()).PSendSysMessage("You have successfully progressed to {}.", tierName);
    }

    std::string GetTierIcon(uint8 tier)
    {
      switch (tier)
      {
        case PROGRESSION_START:
          return "Interface/Icons/inv_misc_map02";
        case PROGRESSION_MOLTEN_CORE:
          return "Interface/Icons/achievement_boss_ragnaros";
        case PROGRESSION_ONYXIA:
          return "Interface/Icons/achievement_boss_onyxia";
        case PROGRESSION_BLACKWING_LAIR:
          return "Interface/Icons/achievement_boss_nefarion";
        case PROGRESSION_PRE_AQ:
          return "Interface/Icons/inv_misc_qirajicrystal_05";
        case PROGRESSION_AQ:
          return "Interface/Icons/achievement_boss_cthun";
        case PROGRESSION_NAXX40:
          return "Interface/Icons/achievement_boss_kelthuzad_01";
        case PROGRESSION_TBC_TIER_1:
          return "Interface/Icons/achievement_boss_princemalchezaar_02";
        case PROGRESSION_TBC_TIER_2:
          return "Interface/Icons/achievement_boss_kael'thassunstrider_01";
        case PROGRESSION_TBC_TIER_3:
          return "Interface/Icons/achievement_boss_illidan";
        case PROGRESSION_TBC_TIER_4:
          return "Interface/Icons/achievement_boss_zuljin";
        case PROGRESSION_TBC_TIER_5:
          return "Interface/Icons/achievement_boss_kiljaeden";
        case PROGRESSION_WOTLK_TIER_1:
          return "Interface/Icons/achievement_boss_kelthuzad_01";
        case PROGRESSION_WOTLK_TIER_2:
          return "Interface/Icons/achievement_boss_yoggsaron_01";
        case PROGRESSION_WOTLK_TIER_3:
          return "Interface/Icons/achievement_boss_anubarak_01";
        case PROGRESSION_WOTLK_TIER_4:
          return "Interface/Icons/achievement_boss_lichking";
        case PROGRESSION_WOTLK_TIER_5:
          return "Interface/Icons/achievement_boss_halion";
        default:
          return "Interface/Icons/inv_misc_questionmark";
      }
    }

    std::string GetProgressionTierName(uint8 tier)
    {
      switch (tier)
      {
        case PROGRESSION_START:
          return "Start";
        case PROGRESSION_MOLTEN_CORE:
          return "Molten Core";
        case PROGRESSION_ONYXIA:
          return "Onyxia's Lair";
        case PROGRESSION_BLACKWING_LAIR:
          return "Blackwing Lair";
        case PROGRESSION_PRE_AQ:
          return "Pre-AQ";
        case PROGRESSION_AQ:
          return "Ahn'Qiraj";
        case PROGRESSION_NAXX40:
          return "Naxxramas (Vanilla)";
        case PROGRESSION_TBC_TIER_1:
          return "TBC Tier 1";
        case PROGRESSION_TBC_TIER_2:
          return "TBC Tier 2";
        case PROGRESSION_TBC_TIER_3:
          return "TBC Tier 3";
        case PROGRESSION_TBC_TIER_4:
          return "TBC Tier 4";
        case PROGRESSION_TBC_TIER_5:
          return "TBC Tier 5";
        case PROGRESSION_WOTLK_TIER_1:
          return "WotLK Tier 1";
        case PROGRESSION_WOTLK_TIER_2:
          return "WotLK Tier 2";
        case PROGRESSION_WOTLK_TIER_3:
          return "WotLK Tier 3";
        case PROGRESSION_WOTLK_TIER_4:
          return "WotLK Tier 4";
        case PROGRESSION_WOTLK_TIER_5:
          return "WotLK Tier 5";
        default:
          return "Unknown Tier";
      }
    }

    uint32 GetProgressionTierCost(Player* player, uint8 tier)
    {
      bool dynamicCostEnabled = sConfigMgr->GetOption<bool>("CharacterServices.DynamicCost.Enable", false);
      double playerLevel = player->GetLevel();
      double dynamicRatio = (playerLevel / 80);
      
      std::string configOption = "CharacterServices.Cost.ProgressionTier." + std::to_string(tier);
      // Default cost is tier-dependent; higher tiers cost more
      uint32 defaultCost = 100 + (tier * 50);
      uint32 tierCost = sConfigMgr->GetOption<uint32>(configOption, defaultCost) * 10000;
      
      return dynamicCostEnabled ? dynamicRatio * tierCost : tierCost;
    }

    uint32 GetServiceCost(Player* player, uint8 service)
    {
       bool dynamicCostEnabled = sConfigMgr->GetOption<bool>("CharacterServices.DynamicCost.Enable", false);
       double playerLevel = player->GetLevel();
       double dynamicRatio = (playerLevel / 80);
       switch (service)
       {
         case 1:
           {
             uint32 serviceCost = sConfigMgr->GetOption<uint32>("CharacterServices.Cost.NameChange", 25) * 10000;
             return dynamicCostEnabled ? dynamicRatio * serviceCost : serviceCost;
           }
         case 2:
           {
             uint32 serviceCost = sConfigMgr->GetOption<uint32>("CharacterServices.Cost.AppearanceChange", 50) * 10000;
             return dynamicCostEnabled ? dynamicRatio * serviceCost : serviceCost;
           }
         case 3:
           {
             uint32 serviceCost = sConfigMgr->GetOption<uint32>("CharacterServices.Cost.RaceChange", 100) * 10000;
             return dynamicCostEnabled ? dynamicRatio * serviceCost : serviceCost;
           }
         case 4:
           {
             uint32 serviceCost = sConfigMgr->GetOption<uint32>("CharacterServices.Cost.FactionChange", 250) * 10000;
             return dynamicCostEnabled ? dynamicRatio * serviceCost : serviceCost;
           }
         default:
           return 250 * 10000;
       }
    }
};

void AddCharacterServicesScripts()
{
    new CharacterServices();
};
