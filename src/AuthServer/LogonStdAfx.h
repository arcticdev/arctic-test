/*
 * Arctic MMORPG Server Software
 * Copyright (c) 2008-2014 Arctic Server Team
 * See COPYING for license details.
 */

#pragma once

#include <signal.h>
#include <list>
#include <vector>
#include <map>
#include <sstream>
#include <string>
// #include <fstream>

#include <Network/Network.h>

#include "../libs/svn_revision.h"
#include "../libs/Getopt.h"
#include "../libs/Log.h"
#include "../libs/Utilities/Utility.h"
#include "../libs/ByteBuffer.h"
#include "../libs/Common.h"
#include "../libs/Config/ConfigEnv.h"

#include "../../dep/vc/include/zlib.h"
#include "../../dep/vc/include/openssl/md5.h"

#include "../libs/DataStorage/DatabaseEnv.h"
#include "../libs/DataStorage/DBCStores.h"
#include "../libs/DataStorage/dbcfile.h"
#include "../libs/Auth/MD5.h"
#include "../libs/Auth/BigNumber.h"
#include "../libs/Auth/Sha1.h"
#include "../libs/Auth/WowCrypt.h"
#include "../libs/CrashHandler.h"
#include "../libs/WorldPacket.h"

#include "LogonOpcodes.h"
#include "Main.h"
#include "AccountCache.h"
#include "AutoPatcher.h"
#include "AuthSocket.h"
#include "AuthStructs.h"
#include "LogonOpcodes.h"
#include "LogonCommServer.h"
#include "LogonConsole.h"
#include "PeriodicFunctionCall_Thread.h"

#include "../WorldServer/NameTables.h"

#ifndef WIN32
#include <sys/resource.h>
#endif

#ifndef WIN32
#include <sched.h>
#endif

// database decl
extern Database* sLogonSQL;
