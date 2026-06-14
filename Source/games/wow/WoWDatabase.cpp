/*
 * WoWDatabase.cpp
 *
 *  Created on: 9 nov. 2014
 *      Author: Jerome
 */

#include "WoWDatabase.h"

#include <QDomNamedNodeMap>
#include <QFile>

#include "Game.h"
#include "logger/Logger.h"
#include "dbdfile.h"
#include "wdb2file.h"
#include "wdb5file.h"
#include "wdb6file.h"
#include "wdc1file.h"
#include "wdc2file.h"
#include "wdc3file.h"

const std::vector<QString> POSSIBLE_DB_EXT = {".db2", ".dbc"};

wow::WoWDatabase::WoWDatabase()
  : GameDatabase()
{

}

core::TableStructure *  wow::WoWDatabase::createTableStructure()
{
  return new wow::TableStructure;
}

core::FieldStructure *  wow::WoWDatabase::createFieldStructure()
{
  return new wow::FieldStructure;
}

void wow::WoWDatabase::readSpecificTableAttributes(QDomElement & e, core::TableStructure * tblStruct)
{
  wow::TableStructure * tbl = dynamic_cast<wow::TableStructure *>(tblStruct);

  if (!tbl)
    return;

  QDomNamedNodeMap attributes = e.attributes();
  QDomNode hash = attributes.namedItem("layoutHash");

  if (!hash.isNull())
    tbl->hash = hash.nodeValue().toUInt();
}

void wow::WoWDatabase::readSpecificFieldAttributes(QDomElement & e, core::FieldStructure * fieldStruct)
{
  wow::FieldStructure * field = dynamic_cast<wow::FieldStructure *>(fieldStruct);

  if (!field)
    return;

  QDomNamedNodeMap attributes = e.attributes();

  QDomNode pos = attributes.namedItem("pos");
  QDomNode commonData = attributes.namedItem("commonData");
  QDomNode relationshipData = attributes.namedItem("relationshipData");

  if (!pos.isNull())
    field->pos = pos.nodeValue().toInt();

  if (!commonData.isNull())
    field->isCommonData = true;

  if (!relationshipData.isNull())
    field->isRelationshipData = true;
}

void wow::WoWDatabase::refreshStructures(std::vector<core::TableStructure *> & tables)
{
  // The shipped database.xml carries WMV's curated column NAMES/types (which the
  // hardcoded SQL queries depend on) but version-specific field POSITIONS. For a
  // build newer than the shipped schema, refresh each field's DB2 field index
  // from its WoWDBDefs (.dbd) definition, matching fields by name. Tables/fields
  // without a matching .dbd entry simply keep their base positions.
  const QString build = GAMEDIRECTORY.version();
  if (build.isEmpty())
    return;

  LOG_INFO << "Refreshing database structures from WoWDBDefs for build" << build;

  for (core::TableStructure * t : tables)
  {
    wow::TableStructure * tbl = dynamic_cast<wow::TableStructure *>(t);
    if (!tbl)
      continue;

    const QString dbdPath = "dbd/" + tbl->name + ".dbd";
    if (!QFile::exists(dbdPath))
      continue; // no definition available -> keep base XML positions

    wow::DBDFile dbd;
    if (!dbd.parseFile(dbdPath))
    {
      LOG_WARNING << "DBD: failed to parse" << dbdPath;
      continue;
    }

    // Match by build (layout hash matching would require pre-opening the DB2).
    const wow::DBDDefinition * def = dbd.getStructure(build, 0);
    if (!def)
    {
      LOG_WARNING << "DBD: no definition covering build" << build << "for table" << tbl->name << "- keeping base positions";
      continue;
    }

    // Map column name -> DB2 field index. Non-inline id/relation fields live in
    // the id-list / relationship section and do NOT occupy a record field slot.
    QMap<QString, int> nameToPos;
    int idx = 0;
    for (const wow::DBDField & f : def->fields)
    {
      if ((f.isID || f.isRelation) && !f.isInline)
        continue;
      nameToPos[f.name.toLower()] = idx;
      idx++;
    }

    int refreshed = 0, missing = 0;
    for (core::FieldStructure * cf : tbl->fields)
    {
      wow::FieldStructure * field = dynamic_cast<wow::FieldStructure *>(cf);
      if (!field || field->isKey || field->isRelationshipData)
        continue; // id / relationship fields are not read by position

      auto it = nameToPos.find(field->name.toLower());
      if (it != nameToPos.end())
      {
        field->pos = it.value();
        refreshed++;
      }
      else
      {
        missing++;
        LOG_WARNING << "DBD:" << tbl->name << "field" << field->name << "absent in build" << build << "- keeping pos" << field->pos;
      }
    }
    LOG_INFO << "DBD refreshed" << tbl->name << "-" << refreshed << "fields updated," << missing << "unmatched";
  }
}

DBFile * wow::TableStructure::createDBFile()
{
  DBFile * result = core::TableStructure::createDBFile();

  if (result != 0)
    return result;

  GameFile * fileToOpen = 0;
  // loop over possible extension to check if file exists
  for (unsigned int i = 0; i < POSSIBLE_DB_EXT.size(); i++)
  {
    fileToOpen = GAMEDIRECTORY.getFile("DBFilesClient\\" + file + POSSIBLE_DB_EXT[i]);
    if (fileToOpen)
      break;
  }

  if (!fileToOpen)
    return 0;

  
  if (fileToOpen)
  {
    if (fileToOpen->open(false))
    {
      char header[5];

      fileToOpen->read(header, 4);

      if (strncmp(header, "WDB2", 4) == 0)
        result = new WDB2File(fileToOpen->fullname());
      else if (strncmp(header, "WDB5", 4) == 0)
        result = new WDB5File(fileToOpen->fullname());
      else if (strncmp(header, "WDB6", 4) == 0)
        result = new WDB6File(fileToOpen->fullname());
      else if (strncmp(header, "WDC1", 4) == 0)
        result = new WDC1File(fileToOpen->fullname());
      else if (strncmp(header, "WDC2", 4) == 0)
        result = new WDC2File(fileToOpen->fullname());
      else if (strncmp(header, "WDC3", 4) == 0)
        result = new WDC3File(fileToOpen->fullname());
      else if (strncmp(header, "WDC4", 4) == 0)
        result = new WDC3File(fileToOpen->fullname());
      else if (strncmp(header, "WDC5", 4) == 0)
        result = new WDC3File(fileToOpen->fullname());
      else
        LOG_ERROR << "Unsupported database file" << header[0] << header[1] << header[2] << header[3];

      fileToOpen->close();
    }
  }

  return result;
}
