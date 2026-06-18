#include "RaceInfos.h"

#include "Game.h"
#include "WoWDatabase.h"
#include "WoWModel.h"

#include "logger/Logger.h"

#include <algorithm>

#define DEBUG_RACEINFOS 1

std::map<int, RaceInfos> RaceInfos::RACES;

void RaceInfos::init()
{
  auto races =
    GAMEDATABASE.sqlQuery("SELECT ChrRaces.ClientPrefix, ChrRaces.ID, ChrRaces.Flags, ChrModel.Sex, CreatureModelData.FileDataID, ChrModel.CharComponentTextureLayoutID, "
                          "ChrRaces.MaleModelFallbackRaceID, ChrRaces.MaleModelFallbackSex, ChrRaces.MaleTextureFallbackRaceID, ChrRaces.MaleTextureFallbackSex, "
                          "ChrRaces.FemaleModelFallbackRaceID, ChrRaces.FemaleModelFallbackSex, ChrRaces.FemaleTextureFallbackRaceID, ChrRaces.FemaleTextureFallbackSex, "
                          "ChrRaceXChrModel.ChrModelID, ChrRaces.ClientFileString, ChrRaces.Name_lang "
                          "FROM ChrRaceXChrModel "
                          "LEFT JOIN ChrRaces ON ChrRaces.ID = ChrRaceXChrModel.ChrRacesID "
                          "LEFT JOIN ChrModel ON ChrModel.ID = ChrRaceXChrModel.ChrModelID "
                          "LEFT JOIN CreatureDisplayInfo ON CreatureDisplayInfo.ID = ChrModel.DisplayID "
                          "LEFT JOIN CreatureModelData ON CreatureModelData.ID = CreatureDisplayInfo.ModelID ");

  if (!races.valid || races.empty())
  {
    LOG_ERROR << "Unable to collect race information from game database";
    return;
  }

  for (auto& race : races.values)
  {
    RaceInfos infos;
    infos.prefix = race[0].toStdString();
    infos.raceID = race[1].toInt();
    infos.barefeet = (race[2].toInt() & 0x2);
    infos.sexID = race[3].toInt();
    auto modelfileid = race[4].toInt();
    infos.textureLayoutID = race[5].toInt();

    // Get fallback display race ID (this is mostly for allied races and others that rely on
    // item display info from other race models):
    if (infos.sexID == GENDER_MALE)
    {
      infos.modelFallbackRaceID = race[6].toInt();
      infos.modelFallbackSexID = race[7].toInt();
      infos.textureFallbackRaceID = race[8].toInt();
      infos.textureFallbackSexID = race[9].toInt();
    }
    else
    {
      infos.modelFallbackRaceID = race[10].toInt();
      infos.modelFallbackSexID = race[11].toInt();
      infos.textureFallbackRaceID = race[12].toInt();
      infos.textureFallbackSexID = race[13].toInt();
    }

    infos.ChrModelID.push_back(race[14].toInt());

    // lowercased race directory name (matches the character/<race>/<sex>/ path)
    if (race.size() > 15)
      infos.clientFileString = race[15].toLower().toStdString();

    // display name + playable/NPC classification (rule: Flags & 1 means
    // NPC-only, except race 23 (Pandaren) and 75 which are forced playable)
    if (race.size() > 16)
      infos.nameLang = race[16].toStdString();
    infos.isNPC = ((race[2].toInt() & 1) != 0) && infos.raceID != 23 && infos.raceID != 75;

    // modelfileid comes from a CreatureDisplayInfo -> CreatureModelData join. If those tables
    // didn't populate cleanly (e.g. a DB2 layout mismatch on an unexpected client build), the id
    // can be 0/invalid and getFile() returns null -- dereferencing it here crashed the whole app
    // on startup. Skip such races instead.
    GameFile * modelFile = GAMEDIRECTORY.getFile(modelfileid);
    if (!modelFile)
      continue;
    infos.isHD = modelFile->fullname().contains("_hd") ? true : false;

    if (RACES.find(modelfileid) == RACES.end())
    {
      RACES[modelfileid] = infos;
    }
    else // if a race is already inserted, capture any additional ChrModelID
    {
      auto id = race[14].toInt();
      if (std::find(RACES[modelfileid].ChrModelID.begin(), RACES[modelfileid].ChrModelID.end(), id) == RACES[modelfileid].ChrModelID.end())
        RACES[modelfileid].ChrModelID.push_back(id);
    }
  }

#if DEBUG_RACEINFOS > 0
  for (const auto & r : RACES)
  {
    LOG_INFO << "---------------------------";
    LOG_INFO << "modelfileid ->" << r.first;
    LOG_INFO << "infos.prefix =" << r.second.prefix.c_str();
    LOG_INFO << "infos.textureLayoutID =" << r.second.textureLayoutID;
    LOG_INFO << "infos.raceID =" << r.second.raceID;
    LOG_INFO << "infos.sexID =" << r.second.sexID;
    LOG_INFO << "infos.isHD =" << r.second.isHD;
    LOG_INFO << "infos.modelFallbackRaceID =" << r.second.modelFallbackRaceID;
    LOG_INFO << "infos.modelFallbackSexID =" << r.second.modelFallbackSexID;
    LOG_INFO << "infos.textureFallbackRaceID =" << r.second.textureFallbackRaceID;
    LOG_INFO << "infos.textureFallbackSexID =" << r.second.textureFallbackSexID;
    for(const auto & it : r.second.ChrModelID)
      LOG_INFO << "infos.ChrModelID ->" << it;
    LOG_INFO << "---------------------------";
  }
#endif
}

int RaceInfos::getHDModelForFileID(int fileid)
{
  auto result = fileid; // return same file id by default

  const auto it = RACES.find(fileid);
  if (it != RACES.end() && !it->second.isHD)
  {
    const auto raceID = it->second.raceID;
    const auto sexID = it->second.sexID;

    for (auto &r : RACES)
    {
      if (r.second.raceID == raceID && r.second.sexID == sexID && r.second.isHD)
      {
        result = r.first;
        break;
      }
    }
  }

  return result;
}

bool RaceInfos::getRaceInfosForFileID(int fileid, RaceInfos & infos)
{
  const auto raceInfosIt = RaceInfos::RACES.find(fileid);

  if (raceInfosIt != RaceInfos::RACES.end())
  {
    infos = raceInfosIt->second;
    return true;
  }

  return false;
}

bool RaceInfos::getRaceInfosForName(const std::string & raceName, int sex, RaceInfos & out)
{
  bool found = false;
  for (const auto & r : RACES)
  {
    if (r.second.sexID == sex && !r.second.clientFileString.empty() && r.second.clientFileString == raceName)
    {
      out = r.second;
      found = true;
      if (r.second.isHD) // prefer the HD model when several match
        break;
    }
  }
  return found;
}

int RaceInfos::getFileIDForRaceSex(const int & race, const int & sex)
{
  for (auto &r : RACES)
  {
    if (r.second.raceID == race && r.second.sexID == sex)
      return r.first;
  }

  return -1;
}

std::vector<RaceInfos::RaceMenuEntry> RaceInfos::getRaceMenu()
{
  // collapse the per-(race,sex,model) RACES map into one entry per race, keeping
  // the HD model FileDataID for each sex.
  std::map<int, RaceMenuEntry> byRace;
  for (const auto & kv : RACES)
  {
    const int fileID = kv.first;
    const RaceInfos & r = kv.second;
    if (r.raceID < 0)
      continue;

    RaceMenuEntry & e = byRace[r.raceID];
    e.raceID = r.raceID;
    e.isNPC = r.isNPC;
    if (e.name.empty())
      e.name = !r.nameLang.empty() ? r.nameLang : r.clientFileString;

    if (r.sexID == GENDER_FEMALE)
    {
      if (e.femaleFileID < 0 || r.isHD)
        e.femaleFileID = fileID;
    }
    else // treat anything else as male (covers GENDER_MALE / GENDER_NONE)
    {
      if (e.maleFileID < 0 || r.isHD)
        e.maleFileID = fileID;
    }
  }

  std::vector<RaceMenuEntry> out;
  out.reserve(byRace.size());
  for (auto & kv : byRace)
    out.push_back(kv.second);

  std::sort(out.begin(), out.end(),
            [](const RaceMenuEntry & a, const RaceMenuEntry & b) { return a.name < b.name; });

  return out;
}