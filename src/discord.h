// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2012-2018 by Sally "TehRealSalt" Cochenour.
// Copyright (C) 2012-2016 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  discord.h
/// \brief Discord Rich Presence handling

#ifndef __DISCORD__
#define __DISCORD__

#ifdef HAVE_DISCORDRPC

#include "discord_rpc.h"

extern consvar_t cv_discordrp;

void DRPC_Init(void);
void DRPC_UpdatePresence(void);

#endif // HAVE_DISCORDRPC

#endif // __DISCORD__
