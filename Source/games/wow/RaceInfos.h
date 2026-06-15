#ifndef _RACEINFOS_H_
#define _RACEINFOS_H_

#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _RACEINFOS_API_ __declspec(dllexport)
#    else
#        define _RACEINFOS_API_ __declspec(dllimport)
#    endif
#else
#    define _RACEINFOS_API_
#endif

class WoWModel;

class _RACEINFOS_API_ RaceInfos
{
  public:
    int raceID = -1; // -1 means invalid race (default value)
    int sexID; // 0 male / 1 female
    int textureLayoutID;
    bool isHD;
    bool barefeet;
    std::string prefix;
    std::string clientFileString; // lowercased ChrRaces.ClientFileString, e.g. "bloodelf"
    std::string nameLang;         // ChrRaces.Name_lang display name, e.g. "Blood Elf"
    bool isNPC = false;           // ChrRaces.Flags & 1 (except races 23/75) -> NPC-only race
    int modelFallbackRaceID;
    int modelFallbackSexID;
    int textureFallbackRaceID;
    int textureFallbackSexID;
    std::vector<int> ChrModelID;

    // One row per race for the UI race browser (Playable vs NPC), built from the
    // ChrRaces data; maps each race to its male/female model FileDataID.
    struct RaceMenuEntry
    {
      int raceID = -1;
      std::string name;       // display name (Name_lang, falls back to clientFileString)
      bool isNPC = false;
      int maleFileID = -1;    // model FileDataID, -1 if none
      int femaleFileID = -1;
    };
    static std::vector<RaceMenuEntry> getRaceMenu(); // sorted by display name

    static void init();
    static int getHDModelForFileID(int);
    static bool getRaceInfosForFileID(int, RaceInfos &);
    // Resolve by race directory name (lowercased ClientFileString) + sex, used as a
    // fallback when a character model file isn't the canonical race model in the map.
    static bool getRaceInfosForName(const std::string & raceName, int sex, RaceInfos &);
    static int getFileIDForRaceSex(const int & race, const int & sex);

  private:
    static std::map<int, RaceInfos> RACES;
};




#endif /* _RACEINFOS_H_ */
