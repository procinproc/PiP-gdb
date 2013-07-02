# Caller-is functions.

# Copyright (C) 2008 Free Software Foundation, Inc.

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import gdb
import re

class CallerIs (gdb.Function):
    """Return True if the calling function's name is equal to a string.
This function takes one or two arguments.
The first argument is the name of a function; if the calling function's
name is equal to this argument, this function returns True.
The optional second argument tells this function how many stack frames
to traverse to find the calling function.  The default is 1."""

    def __init__ (self):
        super (CallerIs, self).__init__ ("caller_is")

    def invoke (self, name, nframes = 1):
        frame = gdb.selected_frame ()
        while nframes > 0:
            frame = frame.older ()
            nframes = nframes - 1
        return frame.name () == name.string ()

class CallerMatches (gdb.Function):
    """Return True if the calling function's name matches a string.
This function takes one or two arguments.
The first argument is a regular expression; if the calling function's
name is matched by this argument, this function returns True.
The optional second argument tells this function how many stack frames
to traverse to find the calling function.  The default is 1."""

    def __init__ (self):
        super (CallerMatches, self).__init__ ("caller_matches")

    def invoke (self, name, nframes = 1):
        frame = gdb.selected_frame ()
        while nframes > 0:
            frame = frame.older ()
            nframes = nframes - 1
        return re.match (name.string (), frame.name ()) is not None

CallerIs()
CallerMatches()
