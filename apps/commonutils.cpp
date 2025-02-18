/******************************************************************************
 *
 * Project:  GDAL Utilities
 * Purpose:  Common utility routines
 * Author:   Even Rouault, <even dot rouault at spatialys.com>
 *
 ******************************************************************************
 * Copyright (c) 2011-2012, Even Rouault <even dot rouault at spatialys.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "commonutils.h"

#include <cstdio>
#include <cstring>

#include <string>

#include "cpl_conv.h"
#include "cpl_string.h"
#include "gdal.h"

/* -------------------------------------------------------------------- */
/*                   DoesDriverHandleExtension()                        */
/* -------------------------------------------------------------------- */

static bool DoesDriverHandleExtension(GDALDriverH hDriver, const char *pszExt)
{
    bool bRet = false;
    const char *pszDriverExtensions =
        GDALGetMetadataItem(hDriver, GDAL_DMD_EXTENSIONS, nullptr);
    if (pszDriverExtensions)
    {
        char **papszTokens = CSLTokenizeString(pszDriverExtensions);
        for (int j = 0; papszTokens[j]; j++)
        {
            if (EQUAL(pszExt, papszTokens[j]))
            {
                bRet = true;
                break;
            }
        }
        CSLDestroy(papszTokens);
    }
    return bRet;
}

/* -------------------------------------------------------------------- */
/*                         GetOutputDriversFor()                        */
/* -------------------------------------------------------------------- */

std::vector<CPLString> GetOutputDriversFor(const char *pszDestFilename,
                                           int nFlagRasterVector)
{
    std::vector<CPLString> aoDriverList;

    CPLString osExt = CPLGetExtension(pszDestFilename);
    if (EQUAL(osExt, "zip") &&
        (CPLString(pszDestFilename).endsWith(".shp.zip") ||
         CPLString(pszDestFilename).endsWith(".SHP.ZIP")))
    {
        osExt = "shp.zip";
    }
    else if (EQUAL(osExt, "zip") &&
             (CPLString(pszDestFilename).endsWith(".gpkg.zip") ||
              CPLString(pszDestFilename).endsWith(".GPKG.ZIP")))
    {
        osExt = "gpkg.zip";
    }
    const int nDriverCount = GDALGetDriverCount();
    for (int i = 0; i < nDriverCount; i++)
    {
        GDALDriverH hDriver = GDALGetDriver(i);
        bool bOk = false;
        if ((GDALGetMetadataItem(hDriver, GDAL_DCAP_CREATE, nullptr) !=
                 nullptr ||
             GDALGetMetadataItem(hDriver, GDAL_DCAP_CREATECOPY, nullptr) !=
                 nullptr) &&
            (((nFlagRasterVector & GDAL_OF_RASTER) &&
              GDALGetMetadataItem(hDriver, GDAL_DCAP_RASTER, nullptr) !=
                  nullptr) ||
             ((nFlagRasterVector & GDAL_OF_VECTOR) &&
              GDALGetMetadataItem(hDriver, GDAL_DCAP_VECTOR, nullptr) !=
                  nullptr)))
        {
            bOk = true;
        }
        else if (GDALGetMetadataItem(hDriver, GDAL_DCAP_VECTOR_TRANSLATE_FROM,
                                     nullptr) &&
                 (nFlagRasterVector & GDAL_OF_VECTOR) != 0)
        {
            bOk = true;
        }
        if (bOk)
        {
            if (!osExt.empty() && DoesDriverHandleExtension(hDriver, osExt))
            {
                aoDriverList.push_back(GDALGetDriverShortName(hDriver));
            }
            else
            {
                const char *pszPrefix = GDALGetMetadataItem(
                    hDriver, GDAL_DMD_CONNECTION_PREFIX, nullptr);
                if (pszPrefix && STARTS_WITH_CI(pszDestFilename, pszPrefix))
                {
                    aoDriverList.push_back(GDALGetDriverShortName(hDriver));
                }
            }
        }
    }

    // GMT is registered before netCDF for opening reasons, but we want
    // netCDF to be used by default for output.
    if (EQUAL(osExt, "nc") && aoDriverList.size() == 2 &&
        EQUAL(aoDriverList[0], "GMT") && EQUAL(aoDriverList[1], "NETCDF"))
    {
        aoDriverList.clear();
        aoDriverList.push_back("NETCDF");
        aoDriverList.push_back("GMT");
    }

    return aoDriverList;
}

/* -------------------------------------------------------------------- */
/*                      GetOutputDriverForRaster()                      */
/* -------------------------------------------------------------------- */

CPLString GetOutputDriverForRaster(const char *pszDestFilename)
{
    CPLString osFormat;
    std::vector<CPLString> aoDrivers =
        GetOutputDriversFor(pszDestFilename, GDAL_OF_RASTER);
    CPLString osExt(CPLGetExtension(pszDestFilename));
    if (aoDrivers.empty())
    {
        if (osExt.empty())
        {
            osFormat = "GTiff";
        }
        else
        {
            CPLError(CE_Failure, CPLE_AppDefined, "Cannot guess driver for %s",
                     pszDestFilename);
            return "";
        }
    }
    else
    {
        if (aoDrivers.size() > 1 &&
            !(aoDrivers[0] == "GTiff" && aoDrivers[1] == "COG"))
        {
            CPLError(CE_Warning, CPLE_AppDefined,
                     "Several drivers matching %s extension. Using %s",
                     osExt.c_str(), aoDrivers[0].c_str());
        }
        osFormat = aoDrivers[0];
    }
    CPLDebug("GDAL", "Using %s driver", osFormat.c_str());
    return osFormat;
}

/* -------------------------------------------------------------------- */
/*                        EarlySetConfigOptions()                       */
/* -------------------------------------------------------------------- */

void EarlySetConfigOptions(int argc, char **argv)
{
    // Must process some config options before GDALAllRegister() or
    // OGRRegisterAll(), but we can't call GDALGeneralCmdLineProcessor() or
    // OGRGeneralCmdLineProcessor(), because it needs the drivers to be
    // registered for the --format or --formats options.
    for (int i = 1; i < argc; i++)
    {
        if (EQUAL(argv[i], "--config") && i + 2 < argc)
        {
            CPLSetConfigOption(argv[i + 1], argv[i + 2]);

            i += 2;
        }
        else if (EQUAL(argv[i], "--debug") && i + 1 < argc)
        {
            CPLSetConfigOption("CPL_DEBUG", argv[i + 1]);
            i += 1;
        }
    }
}

/************************************************************************/
/*                          GDALRemoveBOM()                             */
/************************************************************************/

/* Remove potential UTF-8 BOM from data (must be NUL terminated) */
void GDALRemoveBOM(GByte *pabyData)
{
    if (pabyData[0] == 0xEF && pabyData[1] == 0xBB && pabyData[2] == 0xBF)
    {
        memmove(pabyData, pabyData + 3,
                strlen(reinterpret_cast<char *>(pabyData) + 3) + 1);
    }
}

/************************************************************************/
/*                      GDALRemoveSQLComments()                         */
/************************************************************************/

std::string GDALRemoveSQLComments(const std::string &osInput)
{
    char **papszLines =
        CSLTokenizeStringComplex(osInput.c_str(), "\r\n", FALSE, FALSE);
    std::string osSQL;
    for (char **papszIter = papszLines; papszIter && *papszIter; ++papszIter)
    {
        const char *pszLine = *papszIter;
        char chQuote = 0;
        int i = 0;
        for (; pszLine[i] != '\0'; ++i)
        {
            if (chQuote)
            {
                if (pszLine[i] == chQuote)
                {
                    if (pszLine[i + 1] == chQuote)
                    {
                        i++;
                    }
                    else
                    {
                        chQuote = 0;
                    }
                }
            }
            else if (pszLine[i] == '\'' || pszLine[i] == '"')
            {
                chQuote = pszLine[i];
            }
            else if (pszLine[i] == '-' && pszLine[i + 1] == '-')
            {
                break;
            }
        }
        if (i > 0)
        {
            osSQL.append(pszLine, i);
        }
        osSQL += ' ';
    }
    CSLDestroy(papszLines);
    return osSQL;
}

/************************************************************************/
/*                            ArgIsNumeric()                            */
/************************************************************************/

int ArgIsNumeric(const char *pszArg)

{
    return CPLGetValueType(pszArg) != CPL_VALUE_STRING;
}
