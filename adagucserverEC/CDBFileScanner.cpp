/******************************************************************************
 * 
 * Project:  ADAGUC Server
 * Purpose:  ADAGUC OGC Server
 * Author:   Maarten Plieger, plieger "at" knmi.nl
 * Date:     2013-06-01
 *
 ******************************************************************************
 *
 * Copyright 2013, Royal Netherlands Meteorological Institute (KNMI)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 ******************************************************************************/

#include "CDBFileScanner.h"
#include "CDBFactory.h"
#include "CDebugger.h"
#include "adagucserver.h"
#include <set>
const char *CDBFileScanner::className="CDBFileScanner";
std::vector <CT::string> CDBFileScanner::tableNamesDone;
//#define CDBFILESCANNER_DEBUG
#define ISO8601TIME_LEN 32


bool CDBFileScanner::isTableAlreadyScanned(CT::string *tableName){
  for(size_t t=0;t<tableNamesDone.size();t++){
    if(tableNamesDone[t].equals(tableName->c_str())){
      return true;
    }
  }
  return false;
}
void CDBFileScanner::markTableDirty(CT::string *tableName){
  CDBDebug("Marking table dirty: %d %s",tableNamesDone.size(),tableName->c_str());
  for(size_t t=0;t<tableNamesDone.size();t++){
    if(tableNamesDone[t].equals(tableName->c_str())){
      tableNamesDone.erase (tableNamesDone.begin()+t);
      CDBDebug("Table marked dirty %d %s",tableNamesDone.size(),tableName->c_str());
      return;
    }
  }
}

/**
 * 
 * @return Positive on error, zero on succes, negative on skip.
 */
int CDBFileScanner::createDBUpdateTables(CDataSource *dataSource,int &removeNonExistingFiles,CDirReader *dirReader){
 ;
  int status = 0;
  CT::string query;
  
  
  CDBAdapter * dbAdapter = CDBFactory::getDBAdapter(dataSource->srvParams->cfg);
  
  
  
  if(dataSource->cfgLayer->Dimension.size()==0){
    if(CDataReader::autoConfigureDimensions(dataSource)!=0){
      CDBError("Unable to configure dimensions automatically");
      return 1;
    }
  }
  
  CDFObject *cdfObject = NULL;
  try{
    cdfObject = CDFObjectStore::getCDFObjectStore()->getCDFObject(dataSource,dirReader->fileList[0]->fullName.c_str());
  }catch(int e){
    CDBError("Unable to get CDFObject for file %s",dirReader->fileList[0]->fullName.c_str());
    return 1;
  }

  
  //Check and create all tables...
  for(size_t d=0;d<dataSource->cfgLayer->Dimension.size();d++){
  
    
    
    CDataReader::DimensionType dtype = CDataReader::getDimensionType(cdfObject,dataSource->cfgLayer->Dimension[d]->attr.name.c_str());
    if(dtype==CDataReader::dtype_none){
      CDBWarning("dtype_none for %s",dtype,dataSource->cfgLayer->Dimension[d]->attr.name.c_str());
    }

    
    bool isTimeDim = false;
    if(dtype == CDataReader::dtype_time || dtype == CDataReader::dtype_reference_time){
      isTimeDim = true;
    }
    
    CT::string dimName(dataSource->cfgLayer->Dimension[d]->attr.name.c_str());

    
    //Create database tableNames
    CT::string tableName;
    try{
      tableName = dbAdapter->getTableNameForPathFilterAndDimension(dataSource->cfgLayer->FilePath[0]->value.c_str(),dataSource->cfgLayer->FilePath[0]->attr.filter.c_str(), dimName.c_str(),dataSource);
    }catch(int e){
      CDBError("Unable to create tableName from '%s' '%s' '%s'",dataSource->cfgLayer->FilePath[0]->value.c_str(),dataSource->cfgLayer->FilePath[0]->attr.filter.c_str(), dimName.c_str());
      return 1;

    }

    
    int TABLETYPE_TIMESTAMP = 1;
    int TABLETYPE_INT       = 2;
    int TABLETYPE_REAL      = 3;
    int TABLETYPE_STRING    = 4;

    int tableType = 0;
    
    //Check whether we already did this table in this scan
    bool skip = isTableAlreadyScanned(&tableName);
    if(skip==false){
      CDBDebug("Updating dimension '%s' with table '%s'",dimName.c_str(),tableName.c_str());
      
      //Create column names
      

          
      if(isTimeDim==true){
        tableType = TABLETYPE_TIMESTAMP;
      }else{
        try{
        
          CDF::Variable *dimVar = cdfObject->getVariableNE(dimName.c_str());
          if(dimVar==NULL){
            CDBError("Dimension '%s' not found.",dimName.c_str());
            throw(__LINE__);
          }
          
          bool hasStatusFlag=false;
          std::vector<CDataSource::StatusFlag*> statusFlagList;
          CDataSource::readStatusFlags(dimVar,&statusFlagList);
          if(statusFlagList.size()>0)hasStatusFlag=true;
          for(size_t i=0;i<statusFlagList.size();i++)delete statusFlagList[i];
          statusFlagList.clear();
          if(hasStatusFlag){
            tableType = TABLETYPE_STRING;
            status = dbAdapter->createDimTableString(dimName.c_str(),tableName.c_str());
          }else{
            switch(dimVar->getType()){
              case CDF_FLOAT:
              case CDF_DOUBLE:
                tableType = TABLETYPE_REAL;break;
              case CDF_STRING:
                tableType = TABLETYPE_STRING;break;
              default:
                tableType = TABLETYPE_INT;break;
              
            }          
          }
        }catch(int e){
          CDBError("Exception defining table structure at line %d",e);
          return 1;
        }
        
      }
      if(tableType == 0){
        CDBError("Unknown tabletype");
        return 1;
      }
    
      if(tableType == TABLETYPE_TIMESTAMP)status = dbAdapter->createDimTableTimeStamp(dimName.c_str(),tableName.c_str());
      if(tableType == TABLETYPE_INT      )status = dbAdapter->createDimTableInt      (dimName.c_str(),tableName.c_str());
      if(tableType == TABLETYPE_REAL     )status = dbAdapter->createDimTableReal     (dimName.c_str(),tableName.c_str());
      if(tableType == TABLETYPE_STRING   )status = dbAdapter->createDimTableString   (dimName.c_str(),tableName.c_str());
      
      CDBDebug("Checking filetable %s",tableName.c_str());
      
      //if(status == 0){CDBDebug("OK: Table is available");}
      if(status == 1){
        CDBError("\nFAIL: Table %s could not be created",tableName.c_str()); 
        return 1;  }
      if(status == 2){
        removeNonExistingFiles=0;
        //removeExisting files can be set back to zero, because there are no files to remove (table is created)
        //note the int &removeNonExistingFiles as parameter of this function!
        //(Setting the value will have effect outside this function)
        //CDBDebug("OK: Table %s created, (check for unavailable files is off);",tableName);
        //if( addIndexToTable(DB,tableName.c_str(),dimName.c_str()) != 0)return 1;
      }
      //TODO set removeNonExistingFiles =0 when no records are in table
      
      
      if(removeNonExistingFiles==1){
        //The temporary table should always be dropped before filling.  
        //We will do a complete new update, so store everything in an new table
        //Later we will rename this table
        CT::string tableName_temp(&tableName);
        if(removeNonExistingFiles==1){
          tableName_temp.concat("_temp");
        }
        //CDBDebug("Making empty temporary table %s ... ",tableName_temp.c_str());
        //CDBDebug("Check table %s ...\t",tableName.c_str());

        if(status==0){
          //Table already exists....
          CDBError("*** WARNING: Temporary table %s already exists. Is another process updating the database? ***",tableName_temp.c_str());
          
          
          CDBDebug("*** DROPPING TEMPORARY TABLE: %s",query.c_str());
          if(dbAdapter->dropTable(tableName_temp.c_str())!=0){
            CDBError("Dropping table %s failed",tableName_temp.c_str());
            return 1;
          }

          CDBDebug("Check table %s ... ",tableName_temp.c_str());
          if(tableType == TABLETYPE_TIMESTAMP)status = dbAdapter->createDimTableTimeStamp(dimName.c_str(),tableName_temp.c_str());
          if(tableType == TABLETYPE_INT      )status = dbAdapter->createDimTableInt      (dimName.c_str(),tableName_temp.c_str());
          if(tableType == TABLETYPE_REAL     )status = dbAdapter->createDimTableReal     (dimName.c_str(),tableName_temp.c_str());
          if(tableType == TABLETYPE_STRING   )status = dbAdapter->createDimTableString   (dimName.c_str(),tableName_temp.c_str());

          if(status == 0){CDBDebug("OK: Table is available");}
          if(status == 1){CDBError("\nFAIL: Table %s could not be created",tableName_temp.c_str()); return 1;  }
          if(status == 2){CDBDebug("OK: Table %s created",tableName_temp.c_str());
            //Create a index on these files:
            //if(addIndexToTable(DB,tableName_temp.c_str(),dimName.c_str())!= 0)return 1;
          }
        }
        
        if(status == 0 || status == 1){CDBError("\nFAIL: Table %s could not be created",tableName_temp.c_str()); return 1;  }
        if(status == 2){
          //OK, Table did not exist, is created.
          //Create a index on these files:
          //if(addIndexToTable(DB,tableName_temp.c_str(),dimName.c_str()) != 0)return 1;
        }
      }
    }
      
  }

  return 0;
}

int CDBFileScanner::DBLoopFiles(CDataSource *dataSource,int removeNonExistingFiles,CDirReader *dirReader){
//  CDBDebug("DBLoopFiles");
  CT::string query;
  CDFObject *cdfObject = NULL;
  int status = 0;
  CT::string multiInsertCache;

  CDBAdapter * dbAdapter = CDBFactory::getDBAdapter(dataSource->srvParams->cfg);
  try{
    //Loop dimensions and files
    //CDBDebug("Checking files that are already in the database...");
    //char ISOTime[ISO8601TIME_LEN+1];
    CT::string isoString;
    size_t numberOfFilesAddedFromDB=0;
    
    //Setup variables like tableNames and timedims for each dimension
    size_t numDims=dataSource->cfgLayer->Dimension.size();
    
    bool isTimeDim[numDims];
    CT::string dimNames[numDims];
    //CT::string tableColumns[numDims];
    CT::string tableNames[numDims];
    //CT::string tableNames_temp[numDims];
    bool skipDim[numDims];
    CT::string queryString;
    //CT::string VALUES;
    //CADAGUC_time *ADTime  = NULL;
    CTime adagucTime;
    
     
    CDFObject *cdfObjectOfFirstFile = NULL;
    try{
      cdfObjectOfFirstFile = CDFObjectStore::getCDFObjectStore()->getCDFObject(dataSource,dirReader->fileList[0]->fullName.c_str());
    }catch(int e){
      CDBError("Unable to get CDFObject for file %s",dirReader->fileList[0]->fullName.c_str());
      return 1;
    }


    
    for(size_t d=0;d<dataSource->cfgLayer->Dimension.size();d++){
      
     
      dimNames[d].copy(dataSource->cfgLayer->Dimension[d]->attr.name.c_str());
     
      isTimeDim[d]=false;
      skipDim[d]=false;
      
      CDataReader::DimensionType dtype = CDataReader::getDimensionType(cdfObjectOfFirstFile,dimNames[d].c_str());
      
      if(dtype==CDataReader::dtype_none){
        CDBWarning("dtype_none for %s",dtype,dimNames[d].c_str());
      }
      dimNames[d].toLowerCaseSelf();
      if(dtype == CDataReader::dtype_time || dtype == CDataReader::dtype_reference_time){
        isTimeDim[d] = true;
      }
      CDBDebug("Found dimension %d with name %s of type %d, istimedim: [%d]",d,dimNames[d].c_str(),dtype,isTimeDim[d] );
      
      try{
        tableNames[d] = dbAdapter->getTableNameForPathFilterAndDimension(dataSource->cfgLayer->FilePath[0]->value.c_str(),dataSource->cfgLayer->FilePath[0]->attr.filter.c_str(), dimNames[d].c_str(),dataSource);
      }catch(int e){
        CDBError("Unable to create tableName from '%s' '%s' '%s'",dataSource->cfgLayer->FilePath[0]->value.c_str(),dataSource->cfgLayer->FilePath[0]->attr.filter.c_str(), dimNames[d].c_str());
        return 1;
      }
      
//       //Create temporary tableName
//       tableNames_temp[d].copy(&(tableNames[d]));
//       if(removeNonExistingFiles==1){
//         tableNames_temp[d].concat("_temp");
//       }
//       
      skipDim[d] = isTableAlreadyScanned(&tableNames[d]);
      if(skipDim[d]){
        CDBDebug("Skipping dimension '%s' with table '%s': already scanned.",dimNames[d].c_str(),tableNames[d].c_str());
      }
    }
    
    for(size_t j=0;j<dirReader->fileList.size();j++){
      //Loop through all configured dimensions.
      #ifdef CDBFILESCANNER_DEBUG
      CDBDebug("Loop through all configured dimensions.");
      #endif
      
      
      CT::string fileDate = CDirReader::getFileDate(dirReader->fileList[j]->fullName.c_str());

      
      CT::string dimensionTextList="none";
      if(dataSource->cfgLayer->Dimension.size()>0){
        dimensionTextList.print("(%s",dataSource->cfgLayer->Dimension[0]->attr.name.c_str());
        for(size_t d=1;d<dataSource->cfgLayer->Dimension.size();d++){
          dimensionTextList.printconcat(", %s",dataSource->cfgLayer->Dimension[d]->attr.name.c_str());
        }
        dimensionTextList.concat(")");
      }
      for(size_t d=0;d<dataSource->cfgLayer->Dimension.size();d++){
        multiInsertCache = "";
        if(skipDim[d] == false){
          
          numberOfFilesAddedFromDB=0;
          int fileExistsInDB=0;
          //If we are messing in the non-temporary table (e.g.removeNonExistingFiles==0)
          //we need to make a transaction to make sure a file is not added twice
          //If removeNonExistingFiles==1, we are using the temporary table
          //Which is already locked by a transaction 
          if(removeNonExistingFiles==0){
            #ifdef USEQUERYTRANSACTIONS                
            status = DB->query("BEGIN"); if(status!=0)throw(__LINE__);
            #endif          
          }
          
          //Delete files with non-matching creation date 
          dbAdapter->removeFilesWithChangedCreationDate(tableNames[d].c_str(),dirReader->fileList[j]->fullName.c_str(),fileDate.c_str());
          
          //Check if file is already there
          status = dbAdapter->checkIfFileIsInTable(tableNames[d].c_str(),dirReader->fileList[j]->fullName.c_str());
          if(status == 0){
            fileExistsInDB = 1;
          }
         
          CDBDebug("fileExistsInDB = %d",fileExistsInDB);
          
          //The file metadata does not already reside in the db.
          //Therefore we need to read information from it
          if(fileExistsInDB == 0){
            try{
              if(d==0){
                CDBDebug("Adding fileNo %d/%d %s\t %s",
                (int)j,
                (int)dirReader->fileList.size(),
                dimensionTextList.c_str(),
                dirReader->fileList[j]->baseName.c_str());
              };
              #ifdef CDBFILESCANNER_DEBUG
              CDBDebug("Creating new CDFObject");
              #endif
              cdfObject = CDFObjectStore::getCDFObjectStore()->getCDFObject(dataSource,dirReader->fileList[j]->fullName.c_str());
              if(cdfObject == NULL)throw(__LINE__);
              
              //Open the file
              #ifdef CDBFILESCANNER_DEBUG
              CDBDebug("Opening file %s",dirReader->fileList[j]->fullName.c_str());
              #endif
             
              
             
                #ifdef CDBFILESCANNER_DEBUG
                CDBDebug("Looking for %s",dataSource->cfgLayer->Dimension[d]->attr.name.c_str());
                #endif
                //Check for the configured dimensions or scalar variables
                //1 )Is this a scalar?
                CDF::Variable *  dimVar = cdfObject->getVariableNE(dataSource->cfgLayer->Dimension[d]->attr.name.c_str());
                CDF::Dimension * dimDim = cdfObject->getDimensionNE(dataSource->cfgLayer->Dimension[d]->attr.name.c_str());
                
                
                if(dimVar!=NULL&&dimDim==NULL){
                  //Check for scalar variable
                  if(dimVar->dimensionlinks.size() == 0){
                    CDBDebug("Found scalar variable %s with no dimension. Creating dim",dimVar->name.c_str());
                    dimDim = new CDF::Dimension();
                    dimDim->name = dimVar->name;
                    dimDim->setSize(1);
                    cdfObject->addDimension(dimDim);
                    dimVar->dimensionlinks.push_back(dimDim);
                  }
                  //Check if this variable has another dim attached
                  if(dimVar->dimensionlinks.size() == 1){
                    dimDim = dimVar->dimensionlinks[0];
                    CDBDebug("Using dimension %s for dimension variable %s",dimVar->dimensionlinks[0]->name.c_str(),dimVar->name.c_str());
                  }
                }
                
                
                
                
                if(dimDim==NULL||dimVar==NULL){
                  CDBError("In file %s",dirReader->fileList[j]->fullName.c_str());
                  if(dimVar == NULL){
                    CDBError("Variable '%s' for dimension '%s' not found",dataSource->cfgLayer->Variable[0]->value.c_str(),dataSource->cfgLayer->Dimension[d]->attr.name.c_str());
                  }
                  if(dimDim == NULL){
                    CDBError("For variable '%s' dimension '%s' not found",dataSource->cfgLayer->Variable[0]->value.c_str(),dataSource->cfgLayer->Dimension[d]->attr.name.c_str());
                  }
                  
                  throw(__LINE__);
                }else{
                  CDF::Attribute *dimUnits = dimVar->getAttributeNE("units");
                  if(dimUnits==NULL){
                    if(isTimeDim[d]){
                      CDBError("No time units found for variable %s",dimVar->name.c_str());
                      throw(__LINE__);
                    }
                    dimVar->setAttributeText("units","1");
                    dimUnits = dimVar->getAttributeNE("units");
                  }
                  
                
                  //Create adaguctime structure, when this is a time dimension.
                  if(isTimeDim[d]){
                    try{
                      adagucTime.reset();
                      adagucTime.init((char*)dimUnits->toString().c_str());
                    }catch(int e){
                      CDBDebug("Exception occurred during time initialization: %d",e);
                      throw(__LINE__);
                    }
                  }
                  
                  #ifdef CDBFILESCANNER_DEBUG
                  CDBDebug("Dimension type = %s",CDF::getCDFDataTypeName(dimVar->getType()).c_str());
                  #endif
                  
                  #ifdef CDBFILESCANNER_DEBUG
                  CDBDebug("Reading dimension %s of length %d",dimVar->name.c_str(),dimDim->getSize());
                  #endif
                  
                  //Strings do never fit in a double.
                  if(dimVar->getType()!=CDF_STRING){
                    //Read the dimension data
                    status = dimVar->readData(CDF_DOUBLE);
                  }else{
                    //Read the dimension data
                    status = dimVar->readData(CDF_STRING);
                  }
                  CDBDebug("/Reading dimension %s of length %d",dimVar->name.c_str(),dimDim->getSize());
                  
                  if(status!=0){
                    CDBError("Unable to read variable data for %s",dimVar->name.c_str());
                    throw(__LINE__);
                  }
                  
                  //Check for status flag dimensions
                  bool hasStatusFlag = false;
                  std::vector<CDataSource::StatusFlag*> statusFlagList;
                  CDataSource::readStatusFlags(dimVar,&statusFlagList);
                  if(statusFlagList.size()>0)hasStatusFlag=true;
                  
                  int exceptionAtLineNr=0;
                  
       
                  
                  try{
                    const double *dimValues=(double*)dimVar->data;
                    
                    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                    //Start looping over every netcdf dimension element
                    std::set<std::string> uniqueDimensionValueSet;
                    std::pair<std::set<std::string>::iterator,bool> uniqueDimensionValueRet;
                    
                    CT::string uniqueKey;
                    for(size_t i=0;i<dimDim->length;i++){
                      
                      
                      CT::string uniqueKey = "";
                      //Insert individual values of type char, short, int, float, double
                      if(dimVar->getType()!=CDF_STRING){
                        if(dimValues[i]!=NC_FILL_DOUBLE){
                          if(isTimeDim[d]==false){
                            if(hasStatusFlag==true){
                              uniqueKey.print("%s",CDataSource::getFlagMeaning( &statusFlagList,double(dimValues[i])));
                              uniqueDimensionValueRet = uniqueDimensionValueSet.insert(uniqueKey.c_str());
                              if(uniqueDimensionValueRet.second == true){
                                dbAdapter->setFileString(tableNames[d].c_str(),dirReader->fileList[j]->fullName.c_str(),uniqueKey.c_str(),int(i),fileDate.c_str()) ;
                              }
                            }
                            if(hasStatusFlag==false){
                              switch(dimVar->getType()){
                                case CDF_FLOAT:
                                case CDF_DOUBLE:
                                  uniqueKey.print("%f",double(dimValues[i]));
                                  uniqueDimensionValueRet = uniqueDimensionValueSet.insert(uniqueKey.c_str());
                                  if(uniqueDimensionValueRet.second == true){
                                    dbAdapter->setFileReal(tableNames[d].c_str(),dirReader->fileList[j]->fullName.c_str(),double(dimValues[i]),int(i),fileDate.c_str());
                                  }
                                  break;
                                default:
                                  uniqueKey.print("%d",int(dimValues[i]));
                                  uniqueDimensionValueRet = uniqueDimensionValueSet.insert(uniqueKey.c_str());
                                  if(uniqueDimensionValueRet.second == true){
                                    dbAdapter->setFileInt(tableNames[d].c_str(),dirReader->fileList[j]->fullName.c_str(),int(dimValues[i]),int(i),fileDate.c_str()) ;
                                  }
                                  break;
                              }
                            }
                          }else{
                            
                            //ADTime->PrintISOTime(ISOTime,ISO8601TIME_LEN,dimValues[i]);status = 0;//TODO make PrintISOTime return a 0 if succeeded
                            
                            try{
                              uniqueKey = adagucTime.dateToISOString(adagucTime.getDate(dimValues[i]));
                              uniqueKey.setSize(19);
                              uniqueKey.concat("Z");
                              dbAdapter->setFileTimeStamp(tableNames[d].c_str(),dirReader->fileList[j]->fullName.c_str(),uniqueKey.c_str(),int(i),fileDate.c_str()) ;
                              
                         
                            }catch(int e){
                              CDBDebug("Exception occurred during time conversion: %d",e);
                            }
                            
                          }
                        }
                      }
                      
                      if(dimVar->getType()==CDF_STRING){
                        const char *str=((char**)dimVar->data)[i];
                        uniqueKey.print("%s",str);
                        uniqueDimensionValueRet = uniqueDimensionValueSet.insert(uniqueKey.c_str());
                        if(uniqueDimensionValueRet.second == true){
                          dbAdapter->setFileString(tableNames[d].c_str(),dirReader->fileList[j]->fullName.c_str(),uniqueKey.c_str(),int(i),fileDate.c_str()) ;
                        }
                      }
                      
                      
                      
                      //Check if this insert is unique
                      if(uniqueKey.length()>0){
                        uniqueDimensionValueRet = uniqueDimensionValueSet.insert(uniqueKey.c_str());
                        if(uniqueDimensionValueRet.second == false){
                          CDBError("Dimension value [%s] not unique in dimension [%s]",dirReader->fileList[j]->fullName.c_str(),dimVar->name.c_str());
                        }
                      }
                    }
                  }catch(int linenr){
                    CDBError("Exception at linenr %d",linenr);
                    exceptionAtLineNr=linenr;
                  }
                  //Cleanup statusflags
                  for(size_t i=0;i<statusFlagList.size();i++)delete statusFlagList[i];
                  statusFlagList.clear();
                  
                  //Cleanup adaguctime structure
                  
                  
                  if(exceptionAtLineNr!=0)throw(exceptionAtLineNr);
                }
              
              //delete cdfObject;cdfObject=NULL;
              //cdfObject=CDFObjectStore::getCDFObjectStore()->deleteCDFObject(&cdfObject);
            }catch(int linenr){
              CDBError("Exception in DBLoopFiles at line %d");
              CDBError(" *** SKIPPING FILE %s ***",dirReader->fileList[j]->baseName.c_str());
              //Close cdfObject. this is only needed if an exception occurs, otherwise it does nothing...
              //delete cdfObject;cdfObject=NULL;

              //TODO CHECK cdfObject=CDFObjectStore::getCDFObjectStore()->deleteCDFObject(&cdfObject);
            }
          }
        }
        
        
        //End of dimloop, start inserting our collected records in one statement
        CDBDebug("Adding files to database");
        dbAdapter->addFilesToDataBase();
      }
    }
    
    
    

    if(numberOfFilesAddedFromDB!=0){CDBDebug("%d file(s) were already in the database",numberOfFilesAddedFromDB);}
    

    
    
  }
  catch(int linenr){
    #ifdef USEQUERYTRANSACTIONS                    
    DB->query("COMMIT"); 
    #endif    
    CDBError("Exception in DBLoopFiles at line %d",linenr);
    
    //TODO CHECK    cdfObject=CDFObjectStore::getCDFObjectStore()->deleteCDFObject(&cdfObject);
    return 1;
  }
  
  
  //delete cdfObject;cdfObject=NULL;
  //cdfObject=CDFObjectStore::getCDFObjectStore()->deleteCDFObject(&cdfObject);
  return 0;
}



int CDBFileScanner::updatedb(const char *pszDBParams, CDataSource *dataSource,CT::string *_tailPath,CT::string *_layerPathToScan){
 
  if(dataSource->dLayerType!=CConfigReaderLayerTypeDataBase)return 0;
 
  CCache::Lock lock;
  CT::string identifier = "updatedb";  identifier.concat(dataSource->cfgLayer->FilePath[0]->value.c_str());  identifier.concat("/");  identifier.concat(dataSource->cfgLayer->FilePath[0]->attr.filter.c_str());  
  //CT::string cacheDirectory = "";
  CT::string cacheDirectory = dataSource->srvParams->cfg->TempDir[0]->attr.value.c_str();
  //dataSource->srvParams->getCacheDirectory(&cacheDirectory);
  if(cacheDirectory.length()>0){
    lock.claim(cacheDirectory.c_str(),identifier.c_str(),"updatedb",dataSource->srvParams->isAutoResourceEnabled());
  }

  //We only need to update the provided path in layerPathToScan. We will simply ignore the other directories
  if(_layerPathToScan!=NULL){
    if(_layerPathToScan->length()!=0){
      CT::string layerPath,layerPathToScan;
      layerPath.copy(dataSource->cfgLayer->FilePath[0]->value.c_str());
      layerPathToScan.copy(_layerPathToScan);
      
      CDirReader::makeCleanPath(&layerPath);
      CDirReader::makeCleanPath(&layerPathToScan);
      
      //If this is another directory we will simply ignore it.
      if(layerPath.equals(&layerPathToScan)==false){
        //CDBDebug ("Skipping %s==%s\n",layerPath.c_str(),layerPathToScan.c_str());
        return 0;
      }
    }
  }
  // This variable enables the query to remove files that no longer exist in the filesystem
  int removeNonExistingFiles=1;
  
  int status;
 
  
  //Copy tailpath (can be provided to scan only certain subdirs)
  CT::string tailPath(_tailPath);
  
  CDirReader::makeCleanPath(&tailPath);
  
  //if tailpath is defined than removeNonExistingFiles must be zero
  if(tailPath.length()>0)removeNonExistingFiles=0;
  
  
  //temp = new CDirReader();
  //temp->makeCleanPath(&FilePath);
  //delete temp;
  
  CDirReader dirReader;
  
  CDBDebug("*** Starting update layer '%s' ***",dataSource->cfgLayer->Name[0]->value.c_str());
  
  if(searchFileNames(&dirReader,dataSource->cfgLayer->FilePath[0]->value.c_str(),dataSource->cfgLayer->FilePath[0]->attr.filter.c_str(),tailPath.c_str())!=0)return 0;
  
  if(dirReader.fileList.size()==0){
    CDBWarning("No files found for layer %s",dataSource->cfgLayer->Name[0]->value.c_str());
    return 0;
  }
  

  try{ 
    
//     status = DB->query("SET client_min_messages TO WARNING");
//     if(status != 0 )throw(__LINE__);
    
    //First check and create all tables... returns zero on success, positive on error, negative on already done.
    status = createDBUpdateTables(dataSource,removeNonExistingFiles,&dirReader);
    if(status > 0 ){

      throw(__LINE__);
    }
    
    if(status == 0){
            
  
      
      
      //Loop Through all files
      status = DBLoopFiles(dataSource,removeNonExistingFiles,&dirReader);
      if(status != 0 )throw(__LINE__);
              
//       //In case of a complete update, the data is written in a temporary table
//       //Rename the table to the correct one (remove _temp)
//       for(size_t d=0;d<dataSource->cfgLayer->Dimension.size();d++){
//         CT::string dimName(dataSource->cfgLayer->Dimension[d]->attr.name.c_str());
// 
//         CT::string tableName;
//         try{
//           tableName = dbAdapter->getTableNameForPathFilterAndDimension(dataSource->cfgLayer->FilePath[0]->value.c_str(),dataSource->cfgLayer->FilePath[0]->attr.filter.c_str(), dimName.c_str(),dataSource);
//         }catch(int e){
//           CDBError("Unable to create tableName from '%s' '%s' '%s'",dataSource->cfgLayer->FilePath[0]->value.c_str(),dataSource->cfgLayer->FilePath[0]->attr.filter.c_str(), dimName.c_str());
//           return 1;
//         }
//           
//         bool skip = isTableAlreadyScanned(&tableName);
//         //bool skip = false;
//         if(skip == false){
//           //Remember that we have completed this scan
//           tableNamesDone.push_back(CT::string(tableName.c_str()));
//           
//           if(removeNonExistingFiles==1){
//             CDBDebug("Renaming temporary table '%s_temp' to '%s'",tableName.c_str(),tableName.c_str());
//             CT::string query;
//             //Drop old table
//             query.print("drop table %s",tableName.c_str());
//             if(DB->query(query.c_str())!=0){CDBError("Query %s failed",query.c_str());throw(__LINE__);}
//             //Rename new table to old table name
//             query.print("alter table %s_temp rename to %s",tableName.c_str(),tableName.c_str());
//             if(DB->query(query.c_str())!=0){CDBError("Query %s failed",query.c_str());throw(__LINE__);}
//             if(status!=0){throw(__LINE__);}
//           }
//         }
//       }
    }
    
  }
  catch(int linenr){
    CDBError("Exception in updatedb at line %d",linenr);
    #ifdef USEQUERYTRANSACTIONS                    
    if(removeNonExistingFiles==1)status = DB->query("COMMIT");
           #endif    
    return 1;
  }
  
  
 
  // Close DB
  //CDBDebug("COMMIT");
  #ifdef USEQUERYTRANSACTIONS                  
  if(removeNonExistingFiles==1)status = DB->query("COMMIT");
           #endif  
  

  CDBDebug("*** Finished update layer '%s' ***\n",dataSource->cfgLayer->Name[0]->value.c_str());
  lock.release();
  return 0;
}


int CDBFileScanner::searchFileNames(CDirReader *dirReader,const char * path,CT::string expr,const char *tailPath){
  if(path==NULL){
    CDBError("No path defined");
    return 1;
  }
  CT::string filePath=path;//dataSource->cfgLayer->FilePath[0]->value.c_str();
  if(tailPath!=NULL)filePath.concat(tailPath);
  if(filePath.lastIndexOf(".nc")==int(filePath.length()-3)||filePath.lastIndexOf(".h5")==int(filePath.length()-3)||filePath.indexOf("http://")==0||filePath.indexOf("https://")==0||filePath.indexOf("dodsc://")==0){
    //Add single file or opendap URL.
    CFileObject * fileObject = new CFileObject();
    fileObject->fullName.copy(&filePath);
    fileObject->baseName.copy(&filePath);
    fileObject->isDir=false;
    dirReader->fileList.push_back(fileObject);
  }else{
    //Read directory
    dirReader->makeCleanPath(&filePath);
    try{
      CT::string fileFilterExpr(".*\\.nc$");
      if(expr.empty()==false){//dataSource->cfgLayer->FilePath[0]->attr.filter.c_str()
        fileFilterExpr.copy(&expr);
      }
      //CDBDebug("Reading directory %s with filter %s",filePath.c_str(),fileFilterExpr.c_str()); 
      dirReader->listDirRecursive(filePath.c_str(),fileFilterExpr.c_str());
      
      //Delete all files that start with a "." from the filelist.
      for(size_t j=0;j<dirReader->fileList.size();j++){
        if(dirReader->fileList[j]->baseName.c_str()[0]=='.'){
          delete dirReader->fileList[j];
          dirReader->fileList.erase(dirReader->fileList.begin()+j);
          j--;
        }
      }
      
    }catch(const char *msg){
      CDBDebug("Directory %s does not exist, silently skipping...",filePath.c_str());
      return 1;
    }
  }
  //CDBDebug("%s",dirReader->fileList[0]->fullName.c_str());
  #ifdef CDBFILESCANNER_DEBUG
  CDBDebug("Found %d file(s)",int(dirReader->fileList.size()));
  #endif
  return 0;
}

