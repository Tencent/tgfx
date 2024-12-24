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

#include "Utility.h"
#include <QFontMetrics>
#include <QRect>
#include <QString>
#include "src/profiler/TracyColor.hpp"

// Short list based on GetTypes() in TracySourceTokenizer.cpp
constexpr const char* TypesList[] = {
  "bool ", "char ", "double ", "float ", "int ", "long ", "short ",
  "signed ", "unsigned ", "void ", "wchar_t ", "size_t ", "int8_t ",
  "int16_t ", "int32_t ", "int64_t ", "intptr_t ", "uint8_t ", "uint16_t ",
  "uint32_t ", "uint64_t ", "ptrdiff_t ", nullptr
};

QFont& getFont() {
  QFont font("Arial", 12);
  return font;
}


QRect getFontSize(const char* text, size_t textSize) {
  QFont font("Arial", 12);
  QFontMetrics fm(font);
  QString str = QString(text);
  if (textSize) {
    str = QString(str.data(), textSize);
  }
  return fm.boundingRect(str);
}

QColor getColor(uint32_t color) {
  return QColor(color);
}

void drawPolyLine(QPainter* painter, QPointF p1, QPointF p2, QPointF p3, QColor color, float thickness) {
  QPointF points[3] = {p1, p2, p3};
  painter->setPen(QPen(color, thickness));
  painter->drawPolyline(points, 3);
}

void drawTextContrast(QPainter* painter, QPointF pos, QColor color, const char* text) {
  painter->setPen(QColor(0xAA000000));
  auto height = getFontSize(text).height();
  painter->drawText(pos + QPointF(0.5f, 0.5f + height), text);
  painter->setPen(QColor(color));
  painter->drawText(pos + QPointF(0, height), text);
}

uint32_t getThreadColor( uint64_t thread, int depth, bool dynamic ) {
  if( !dynamic ) return 0xFFCC5555;
  return tracy::GetHsvColor( thread, depth );
}

uint32_t getPlotColor( const tracy::PlotData& plot, const Worker& worker ) {

}

const char* shortenZoneName(ShortenName type, const char* name, QRect tsz, float zsz) {
    assert( type != ShortenName::Never );
    if( name[0] == '<' || name[0] == '[' ) return name;
    if( type == ShortenName::Always ) zsz = 0;

    static char buf[64*1024];
    char tmp[64*1024];

    auto end = name + strlen( name );
    auto ptr = name;
    auto dst = tmp;
    int cnt = 0;
    for(;;)
    {
        auto start = ptr;
        while( ptr < end && *ptr != '<' ) ptr++;
        memcpy( dst, start, ptr - start + 1 );
        dst += ptr - start + 1;
        if( ptr == end ) break;
        cnt++;
        ptr++;
        while( cnt > 0 )
        {
            if( ptr == end ) break;
            if( *ptr == '<' ) cnt++;
            else if( *ptr == '>' ) cnt--;
            ptr++;
        }
        *dst++ = '>';
    }

    end = dst-1;
    ptr = tmp;
    dst = buf;
    cnt = 0;
    for(;;)
    {
        auto start = ptr;
        while( ptr < end && *ptr != '(' ) ptr++;
        memcpy( dst, start, ptr - start + 1 );
        dst += ptr - start + 1;
        if( ptr == end ) break;
        cnt++;
        ptr++;
        while( cnt > 0 )
        {
            if( ptr == end ) break;
            if( *ptr == '(' ) cnt++;
            else if( *ptr == ')' ) cnt--;
            ptr++;
        }
        *dst++ = ')';
    }

    end = dst-1;
    if( end - buf > 6 && memcmp( end-6, " const", 6 ) == 0 )
    {
        dst[-7] = '\0';
        end -= 6;
    }

    ptr = buf;
    for(;;)
    {
        auto match = TypesList;
        while( *match )
        {
            auto m = *match;
            auto p = ptr;
            while( *m )
            {
                if( *m != *p ) break;
                m++;
                p++;
            }
            if( !*m )
            {
                ptr = p;
                break;
            }
            match++;
        }
        if( !*match ) break;
    }

    tsz = getFontSize(ptr, ptr - end);

    if( type == ShortenName::OnlyNormalize || tsz.width() < zsz ) return ptr;

    for(;;)
    {
        auto p = ptr;
        while( p < end && *p != ':' ) p++;
        if( p == end ) return ptr;
        p++;
        while( p < end && *p == ':' ) p++;
        ptr = p;
        tsz = getFontSize(ptr, ptr - end);
        if( tsz.width() < zsz ) return ptr;
    }
}