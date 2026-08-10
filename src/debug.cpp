/*
 *  debug.cpp
 *
 *  Copyright (C) 2004, 2008 - Niko Ritari
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

#include <cstdio>

#include "commont.h"
#include "debug.h"
#include "debugconfig.h"

void ThreadLog::beginEntry() throw () {
    if (!file) {
        // whereuserdir, not a bare relative path: this used to be inert (LOG_THREAD_ACTIONS was
        // always false), but DEVBUILD can now enable it, including for a packaged AppImage/tarball
        // whose CWD when launched isn't reliably writable -- and the nAssert below means a failed
        // fopen crashes rather than degrading, so this has to land somewhere known-writable.
        file = fopen((whereuserdir + "log" + directory_separator + "threadlog.bin").c_str(), "wb");
        nAssert(file); // no fancy handling in developer-only code
    }
}

void ThreadLog::endEntry() throw () {
    if (FLUSH_THREAD_LOG)
        fflush(file);
}
