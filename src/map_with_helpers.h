/*
 *  map_with_helpers.h
 *
 *  Copyright (C) 2008 - Niko Ritari
 *
 *  This file is part of Outgun.
 *
 *  Outgun is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Outgun is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Outgun; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef MAP_WITH_HELPERS_H_INC
#define MAP_WITH_HELPERS_H_INC

#include <map>

#include "nassert.h"

class NotInMap { }; // exception

template<class T1, class T2, class T1EquivT> const T2& map_get(const std::map<T1, T2>& m, const T1EquivT& key) throw (NotInMap) {
    const typename std::map<T1, T2>::const_iterator i = m.find(key);
    if (i == m.end())
        throw NotInMap();
    return i->second;
}

template<class T1, class T2, class T1EquivT> const T2& map_get_assert(const std::map<T1, T2>& m, const T1EquivT& key) throw () {
    try {
        return map_get(m, key);
    } catch (NotInMap) {
        nAssert(0);
    }
}

template<class T1, class T2> class MapReader { /// to be used in place of a const map& to provide an operator[]
    typedef std::map<T1, T2> MapT;
    const MapT& m;

public:
    MapReader(const MapT& map) throw () : m(map) { }
    const T2& operator[](const T1& key) const throw (NotInMap) { return map_get(m, key); }
};

template<class T1, class T2> class AssertingMapReader { /// to be used in place of a const map& to provide an operator[]
    typedef std::map<T1, T2> MapT;
    const MapT& m;

public:
    AssertingMapReader(const MapT& map) throw () : m(map) { }
    const T2& operator[](const T1& key) const throw () { return map_get_assert(m, key); }
};

#endif
