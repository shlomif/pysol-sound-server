#! /usr/bin/env python
##
## vi:ts=4:et:nowrap
##
##---------------------------------------------------------------------------##
##
## PySol -- a Python Solitaire Game Collection
##
## Copyright (C) 2004 Markus Franz Xaver Johannes Oberhumer
## Copyright (C) 2003 Markus Franz Xaver Johannes Oberhumer
## Copyright (C) 2002 Markus Franz Xaver Johannes Oberhumer
## Copyright (C) 2001 Markus Franz Xaver Johannes Oberhumer
## Copyright (C) 2000 Markus Franz Xaver Johannes Oberhumer
## Copyright (C) 1999 Markus Franz Xaver Johannes Oberhumer
## All Rights Reserved.
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; see the file COPYING.
## If not, write to the Free Software Foundation, Inc.,
## 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
##
## Markus F.X.J. Oberhumer
## <markus@oberhumer.com>
## http://www.oberhumer.com/pysol
##
##---------------------------------------------------------------------------##

#
# Test program.
#
# Play all MOD and MP3 files given on the commandline
#

import sys, os, time, getopt

# update sys.path when running in the build directory
from util import get_sys_path
sys.path = get_sys_path()

# import the module
import pysolsoundserver


#
# parse arguments
#

opts, files = getopt.getopt(sys.argv[1:], "l")
if not files:
    print( "Usage: playmus.py [-l] MOD-or-MP3-file..." )
    sys.exit(1)
priority = 0
loop = ("-l", "") in opts
volume = 128


#
# init soundserver
#

p = pysolsoundserver
##print( p.__dict__ )
print(p.__version__, p.__date__, p.__file__)
r = p.init()
if r:
    print( "init:", r )
else:
    print( "init() failed:", r )
    sys.exit(1)
r = p.cmd("protocol 6")
if r != 0:
    print( "protocol failed:", r )
    sys.exit(1)
##p.cmd("debug 3")


#
# prepare playlist
#

playlist = {}
id = 0
for file in files:
    if os.path.isfile(file):
        p.cmd("queuemus '%s' %d %d %d %d" % (file, id, priority, loop, volume))
        playlist[id] = file
        id = id + 1
    else:
        print( "not a plain file -- skipping:", file )


#
# start playing
#

p.cmd("startqueue")
last_id = -1
while 1:
    id = p.getMusicInfo()
    if id < 0:
        print( "Done." )
        break
    if id != last_id:
        print( "Playing %s" % (playlist.get(id)) )
        last_id = id
    time.sleep(1)

sys.exit(0)

