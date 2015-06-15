// $RCSfile: $
// $Revision: $ $Date: $
// Auth: Samson Bonfante (bonfante@steptools.com)
// 
// Copyright (c) 1991-2015 by STEP Tools Inc. 
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "guid.h"

#ifdef _WIN32
//----------------------------------------
// WINDOWS, uuid from CoCreateGuid, link with ole32.lib
#define GETUUID	GetWindowsUUID
#include <rpc.h>
static void GetWindowsUUID(unsigned char uuid[16])
{
    GUID guid;
    CoCreateGuid(&guid);

    // Data1 Specifies the first 8 hexadecimal digits of the GUID.
    uuid[0] = (unsigned char)(guid.Data1 >> 24) & 0xff;
    uuid[1] = (unsigned char)(guid.Data1 >> 16) & 0xff;
    uuid[2] = (unsigned char)(guid.Data1 >> 8) & 0xff;
    uuid[3] = (unsigned char)(guid.Data1) & 0xff;

    uuid[4] = (unsigned char)(guid.Data2 >> 8) & 0xff;
    uuid[5] = (unsigned char)(guid.Data2) & 0xff;

    uuid[6] = (unsigned char)(guid.Data3 >> 8) & 0xff;
    uuid[7] = (unsigned char)(guid.Data3) & 0xff;

    for (unsigned i = 0; i<8; i++)
	uuid[8 + i] = guid.Data4[i];
}
#endif 

#ifdef __APPLE__
//----------------------------------------
// MACOS, uuid from CFUUIDCreate, link with -framework CoreFoundation
#include <CoreFoundation/CoreFoundation.h>
#define GETUUID GetMacUUID

static void GetMacUUID(unsigned char uuid[16])
{
    CFUUIDRef idref = CFUUIDCreate(NULL);
    CFUUIDBytes idbytes = CFUUIDGetUUIDBytes(idref);

    uuid[0] = idbytes.byte0;
    uuid[1] = idbytes.byte1;
    uuid[2] = idbytes.byte2;
    uuid[3] = idbytes.byte3;
    uuid[4] = idbytes.byte4;
    uuid[5] = idbytes.byte5;
    uuid[6] = idbytes.byte6;
    uuid[7] = idbytes.byte7;
    uuid[8] = idbytes.byte8;
    uuid[9] = idbytes.byte9;
    uuid[10] = idbytes.byte10;
    uuid[11] = idbytes.byte11;
    uuid[12] = idbytes.byte12;
    uuid[13] = idbytes.byte13;
    uuid[14] = idbytes.byte14;
    uuid[15] = idbytes.byte15;

    CFRelease(idref);
}
#endif


#if defined(linux) || defined(__linux__)
//----------------------------------------
// LINUX, uuid from uuid_generate(), link with -luuid.  On Linux, the
// uuid_t definition is also an array[16] of bytes, so we just use the
// library function directly.
// 
#include <uuid/uuid.h>
#define GETUUID	uuid_generate
#endif


#ifndef GETUUID
//----------------------------------------
// OTHER PLATFORMS, you will need to find the appropriate
// platform-specific function or get the ethernet id and make your
// own.
#define GETUUID	unknown_uuid_function

static void unknown_uuid_function(unsigned char uuid[16])
{
    // fill with random values as a temporary measure
    for (int i = 0; i<16; i++) {
	uuid[i] = (unsigned char)(rand() % 0x100);
    }
}
#endif

std::string get_guid()
{
    unsigned char uuid[16];
    GETUUID(uuid);
    char foo[47];
    #ifdef _WIN32
	_snprintf(foo, 47, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", uuid[0],uuid[1],uuid[2],uuid[3],uuid[4],uuid[5],uuid[6],uuid[7],uuid[8],uuid[9],uuid[10],uuid[11],uuid[12],uuid[13],uuid[14],uuid[15]);
    #else
	snprintf(foo, 47, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", uuid[0],uuid[1],uuid[2],uuid[3],uuid[4],uuid[5],uuid[6],uuid[7],uuid[8],uuid[9],uuid[10],uuid[11],uuid[12],uuid[13],uuid[14],uuid[15]);
    #endif
    std::string rtn(foo);
    return rtn;
}