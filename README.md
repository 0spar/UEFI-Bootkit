# UEFI-Bootkit

A small bootkit designed to use zero ASM. Make sure to compile the driver as an EFI Runtime driver (EFI_RUNTIME_DRIVER) or else the bootkit will be freed once winload.efi calls ExitBootService!

Thanks to [pyro666](https://github.com/Pyro666), [dreamboot](https://github.com/quarkslab/dreamboot), and [VisualUEFI](https://github.com/ionescu007/VisualUefi)

![alt text](https://i.gyazo.com/8fa42e625ee993ab1bd0ee136076f5ef.png "Bootkit")

## License
Copyright (C) 2016 dude719, pyro666, Quarkslab

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
