/* Copyright (C) 2024 The Android Open Source Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
*/

#pragma once
#include "goldfish/devices/cable/cable.h"
#include "goldfish/archive/Reader.h"
#include "goldfish/archive/Writer.h"

/* These functions should be used inside socket managers
 * to save-load their internal implementations of `ISocket`.
 */

namespace goldfish {
namespace devices {
namespace cable {

/* fully (both `IPlug::TypeId` and its state) saves
 *`IPlug` to a snapshot.
 */
bool savePlugToSnapshot(const IPlug &plug, archive::IWriter &);

/* reads `IPlug::TypeId`, finds its loader (see `registerPlugLoader`
 * in cable.h) and loads an `IPlug` from a snapshot. If any of these
 * operation fails, it returns the given `socket` back.
 */
PlugOrSocket loadPlugFromSnapshot(SocketPtr socket, archive::IReader &);

}  // namespace cable
}  // namespace devices
}  // namespace goldfish
