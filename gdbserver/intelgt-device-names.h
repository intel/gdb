/* Code for name translation of Intel(R) Graphics Technology devices for the
   remote server of GDB.

   Copyright (C) 2022 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

struct Device {
  unsigned int device_id;
  const char *device_name;
};

const Device device_table[] = {
#include "intelgt-device-names.inl"
  {0, nullptr}
};

static const char*
intelgt_device_name_from_id (unsigned int device_id)
{
  for (const auto &d : device_table)
    if (d.device_id == device_id)
      return d.device_name;

  return "Intel(R) Graphics";
}
