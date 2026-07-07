/*
 *  mappic.cpp
 *
 *  Copyright (C) 2004, 2009 - Jani Rivinoja
 *  Copyright (C) 2006 - Niko Ritari
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

#include "incalleg.h"

#include "graphics.h"
#include "language.h"
#include "platform.h"
#include "world.h"

#include "mappic.h"

using std::string;
using std::vector;

Mappic::Mappic(LogSet logs, const string& source, const string& target) throw () :
    log(logs),
    source_dir(source),
    target_dir(target)
{
    if (!source_dir.empty() && *source_dir.rbegin() != directory_separator)
        source_dir += directory_separator;
    if (!target_dir.empty() && *target_dir.rbegin() != directory_separator)
        target_dir += directory_separator;
}

void Mappic::run() throw (Save_error) {
    smaps = load_maps();
    save_pictures();
}

vector<string> Mappic::load_maps() throw () {
    vector<string> maps;
    FileFinder* mapFiles = platMakeFileFinder(source_dir, ".txt", false);
    while (mapFiles->hasNext())
        maps.push_back(FileName(mapFiles->next()).getBaseName());
    delete mapFiles;
    log("Map picture saver: %lu maps found.", (unsigned long)maps.size());
    return maps;
}

void Mappic::save_pictures() const throw (Save_error) {
    set_color_depth(16);
    Graphics graphics(log);
    graphics.setColors();
    for (vector<string>::const_iterator name = smaps.begin(); name != smaps.end(); name++) {
        const string picture = target_dir + *name + Graphics::save_extension;
        Map mp;
        if (!mp.load_file(log, source_dir + *name + ".txt"))
            log.error(_("Map picture saver: Map '$1' is not a valid map file.", *name));
        else if (graphics.save_map_picture(picture, mp))
            log("Map picture saver: Saved map picture to '%s'.", picture.c_str());
        else {
            log.error(_("Map picture saver: Can't save map picture to '$1'.", picture));
            throw Save_error();
        }
    }
}
