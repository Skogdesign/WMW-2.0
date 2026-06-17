/*
 * WoWDatabase.h
 *
 *  Created on: 9 nov. 2014
 *      Author: Jerome
 */

#ifndef _WOWDATABASE_H_
#define _WOWDATABASE_H_

#include "GameDatabase.h"

class DBFile;
class GameFile;

class QDomElement;


#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _WOWDATABASE_API_ __declspec(dllexport)
#    else
#        define _WOWDATABASE_API_ __declspec(dllimport)
#    endif
#else
#    define _WOWDATABASE_API_
#endif

namespace wow
{
  class TableStructure : public core::TableStructure
  {
  public:
    TableStructure() :
      core::TableStructure(), hash(0)
    {
    }

    unsigned int hash;

    DBFile * createDBFile();

  };

  class FieldStructure : public core::FieldStructure
  {
  public:
    FieldStructure() :
      core::FieldStructure(), pos(-1), isCommonData(false), isRelationshipData(false)
    {
    }

    int pos;
    bool isCommonData;
    bool isRelationshipData;
  };

  class _WOWDATABASE_API_ WoWDatabase : public core::GameDatabase
  {
    public:
      WoWDatabase();
      WoWDatabase(WoWDatabase &);

      ~WoWDatabase() {}

      core::TableStructure * createTableStructure();
      core::FieldStructure * createFieldStructure();

      void readSpecificTableAttributes(QDomElement &, core::TableStructure *);
      void readSpecificFieldAttributes(QDomElement &, core::FieldStructure *);

    protected:
      // Refresh each table's DB2 field positions from its WoWDBDefs (.dbd)
      // definition for the loaded build, so the curated XML names/types stay
      // but positions track the current game version.
      void refreshStructures(std::vector<core::TableStructure *> &) override;

      // Secondary indexes on the hot foreign-key / join columns the per-load
      // customization/equipment/creature queries filter on (database.xml declares
      // only PRIMARY KEYs, so these were full table scans of 30k-220k-row tables).
      void createIndices() override;
  };

}

#endif /* _WOWDATABASE_H_ */
