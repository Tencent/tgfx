/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "UserData.h"
#include <assert.h>
#include <memory>

#ifdef _WIN32
#  include <stdio.h>
#else
#  include <unistd.h>
#endif

#include "src/ini.h"
#include "src/profiler/TracyStorage.hpp"

constexpr auto FileDescription = "description";
constexpr auto FileTimeline = "timeline";
constexpr auto FileOptions = "options";

enum : uint32_t { VersionTimeline = 0 };
enum : uint32_t { VersionOptions = 7 };
enum : uint32_t { VersionAnnotations = 0 };
enum : uint32_t { VersionSourceSubstitutions = 0 };

UserData::UserData()
    : preserveState( false )
{
}

UserData::UserData( const char* program, uint64_t time )
    : program( program )
    , time( time )
{
  if( this->program.empty() ) this->program = "_";

  FILE* f = OpenFile( FileDescription, false );
  if( f )
  {
    fseek( f, 0, SEEK_END );
    auto sz = ftell( f );
    fseek( f, 0, SEEK_SET );
    auto size = static_cast<unsigned long>(sz);
    auto buf = std::unique_ptr<char[]>( new char[size] );
    fread( buf.get(), 1, size_t(sz), f );
    fclose( f );
    description.assign( buf.get(), buf.get() + sz );
  }
}


void UserData::LoadState( ViewData& data )
{
    assert( Valid() );
    FILE* f = OpenFile( FileTimeline, false );
    if( f )
    {
        uint32_t ver;
        fread( &ver, 1, sizeof( ver ), f );
        if( ver == VersionTimeline )
        {
            fread( &data.zvStart, 1, sizeof( data.zvStart ), f );
            fread( &data.zvEnd, 1, sizeof( data.zvEnd ), f );
            //fread( &data.zvHeight, 1, sizeof( data.zvHeight ), f );
            fseek( f, sizeof( float ), SEEK_CUR );
            //fread( &data.zvScroll, 1, sizeof( data.zvScroll ), f );
            fseek( f, sizeof( float ), SEEK_CUR );
            fread( &data.frameScale, 1, sizeof( data.frameScale ), f );
            fread( &data.frameStart, 1, sizeof( data.frameStart ), f );
        }
        fclose( f );
    }

    f = OpenFile( FileOptions, false );
    if( f )
    {
        uint32_t ver;
        fread( &ver, 1, sizeof( ver ), f );
        // TODO: remove in future
        if( ver == VersionOptions )
        {
            fread( &data.drawGpuZones, 1, sizeof( data.drawGpuZones ), f );
            fread( &data.drawZones, 1, sizeof( data.drawZones ), f );
            fread( &data.drawLocks, 1, sizeof( data.drawLocks ), f );
            fread( &data.drawPlots, 1, sizeof( data.drawPlots ), f );
            fread( &data.onlyContendedLocks, 1, sizeof( data.onlyContendedLocks ), f );
            fread( &data.drawEmptyLabels, 1, sizeof( data.drawEmptyLabels ), f );
            fread( &data.drawFrameTargets, 1, sizeof( data.drawFrameTargets ), f );
            fread( &data.drawContextSwitches, 1, sizeof( data.drawContextSwitches ), f );
            fread( &data.darkenContextSwitches, 1, sizeof( data.darkenContextSwitches ), f );
            fread( &data.drawCpuData, 1, sizeof( data.drawCpuData ), f );
            fread( &data.drawCpuUsageGraph, 1, sizeof( data.drawCpuUsageGraph ), f );
            fread( &data.drawSamples, 1, sizeof( data.drawSamples ), f );
            fread( &data.dynamicColors, 1, sizeof( data.dynamicColors ), f );
            fread( &data.forceColors, 1, sizeof( data.forceColors ), f );
            fread( &data.ghostZones, 1, sizeof( data.ghostZones ), f );
            fread( &data.frameTarget, 1, sizeof( data.frameTarget ), f );
            fclose( f );
        }
        else
        {
            fclose( f );
            const auto path = tracy::GetSavePath( program.c_str(), time, FileOptions, false );
            assert( path );
            auto ini = ini_load( path );
            if( ini )
            {
                int v;
                if( ini_sget( ini, "options", "drawGpuZones", "%d", &v ) ) data.drawGpuZones = (uint8_t)v;
                if( ini_sget( ini, "options", "drawZones", "%d", &v ) ) data.drawZones = (uint8_t)v;
                if( ini_sget( ini, "options", "drawLocks", "%d", &v ) ) data.drawLocks = (uint8_t)v;
                if( ini_sget( ini, "options", "drawPlots", "%d", &v ) ) data.drawPlots = (uint8_t)v;
                if( ini_sget( ini, "options", "onlyContendedLocks", "%d", &v ) ) data.onlyContendedLocks = (uint8_t)v;
                if( ini_sget( ini, "options", "drawEmptyLabels", "%d", &v ) ) data.drawEmptyLabels = (uint8_t)v;
                if( ini_sget( ini, "options", "drawFrameTargets", "%d", &v ) ) data.drawFrameTargets = (uint8_t)v;
                if( ini_sget( ini, "options", "drawContextSwitches", "%d", &v ) ) data.drawContextSwitches = (uint8_t)v;
                if( ini_sget( ini, "options", "darkenContextSwitches", "%d", &v ) ) data.darkenContextSwitches = (uint8_t)v;
                if( ini_sget( ini, "options", "drawCpuData", "%d", &v ) ) data.drawCpuData = (uint8_t)v;
                if( ini_sget( ini, "options", "drawCpuUsageGraph", "%d", &v ) ) data.drawCpuUsageGraph = (uint8_t)v;
                if( ini_sget( ini, "options", "drawSamples", "%d", &v ) ) data.drawSamples = (uint8_t)v;
                if( ini_sget( ini, "options", "dynamicColors", "%d", &v ) ) data.dynamicColors = (uint8_t)v;
                if( ini_sget( ini, "options", "inheritParentColors", "%d", &v ) ) data.inheritParentColors = (uint8_t)v;
                if( ini_sget( ini, "options", "forceColors", "%d", &v ) ) data.forceColors = (uint8_t)v;
                if( ini_sget( ini, "options", "ghostZones", "%d", &v ) ) data.ghostZones = (uint8_t)v;
                if( ini_sget( ini, "options", "frameTarget", "%d", &v ) ) data.frameTarget = (uint8_t)v;
                if( ini_sget( ini, "options", "shortenName", "%d", &v ) ) data.shortenName = (ShortenName)v;
                if( ini_sget( ini, "options", "plotHeight", "%d", &v ) ) data.plotHeight = (uint8_t)v;
                ini_free( ini );
            }
        }
    }
}

void UserData::SaveState( const ViewData& data )
{
    if( !preserveState ) return;
    assert( Valid() );
    FILE* f = OpenFile( FileTimeline, true );
    if( f )
    {
        uint32_t ver = VersionTimeline;
        fwrite( &ver, 1, sizeof( ver ), f );
        fwrite( &data.zvStart, 1, sizeof( data.zvStart ), f );
        fwrite( &data.zvEnd, 1, sizeof( data.zvEnd ), f );
        //fwrite( &data.zvHeight, 1, sizeof( data.zvHeight ), f );
        float zero = 0;
        fwrite( &zero, 1, sizeof( zero ), f );
        //fwrite( &data.zvScroll, 1, sizeof( data.zvScroll ), f );
        fwrite( &zero, 1, sizeof( zero ), f );
        fwrite( &data.frameScale, 1, sizeof( data.frameScale ), f );
        fwrite( &data.frameStart, 1, sizeof( data.frameStart ), f );
        fclose( f );
    }

    f = OpenFile( FileOptions, true );
    if( f )
    {
        fprintf( f, "[options]\n" );
        fprintf( f, "drawGpuZones = %d\n", data.drawGpuZones );
        fprintf( f, "drawZones = %d\n", data.drawZones );
        fprintf( f, "drawLocks = %d\n", data.drawLocks );
        fprintf( f, "drawPlots = %d\n", data.drawPlots );
        fprintf( f, "onlyContendedLocks = %d\n", data.onlyContendedLocks );
        fprintf( f, "drawEmptyLabels = %d\n", data.drawEmptyLabels );
        fprintf( f, "drawFrameTargets = %d\n", data.drawFrameTargets );
        fprintf( f, "drawContextSwitches = %d\n", data.drawContextSwitches );
        fprintf( f, "darkenContextSwitches = %d\n", data.darkenContextSwitches );
        fprintf( f, "drawCpuData = %d\n", data.drawCpuData );
        fprintf( f, "drawCpuUsageGraph = %d\n", data.drawCpuUsageGraph );
        fprintf( f, "drawSamples = %d\n", data.drawSamples );
        fprintf( f, "dynamicColors = %d\n", data.dynamicColors );
        fprintf( f, "inheritParentColors = %d\n", data.inheritParentColors );
        fprintf( f, "forceColors = %d\n", data.forceColors );
        fprintf( f, "ghostZones = %d\n", data.ghostZones );
        fprintf( f, "frameTarget = %d\n", data.frameTarget );
        fprintf( f, "shortenName = %d\n", (int)data.shortenName );
        fprintf( f, "plotHeight = %d\n", data.plotHeight );
        fclose( f );
    }
}

void UserData::StateShouldBePreserved()
{
    preserveState = true;
}

FILE* UserData::OpenFile( const char* filename, bool write )
{
  const auto path = tracy::GetSavePath( program.c_str(), time, filename, write );
  if( !path ) return nullptr;
  FILE* f = fopen( path, write ? "wb" : "rb" );
  return f;
}

void UserData::Remove( const char* filename )
{
  const auto path = tracy::GetSavePath( program.c_str(), time, filename, false );
  if( !path ) return;
  unlink( path );
}