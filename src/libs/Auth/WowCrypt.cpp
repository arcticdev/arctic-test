/*
 * Arctic MMORPG Server Software
 * Copyright (c) 2008-2014 Arctic Server Team
 * See COPYING for license details.
 */

#include "WowCrypt.h"
#include <algorithm>

WowCrypt::WowCrypt()
{
	_initialized = false;
}

void WowCrypt::Init(uint8 *K)
{
	const uint8 SeedKeyLen = 16;
    uint8 ServerEncryptionKey[SeedKeyLen] = { 0xCC, 0x98, 0xAE, 0x04, 0xE8, 0x97, 0xEA, 0xCA, 0x12, 0xDD, 0xC0, 0x93, 0x42, 0x91, 0x53, 0x57 };

	HMACHash auth;
	auth.Initialize(SeedKeyLen, (uint8*)ServerEncryptionKey);
	auth.UpdateData(K, 40);
	auth.Finalize();
	uint8 *encryptHash = auth.GetDigest();

    uint8 ServerDecryptionKey[SeedKeyLen] = { 0xC2, 0xB3, 0x72, 0x3C, 0xC6, 0xAE, 0xD9, 0xB5, 0x34, 0x3C, 0x53, 0xEE, 0x2F, 0x43, 0x67, 0xCE };
	HMACHash auth2;
	auth2.Initialize(SeedKeyLen, (uint8*)ServerDecryptionKey);
	auth2.UpdateData(K, 40);
	auth2.Finalize();
	uint8 *decryptHash = auth2.GetDigest();

    _Decrypt.Setup((uint8*)decryptHash, 20);
    _Encrypt.Setup((uint8*)encryptHash, 20);

	uint8 encryptRotateBuffer[1024];
    memset(encryptRotateBuffer, 0, 1024);
	_Encrypt.Process((uint8*)encryptRotateBuffer, (uint8*)encryptRotateBuffer, 1024);

	uint8 decryptRotateBuffer[1024];
	memset(decryptRotateBuffer, 0, 1024);
	_Decrypt.Process((uint8*)decryptRotateBuffer, (uint8*)decryptRotateBuffer, 1024);

    _initialized = true;
}

void WowCrypt::DecryptRecv(uint8 *data, unsigned int len)
{
    if (!_initialized)
        return;

    _Decrypt.Process((uint8*)data, (uint8*)data, len);
}

void WowCrypt::EncryptSend(uint8 *data, unsigned int len)
{
    if (!_initialized)
        return;

    _Encrypt.Process((uint8*)data, (uint8*)data, len);
}

WowCrypt::~WowCrypt()
{
}
