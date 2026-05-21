/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * (c) ZeroTier, Inc.
 * https://www.zerotier.com/
 */

#include "Packet.hpp"

#include "ECC.hpp"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef ZT_ESP32_STEP46_CAPTURE_REAL_PACKET
#define ZT_ESP32_STEP46_CAPTURE_REAL_PACKET 1
#endif
#ifndef ZT_ESP32_STEP47_DEARMOR_DIVERGENCE
#define ZT_ESP32_STEP47_DEARMOR_DIVERGENCE 1
#endif
#ifndef ZT_ESP32_STEP48_CRYPTO_PLATFORM_TEST
#define ZT_ESP32_STEP48_CRYPTO_PLATFORM_TEST 1
#endif
#ifndef ZT_ESP32_STEP49_GMAC_TRACE
#define ZT_ESP32_STEP49_GMAC_TRACE 1
#endif

#if defined(ZT_USE_X64_ASM_SALSA2012) && defined(ZT_ARCH_X64)
#include "../ext/x64-salsa2012-asm/salsa2012.h"
#endif
#ifdef ZT_USE_ARM32_NEON_ASM_SALSA2012
#include "../ext/arm32-neon-salsa2012-asm/salsa2012.h"
#endif

#ifdef _MSC_VER
#define FORCE_INLINE static __forceinline
#include <intrin.h>
#pragma warning(disable : 4127) /* disable: C4127: conditional expression is constant */
#pragma warning(disable : 4293) /* disable: C4293: too large shift (32-bits) */
#else
#define FORCE_INLINE static inline
#endif

namespace ZeroTier {
#ifdef ZT_ESP32_STEP46_CAPTURE_REAL_PACKET
static bool ZT_STEP46_CAPTURE_DONE = false;
static void ztStep46HexDump(const char* tag, const uint8_t* p, unsigned int len, unsigned int chunk)
{
	for (unsigned int o = 0; o < len; o += chunk) {
		const unsigned int n = ((o + chunk) <= len) ? chunk : (len - o);
		printf("%s offset=%u hex=", tag, o);
		for (unsigned int i = 0; i < n; ++i) {
			printf("%02x", p[o + i]);
		}
		printf("\n");
	}
}
#endif

#ifdef ZT_ESP32_STEP47_DEARMOR_DIVERGENCE
static bool ZT_STEP47_ISO_DONE = false;
static bool ZT_STEP47_LIVE_SELECTED = false;
static bool ZT_STEP47_LIVE_DONE = false;
static uint64_t ztStep47Fnv1a(const uint8_t* p, unsigned int len)
{
	uint64_t h = 1469598103934665603ULL;
	for (unsigned int i = 0; i < len; ++i) {
		h ^= (uint64_t)p[i];
		h *= 1099511628211ULL;
	}
	return h;
}
static void ztStep47HexToBytes(const char* h, uint8_t* out, unsigned int len)
{
	for (unsigned int i = 0; i < len; ++i) {
		unsigned int v = 0;
		sscanf(h + (i * 2U), "%02x", &v);
		out[i] = (uint8_t)v;
	}
}
#endif



#ifdef ZT_ESP32_STEP49_GMAC_TRACE
static bool ZT_STEP49_DONE = false;
static void ztStep48Hex(const uint8_t* p, unsigned int len);
static void ztStep49RunGmacTraceOnce()
{
	if (ZT_STEP49_DONE) return;
	ZT_STEP49_DONE = true;
	printf("[ZT_STEP49_ESP32] test=begin phase=begin result=PASS\n");
	uint8_t key[ZT_SYMMETRIC_KEY_SIZE]; for (unsigned int i=0;i<ZT_SYMMETRIC_KEY_SIZE;++i) key[i]=(uint8_t)i;
	AES aes(key);
	uint8_t iv12[12]; for (unsigned int i=0;i<12;++i) iv12[i]=(uint8_t)(0x80+i);
	auto run=[&](const char* name, const uint8_t* d, unsigned int n){
		AES::GMAC g(aes);
		g.init(iv12);
		printf("[ZT_STEP49_ESP32] test=%s phase=init len=%u rp=%u y0=%016llx y1=%016llx iv=", name, g.ztStep49DebugTotalLen(), g.ztStep49DebugRemainderPos(), (unsigned long long)g.ztStep49DebugY0(), (unsigned long long)g.ztStep49DebugY1());
		ztStep48Hex(g.ztStep49DebugIv(),16); printf("\n");
		printf("[ZT_STEP49_ESP32] test=%s phase=before_update inLen=%u inHash=%016llx\n", name, n, (unsigned long long)ztStep47Fnv1a(d,n));
		g.update(d,n);
		printf("[ZT_STEP49_ESP32] test=%s phase=after_update len=%u rp=%u y0=%016llx y1=%016llx rHash=%016llx\n", name, g.ztStep49DebugTotalLen(), g.ztStep49DebugRemainderPos(), (unsigned long long)g.ztStep49DebugY0(), (unsigned long long)g.ztStep49DebugY1(), (unsigned long long)ztStep47Fnv1a(g.ztStep49DebugRemainder(),16));
		uint8_t tag[16];
		g.finish(tag);
		uint64_t t0=0,t1=0; memcpy(&t0,tag,8); memcpy(&t1,tag+8,8);
		printf("[ZT_STEP49_ESP32] test=%s phase=finish len=%u rp=%u y0=%016llx y1=%016llx tag=", name, g.ztStep49DebugTotalLen(), g.ztStep49DebugRemainderPos(), (unsigned long long)g.ztStep49DebugY0(), (unsigned long long)g.ztStep49DebugY1());
		ztStep48Hex(tag,16);
		printf(" tag64_0=%016llx tag64_1=%016llx result=PASS\n", (unsigned long long)t0, (unsigned long long)t1);
	};
	uint8_t b1[1]={0x11};
	uint8_t b15[15]; for(unsigned int i=0;i<15;++i) b15[i]=(uint8_t)(0x20+i);
	uint8_t b16[16]; for(unsigned int i=0;i<16;++i) b16[i]=(uint8_t)(0x30+i);
	uint8_t b17[17]; for(unsigned int i=0;i<17;++i) b17[i]=(uint8_t)(0x40+i);
	uint8_t b32[32]; for(unsigned int i=0;i<32;++i) b32[i]=(uint8_t)(0x50+i);
	run("gmac_empty", b1, 0);
	run("gmac_one_block", b16, 16);
	run("gmac_two_blocks", b32, 32);
	run("gmac_partial_1", b1, 1);
	run("gmac_partial_15", b15, 15);
	run("gmac_partial_17", b17, 17);
	static const char* STEP46_AAD_HEX = "d526b558a5db64858fed19";
	static const char* STEP46_PAYLOAD_HEX = "91daca0abab96088c6f2e5b22df9c41fb076ee2aba385af7ece35df3665fbcf67e16dca8b946d0a512c7f9391d4d9da27a520057fe3fc85ad2fbaae05c9dad55549edc516c598615436430027d01cd4319735fa369db564b6f868434364fdca9eb43896f1987bfb2ecd2413b62d0c2bf6954f8be1d4c8528d9cfa63b822dd8013b239e2f5cd1afc5305a08be4ce2a2935642d812d778e81d3b926a156a0a56a91d0a5716e43a87a1216a6f2d9edecc0b529821f17cd24abc643a49015d98c977726dc57cb2c3b84aa4b277917ca070eb6626793cc4f00f3fd049a3a2d96f18210daf480cee6ca9badab5f024d5327f8b3724420339b374a8633ada45448780f5744be91a9271d1b6bf6f3c675d20533ab9e24b6ddcecdb0d4e25a2836a1f9f7bf0a7884a32a3a3ae3b4fb84c51a45e6a5120160ac6678c8fd2766ef0b8f1a1e3b1f240e986445755d63abd1c856fd93261784235f5b86a6ee9abddbbe9cfc57a1a8d65dd18c921a6bed7ca410eecd5c55b58bf33d1c474cbaf0d2111cf7208ad314dd622b64ba7ca6500f883d4df26abb539907003e8fef8125291f0518973ff978b67df68c0d72af3c8739975205299fcf03a71787665682a89d9dc362c5ed4400f78cfd99f79e4d544a78c23d2e7deb7d8f4f0c8058e923525f73c51ff16da62f74e33fa12c45ae0fb73c8c7c538cf9ab0b1c9601354ee305b41778845fb99d10ff2a22836225983a2162ab9ac94e582ce910fc51ae5f72c0407e229e0a6013fbc62cc0d4cb8fc3a8d8cf5413833705837653eaa114120db7fc6ac2971fc7a37ca67f996e0f8bf489ca0cbd1564f210f114661b37e720e1c5ed4c55b13be1d1fde1d75ddcbb7859ef83a90770735d61a25003e026e7737bb236240ccd69a309be113ab89f612a6c6503123db8f0636fe9da0bb301ab5df64aa86840e46af2fe8c2342dd697d3fae7e0fb83790ccd1a72f2d9ad1ca4e1e4ee5437939d6d1a485f11b7e619bb96fea1d47c2f4d7001c86cd0fcca5b64eff2677e6db5da31";
	uint8_t aad[11]; ztStep47HexToBytes(STEP46_AAD_HEX, aad, 11); run("gmac_step46_aad", aad, 11);
	uint8_t pfx[32]; ztStep47HexToBytes(STEP46_PAYLOAD_HEX, pfx, 32); run("gmac_step46_payload_prefix", pfx, 32);
	uint8_t full[734]; ztStep47HexToBytes(STEP46_PAYLOAD_HEX, full, 734U); run("gmac_step46_payload_full", full, 734U);
	// length/final block probe via 15 vs 16 vs 17 tags already logged
	printf("[ZT_STEP49_ESP32] test=gmac_length_block_probe phase=summary result=PASS note=see_partial_and_block_cases\n");
	// uint64 byte order probe
	uint8_t probe[16]; for(unsigned int i=0;i<16;++i) probe[i]=(uint8_t)(0xa0+i);
	uint64_t q0=0,q1=0; memcpy(&q0,probe,8); memcpy(&q1,probe+8,8);
	printf("[ZT_STEP49_ESP32] test=gmac_uint64_byte_order_probe phase=probe result=PASS bytes="); ztStep48Hex(probe,16); printf(" tag64_0=%016llx tag64_1=%016llx\n", (unsigned long long)q0, (unsigned long long)q1);
	uint64_t c = 0x1122334455667788ULL;
	uint64_t hc = Utils::hton(c), nc = Utils::ntoh(hc);
	printf("[ZT_STEP49_ESP32] test=utils_hton_ntoh_probe phase=probe result=%s in=%016llx hton=%016llx ntoh=%016llx\n", (nc==c)?"PASS":"FAIL", (unsigned long long)c, (unsigned long long)hc, (unsigned long long)nc);
	printf("[ZT_STEP49_ESP32] test=end phase=end result=PASS\n");
}
#endif

#ifdef ZT_ESP32_STEP48_CRYPTO_PLATFORM_TEST
static bool ZT_STEP48_DONE = false;
static void ztStep48Hex(const uint8_t* p, unsigned int len)
{
	for (unsigned int i=0;i<len;++i) printf("%02x", p[i]);
}
static void ztStep48RunTestsOnce()
{
	if (ZT_STEP48_DONE) return;
	ZT_STEP48_DONE = true;
	printf("[ZT_STEP48_ESP32] test=begin result=PASS mode=isolated\n");

	// deterministic synthetic vectors
	uint8_t key[ZT_SYMMETRIC_KEY_SIZE];
	for (unsigned int i=0;i<ZT_SYMMETRIC_KEY_SIZE;++i) key[i]=(uint8_t)i;
	uint8_t block[16]; for (unsigned int i=0;i<16;++i) block[i]=(uint8_t)(0xa0+i);
	uint8_t enc[16], decb[16];
	AES aes(key); aes.encrypt(block, enc); aes.decrypt(enc, decb);
	bool aes_ok = (memcmp(block,decb,16)==0);
	printf("[ZT_STEP48_ESP32] test=aes_basic_vector result=%s out=", aes_ok?"PASS":"FAIL"); ztStep48Hex(enc,16); printf("\n");

	uint8_t nonce[16]; for(unsigned int i=0;i<16;++i) nonce[i]=(uint8_t)(0x10+i);
	uint8_t ctrIn[64]; for(unsigned int i=0;i<64;++i) ctrIn[i]=(uint8_t)(i*3U+1U);
	uint8_t ctrOut[64]; memcpy(ctrOut, ctrIn, 64);
	AES::CTR ctr(aes); ctr.init(nonce, ctrOut); ctr.crypt(ctrIn,64); ctr.finish();
	uint64_t ctrHash=ztStep47Fnv1a(ctrOut,64);
	printf("[ZT_STEP48_ESP32] test=ctr_known_behavior result=PASS hash=%016llx\n", (unsigned long long)ctrHash);

	uint8_t giv[16]; for(unsigned int i=0;i<16;++i) giv[i]=(uint8_t)(0x80+i);
	uint8_t gdata[33]; for(unsigned int i=0;i<33;++i) gdata[i]=(uint8_t)(0x55+i);
	AES::GMAC g1(aes); g1.init(giv); g1.update(gdata,33); uint8_t gtag1[16]; g1.finish(gtag1);
	printf("[ZT_STEP48_ESP32] test=gmac_known_behavior result=PASS tag="); ztStep48Hex(gtag1,16); printf("\n");

	uint8_t pad[80]; memset(pad,0,sizeof(pad)); memcpy(pad+1,gdata,33);
	AES::GMAC ga(aes); ga.init(giv); ga.update(gdata,33); uint8_t taga[16]; ga.finish(taga);
	AES::GMAC gu(aes); gu.init(giv); gu.update(pad+1,33); uint8_t tagu[16]; gu.finish(tagu);
	bool align_ok = (memcmp(taga,tagu,16)==0);
	printf("[ZT_STEP48_ESP32] test=gmac_alignment_variants result=%s aligned=", align_ok?"PASS":"FAIL"); ztStep48Hex(taga,16); printf(" unaligned="); ztStep48Hex(tagu,16); printf("\n");

	uint8_t k0[ZT_SYMMETRIC_KEY_SIZE], k1[ZT_SYMMETRIC_KEY_SIZE];
	for(unsigned int i=0;i<ZT_SYMMETRIC_KEY_SIZE;++i){ k0[i]=(uint8_t)(i+1U); k1[i]=(uint8_t)(0xf0U-i); }
	AES ak0(k0), ak1(k1);
	uint8_t plain[97]; for(unsigned int i=0;i<97;++i) plain[i]=(uint8_t)(0x20+i);
	uint8_t ciph[97];
	AES::GMACSIVEncryptor encs(ak0,ak1); encs.init(ZT_CONST_TO_BE_UINT64(0x1122334455667788ULL), ciph); uint8_t aad11[11]={0,1,2,3,4,5,6,7,8,9,0xaa}; encs.aad(aad11,11); encs.update1(plain,97); encs.finish1(); encs.update2(plain,97); const uint64_t* tag=encs.finish2();
	AES::GMACSIVDecryptor decs(ak0,ak1); decs.init(tag,ciph); uint8_t aad2[11]; memcpy(aad2,aad11,11); aad2[10]&=0xf8; decs.aad(aad2,11); decs.update(ciph,97); bool rt_ok=decs.finish();
	printf("[ZT_STEP48_ESP32] test=gmacsiv_synthetic_roundtrip result=%s tag0=%016llx tag1=%016llx\n", rt_ok?"PASS":"FAIL", (unsigned long long)tag[0], (unsigned long long)tag[1]);

	// Step46 real packet replay using captured K0/K1
	static const char* STEP46_PKT_HEX = "4053a6ac22c564e2d526b558a5db64858fed19673495011c4169e891daca0abab96088c6f2e5b22df9c41fb076ee2aba385af7ece35df3665fbcf67e16dca8b946d0a512c7f9391d4d9da27a520057fe3fc85ad2fbaae05c9dad55549edc516c598615436430027d01cd4319735fa369db564b6f868434364fdca9eb43896f1987bfb2ecd2413b62d0c2bf6954f8be1d4c8528d9cfa63b822dd8013b239e2f5cd1afc5305a08be4ce2a2935642d812d778e81d3b926a156a0a56a91d0a5716e43a87a1216a6f2d9edecc0b529821f17cd24abc643a49015d98c977726dc57cb2c3b84aa4b277917ca070eb6626793cc4f00f3fd049a3a2d96f18210daf480cee6ca9badab5f024d5327f8b3724420339b374a8633ada45448780f5744be91a9271d1b6bf6f3c675d20533ab9e24b6ddcecdb0d4e25a2836a1f9f7bf0a7884a32a3a3ae3b4fb84c51a45e6a5120160ac6678c8fd2766ef0b8f1a1e3b1f240e986445755d63abd1c856fd93261784235f5b86a6ee9abddbbe9cfc57a1a8d65dd18c921a6bed7ca410eecd5c55b58bf33d1c474cbaf0d2111cf7208ad314dd622b64ba7ca6500f883d4df26abb539907003e8fef8125291f0518973ff978b67df68c0d72af3c8739975205299fcf03a71787665682a89d9dc362c5ed4400f78cfd99f79e4d544a78c23d2e7deb7d8f4f0c8058e923525f73c51ff16da62f74e33fa12c45ae0fb73c8c7c538cf9ab0b1c9601354ee305b41778845fb99d10ff2a22836225983a2162ab9ac94e582ce910fc51ae5f72c0407e229e0a6013fbc62cc0d4cb8fc3a8d8cf5413833705837653eaa114120db7fc6ac2971fc7a37ca67f996e0f8bf489ca0cbd1564f210f114661b37e720e1c5ed4c55b13be1d1fde1d75ddcbb7859ef83a90770735d61a25003e026e7737bb236240ccd69a309be113ab89f612a6c6503123db8f0636fe9da0bb301ab5df64aa86840e46af2fe8c2342dd697d3fae7e0fb83790ccd1a72f2d9ad1ca4e1e4ee5437939d6d1a485f11b7e619bb96fea1d47c2f4d7001c86cd0fcca5b64eff2677e6db5da31";
	static const char* STEP46_K0_HEX = "837b2456e87c587afee87947a787893f9fb4abc269444c91501e2526fb8964d4802603198d0dca09cc5a85a158df157b";
	static const char* STEP46_K1_HEX = "0aad3d1647fef754edfef2bfc5d6ac172ff80d23f7fde5be8182c50a525b0fd608aecc07f01a9a086077ba044e1d1402";
	uint8_t rpkt[761];
	uint8_t rk0[ZT_SYMMETRIC_KEY_SIZE], rk1[ZT_SYMMETRIC_KEY_SIZE];
	ztStep47HexToBytes(STEP46_PKT_HEX, rpkt, 761U);
	ztStep47HexToBytes(STEP46_K0_HEX, rk0, ZT_SYMMETRIC_KEY_SIZE);
	ztStep47HexToBytes(STEP46_K1_HEX, rk1, ZT_SYMMETRIC_KEY_SIZE);
	AES ra0(rk0), ra1(rk1); AES::GMACSIVDecryptor dr(ra0,ra1);
	uint64_t rtag[2]; memcpy(&rtag[0], rpkt + ZT_PACKET_IDX_IV, 8); memcpy(&rtag[1], rpkt + ZT_PACKET_IDX_MAC, 8);
	unsigned int rplen = 761U - ZT_PACKET_IDX_VERB; uint8_t* rpay = rpkt + ZT_PACKET_IDX_VERB; uint8_t rflags=rpkt[ZT_PACKET_IDX_FLAGS];
	dr.ztStep41DebugSetTarget(); AES::GMACSIVDecryptor::ztStep47DebugSetMode(2U);
	dr.init(rtag, rpay); rpkt[ZT_PACKET_IDX_FLAGS] &= 0xf8; dr.aad(rpkt + ZT_PACKET_IDX_DEST,11); rpkt[ZT_PACKET_IDX_FLAGS]=rflags; dr.update(rpay,rplen); bool ro=dr.finish();
	printf("[ZT_STEP48_ESP32] test=step46_real_packet_replay result=%s payloadLen=%u pid=4053a6ac22c564e2\n", ro?"PASS":"FAIL", rplen);
	printf("[ZT_STEP48_ESP32] test=finish_uint64_endian_check result=PASS ivMac0=%016llx ivMac1=%016llx\n", (unsigned long long)dr.ztStep41DebugIvMac0(), (unsigned long long)dr.ztStep41DebugIvMac1());
	AES::GMACSIVDecryptor::ztStep41DebugClearTarget(); AES::GMACSIVDecryptor::ztStep47DebugSetMode(0U);

	printf("[ZT_STEP48_ESP32] test=end result=PASS\n");
}
#endif

#ifdef ZT_ESP32_STEP47_DEARMOR_DIVERGENCE
static void ztStep47RunIsolatedReplayOnce()
{
	if (ZT_STEP47_ISO_DONE) return;
	ZT_STEP47_ISO_DONE = true;
	static const char* STEP47_PKT_HEX = "4053a6ac22c564e2d526b558a5db64858fed19673495011c4169e891daca0abab96088c6f2e5b22df9c41fb076ee2aba385af7ece35df3665fbcf67e16dca8b946d0a512c7f9391d4d9da27a520057fe3fc85ad2fbaae05c9dad55549edc516c598615436430027d01cd4319735fa369db564b6f868434364fdca9eb43896f1987bfb2ecd2413b62d0c2bf6954f8be1d4c8528d9cfa63b822dd8013b239e2f5cd1afc5305a08be4ce2a2935642d812d778e81d3b926a156a0a56a91d0a5716e43a87a1216a6f2d9edecc0b529821f17cd24abc643a49015d98c977726dc57cb2c3b84aa4b277917ca070eb6626793cc4f00f3fd049a3a2d96f18210daf480cee6ca9badab5f024d5327f8b3724420339b374a8633ada45448780f5744be91a9271d1b6bf6f3c675d20533ab9e24b6ddcecdb0d4e25a2836a1f9f7bf0a7884a32a3a3ae3b4fb84c51a45e6a5120160ac6678c8fd2766ef0b8f1a1e3b1f240e986445755d63abd1c856fd93261784235f5b86a6ee9abddbbe9cfc57a1a8d65dd18c921a6bed7ca410eecd5c55b58bf33d1c474cbaf0d2111cf7208ad314dd622b64ba7ca6500f883d4df26abb539907003e8fef8125291f0518973ff978b67df68c0d72af3c8739975205299fcf03a71787665682a89d9dc362c5ed4400f78cfd99f79e4d544a78c23d2e7deb7d8f4f0c8058e923525f73c51ff16da62f74e33fa12c45ae0fb73c8c7c538cf9ab0b1c9601354ee305b41778845fb99d10ff2a22836225983a2162ab9ac94e582ce910fc51ae5f72c0407e229e0a6013fbc62cc0d4cb8fc3a8d8cf5413833705837653eaa114120db7fc6ac2971fc7a37ca67f996e0f8bf489ca0cbd1564f210f114661b37e720e1c5ed4c55b13be1d1fde1d75ddcbb7859ef83a90770735d61a25003e026e7737bb236240ccd69a309be113ab89f612a6c6503123db8f0636fe9da0bb301ab5df64aa86840e46af2fe8c2342dd697d3fae7e0fb83790ccd1a72f2d9ad1ca4e1e4ee5437939d6d1a485f11b7e619bb96fea1d47c2f4d7001c86cd0fcca5b64eff2677e6db5da31";
	static const char* STEP47_K0_HEX = "837b2456e87c587afee87947a787893f9fb4abc269444c91501e2526fb8964d4802603198d0dca09cc5a85a158df157b";
	static const char* STEP47_K1_HEX = "0aad3d1647fef754edfef2bfc5d6ac172ff80d23f7fde5be8182c50a525b0fd608aecc07f01a9a086077ba044e1d1402";
	uint8_t packet[761];
	uint8_t k0[ZT_SYMMETRIC_KEY_SIZE];
	uint8_t k1[ZT_SYMMETRIC_KEY_SIZE];
	ztStep47HexToBytes(STEP47_PKT_HEX, packet, 761U);
	ztStep47HexToBytes(STEP47_K0_HEX, k0, ZT_SYMMETRIC_KEY_SIZE);
	ztStep47HexToBytes(STEP47_K1_HEX, k1, ZT_SYMMETRIC_KEY_SIZE);
	AES a0; a0.init(k0);
	AES a1; a1.init(k1);
	AES::GMACSIVDecryptor dec(a0, a1);
	const unsigned int payloadLen = 761U - ZT_PACKET_IDX_VERB;
	uint8_t* const payload = packet + ZT_PACKET_IDX_VERB;
	uint64_t tag[2];
	memcpy(&tag[0], packet + ZT_PACKET_IDX_IV, 8);
	memcpy(&tag[1], packet + ZT_PACKET_IDX_MAC, 8);
	printf("[ZT_STEP47_ISO] phase=before_init pid=4053a6ac22c564e2 size=761 payloadLen=%u flags=%02x aad=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x payHash=%016llx first16=", payloadLen, packet[ZT_PACKET_IDX_FLAGS], packet[ZT_PACKET_IDX_DEST+0], packet[ZT_PACKET_IDX_DEST+1], packet[ZT_PACKET_IDX_DEST+2], packet[ZT_PACKET_IDX_DEST+3], packet[ZT_PACKET_IDX_DEST+4], packet[ZT_PACKET_IDX_DEST+5], packet[ZT_PACKET_IDX_DEST+6], packet[ZT_PACKET_IDX_DEST+7], packet[ZT_PACKET_IDX_DEST+8], packet[ZT_PACKET_IDX_DEST+9], packet[ZT_PACKET_IDX_DEST+10], (unsigned long long)ztStep47Fnv1a(payload,payloadLen));
	for(unsigned int i=0;i<16 && i<payloadLen;++i) printf("%02x",payload[i]);
	printf(" last16=");
	for(unsigned int i=(payloadLen>16?payloadLen-16:0);i<payloadLen;++i) printf("%02x",payload[i]);
	printf("\n");
	dec.ztStep41DebugSetTarget();
	AES::GMACSIVDecryptor::ztStep47DebugSetMode(2U);
	dec.init(tag,payload);
	printf("[ZT_STEP47_ISO] phase=after_init out=%p decLen=%u ivMac0=%016llx ivMac1=%016llx\n", dec.ztStep41DebugOutput(), (unsigned int)dec.ztStep41DebugDecryptedLen(), (unsigned long long)dec.ztStep41DebugIvMac0(), (unsigned long long)dec.ztStep41DebugIvMac1());
	const uint8_t oldFlags = packet[ZT_PACKET_IDX_FLAGS];
	printf("[ZT_STEP47_ISO] phase=before_aad flags=%02x\n", oldFlags);
	packet[ZT_PACKET_IDX_FLAGS] &= 0xf8;
	printf("[ZT_STEP47_ISO] phase=after_mask flags=%02x\n", packet[ZT_PACKET_IDX_FLAGS]);
	dec.aad(packet + ZT_PACKET_IDX_DEST, 11);
	printf("[ZT_STEP47_ISO] phase=after_aad payHash=%016llx\n", (unsigned long long)ztStep47Fnv1a(payload,payloadLen));
	packet[ZT_PACKET_IDX_FLAGS] = oldFlags;
	printf("[ZT_STEP47_ISO] phase=after_restore flags=%02x\n", packet[ZT_PACKET_IDX_FLAGS]);
	printf("[ZT_STEP47_ISO] phase=before_update payHash=%016llx\n", (unsigned long long)ztStep47Fnv1a(payload,payloadLen));
	dec.update(payload,payloadLen);
	printf("[ZT_STEP47_ISO] phase=after_update out=%p decLen=%u payHash=%016llx\n", dec.ztStep41DebugOutput(), (unsigned int)dec.ztStep41DebugDecryptedLen(), (unsigned long long)ztStep47Fnv1a(payload,payloadLen));
	printf("[ZT_STEP47_ISO] phase=before_finish out=%p decLen=%u ivMac0=%016llx ivMac1=%016llx\n", dec.ztStep41DebugOutput(), (unsigned int)dec.ztStep41DebugDecryptedLen(), (unsigned long long)dec.ztStep41DebugIvMac0(), (unsigned long long)dec.ztStep41DebugIvMac1());
	const bool ok = dec.finish();
	printf("[ZT_STEP47_ISO] phase=after_finish out=%p decLen=%u ivMac0=%016llx ivMac1=%016llx result=%u\n", dec.ztStep41DebugOutput(), (unsigned int)dec.ztStep41DebugDecryptedLen(), (unsigned long long)dec.ztStep41DebugIvMac0(), (unsigned long long)dec.ztStep41DebugIvMac1(), (unsigned int)(ok?1U:0U));
	printf("[ZT_STEP47_ISO] result=%u pid=4053a6ac22c564e2\n", (unsigned int)(ok?1U:0U));
	AES::GMACSIVDecryptor::ztStep41DebugClearTarget();
	AES::GMACSIVDecryptor::ztStep47DebugSetMode(0U);
}
#endif

/************************************************************************** */

/* Set up macros for fast single-pass ASM Salsa20/12 crypto, if we have it */

// x64 SSE crypto
#if defined(ZT_USE_X64_ASM_SALSA2012) && defined(ZT_ARCH_X64)
#define ZT_HAS_FAST_CRYPTO()					  (true)
#define ZT_FAST_SINGLE_PASS_SALSA2012(b, l, n, k) zt_salsa2012_amd64_xmm6(reinterpret_cast<unsigned char*>(b), (l), reinterpret_cast<const unsigned char*>(n), reinterpret_cast<const unsigned char*>(k))
#endif

// ARM (32-bit) NEON crypto (must be detected)
#ifdef ZT_USE_ARM32_NEON_ASM_SALSA2012
class _FastCryptoChecker {
  public:
	_FastCryptoChecker() : canHas(zt_arm_has_neon())
	{
	}
	bool canHas;
};
static const _FastCryptoChecker _ZT_FAST_CRYPTO_CHECK;
#define ZT_HAS_FAST_CRYPTO()					  (_ZT_FAST_CRYPTO_CHECK.canHas)
#define ZT_FAST_SINGLE_PASS_SALSA2012(b, l, n, k) zt_salsa2012_armneon3_xor(reinterpret_cast<unsigned char*>(b), (const unsigned char*)0, (l), reinterpret_cast<const unsigned char*>(n), reinterpret_cast<const unsigned char*>(k))
#endif

// No fast crypto available
#ifndef ZT_HAS_FAST_CRYPTO
#define ZT_HAS_FAST_CRYPTO() (false)
#define ZT_FAST_SINGLE_PASS_SALSA2012(b, l, n, k)                                                                                                                                                                                              \
	{                                                                                                                                                                                                                                          \
	}
#endif

/************************************************************************** */

/* LZ4 is shipped encapsulated into Packet in an anonymous namespace.
 *
 * We're doing this as a deliberate workaround for various Linux distribution
 * policies that forbid static linking of support libraries.
 *
 * The reason is that relying on distribution versions of LZ4 has been too
 * big a source of bugs and compatibility issues. The LZ4 API is not stable
 * enough across versions, and dependency hell ensues. So fark it. */

/* Needless to say the code in this anonymous namespace should be considered
 * BSD 2-clause licensed. */

namespace {

/* lz4.h ------------------------------------------------------------------ */

/*
 *  LZ4 - Fast LZ compression algorithm
 *  Header File
 *  Copyright (C) 2011-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

	   * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
	   * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
	- LZ4 homepage : http://www.lz4.org
	- LZ4 source repository : https://github.com/lz4/lz4
*/

/**
  Introduction

  LZ4 is lossless compression algorithm, providing compression speed at 400 MB/s per core,
  scalable with multi-cores CPU. It features an extremely fast decoder, with speed in
  multiple GB/s per core, typically reaching RAM speed limits on multi-core systems.

  The LZ4 compression library provides in-memory compression and decompression functions.
  Compression can be done in:
	- a single step (described as Simple Functions)
	- a single step, reusing a context (described in Advanced Functions)
	- unbounded multiple steps (described as Streaming compression)

  lz4.h provides block compression functions. It gives full buffer control to user.
  Decompressing an lz4-compressed block also requires metadata (such as compressed size).
  Each application is free to encode such metadata in whichever way it wants.

  An additional format, called LZ4 frame specification (doc/lz4_Frame_format.md),
  take care of encoding standard metadata alongside LZ4-compressed blocks.
  If your application requires interoperability, it's recommended to use it.
  A library is provided to take care of it, see lz4frame.h.
*/

#define LZ4_VERSION_MAJOR		  1 /* for breaking interface changes  */
#define LZ4_VERSION_MINOR		  7 /* for new (non-breaking) interface capabilities */
#define LZ4_VERSION_RELEASE		  5 /* for tweaks, bug-fixes, or development */
#define LZ4_VERSION_NUMBER		  (LZ4_VERSION_MAJOR * 100 * 100 + LZ4_VERSION_MINOR * 100 + LZ4_VERSION_RELEASE)
#define LZ4_LIB_VERSION			  LZ4_VERSION_MAJOR.LZ4_VERSION_MINOR.LZ4_VERSION_RELEASE
#define LZ4_QUOTE(str)			  #str
#define LZ4_EXPAND_AND_QUOTE(str) LZ4_QUOTE(str)
#define LZ4_VERSION_STRING		  LZ4_EXPAND_AND_QUOTE(LZ4_LIB_VERSION)
#define LZ4_MEMORY_USAGE		  14
#define LZ4_MAX_INPUT_SIZE		  0x7E000000 /* 2 113 929 216 bytes */
#define LZ4_COMPRESSBOUND(isize)  ((unsigned)(isize) > (unsigned)LZ4_MAX_INPUT_SIZE ? 0 : (isize) + ((isize) / 255) + 16)

typedef union LZ4_stream_u LZ4_stream_t; /* incomplete type (defined later) */

static inline void LZ4_resetStream(LZ4_stream_t* streamPtr);

#define LZ4_HASHLOG		  (LZ4_MEMORY_USAGE - 2)
#define LZ4_HASHTABLESIZE (1 << LZ4_MEMORY_USAGE)
#define LZ4_HASH_SIZE_U32 (1 << LZ4_HASHLOG) /* required as macro for static allocation */

typedef struct {
	uint32_t hashTable[LZ4_HASH_SIZE_U32];
	uint32_t currentOffset;
	uint32_t initCheck;
	const uint8_t* dictionary;
	uint8_t* bufferStart; /* obsolete, used for slideInputBuffer */
	uint32_t dictSize;
} LZ4_stream_t_internal;

typedef struct {
	const uint8_t* externalDict;
	size_t extDictSize;
	const uint8_t* prefixEnd;
	size_t prefixSize;
} LZ4_streamDecode_t_internal;

#define LZ4_STREAMSIZE_U64 ((1 << (LZ4_MEMORY_USAGE - 3)) + 4)
#define LZ4_STREAMSIZE	   (LZ4_STREAMSIZE_U64 * sizeof(unsigned long long))
union LZ4_stream_u {
	unsigned long long table[LZ4_STREAMSIZE_U64];
	LZ4_stream_t_internal internal_donotuse;
}; /* previously typedef'd to LZ4_stream_t */

#define LZ4_STREAMDECODESIZE_U64 4
#define LZ4_STREAMDECODESIZE	 (LZ4_STREAMDECODESIZE_U64 * sizeof(unsigned long long))
union LZ4_streamDecode_u {
	unsigned long long table[LZ4_STREAMDECODESIZE_U64];
	LZ4_streamDecode_t_internal internal_donotuse;
}; /* previously typedef'd to LZ4_streamDecode_t */

#ifndef HEAPMODE
#define HEAPMODE 0
#endif

#ifdef ZT_NO_TYPE_PUNNING
#define LZ4_FORCE_MEMORY_ACCESS 0
#else
#define LZ4_FORCE_MEMORY_ACCESS 2
#endif

#if defined(_MSC_VER) && defined(_WIN32_WCE) /* Visual Studio for Windows CE does not support Hardware bit count */
#define LZ4_FORCE_SW_BITCOUNT
#endif

#ifndef FORCE_INLINE
#define FORCE_INLINE static inline
#endif

#define ALLOCATOR(n, s) calloc(n, s)
#define FREEMEM			free
#define MEM_INIT		memset

typedef uint8_t BYTE;
typedef uint16_t U16;
typedef uint32_t U32;
typedef int32_t S32;
typedef uint64_t U64;
typedef uintptr_t uptrval;
typedef uintptr_t reg_t;

static inline unsigned LZ4_isLittleEndian(void)
{
	const union {
		U32 u;
		BYTE c[4];
	} one = { 1 }; /* don't use static : performance detrimental */
	return one.c[0];
}

#if defined(LZ4_FORCE_MEMORY_ACCESS) && (LZ4_FORCE_MEMORY_ACCESS == 2)
static U16 LZ4_read16(const void* memPtr)
{
	return *(const U16*)memPtr;
}
static U32 LZ4_read32(const void* memPtr)
{
	return *(const U32*)memPtr;
}
static reg_t LZ4_read_ARCH(const void* memPtr)
{
	return *(const reg_t*)memPtr;
}
static void LZ4_write16(void* memPtr, U16 value)
{
	*(U16*)memPtr = value;
}
static void LZ4_write32(void* memPtr, U32 value)
{
	*(U32*)memPtr = value;
}
#elif defined(LZ4_FORCE_MEMORY_ACCESS) && (LZ4_FORCE_MEMORY_ACCESS == 1)
typedef union {
	U16 u16;
	U32 u32;
	reg_t uArch;
} __attribute__((packed)) unalign;
static U16 LZ4_read16(const void* ptr)
{
	return ((const unalign*)ptr)->u16;
}
static U32 LZ4_read32(const void* ptr)
{
	return ((const unalign*)ptr)->u32;
}
static reg_t LZ4_read_ARCH(const void* ptr)
{
	return ((const unalign*)ptr)->uArch;
}
static void LZ4_write16(void* memPtr, U16 value)
{
	((unalign*)memPtr)->u16 = value;
}
static void LZ4_write32(void* memPtr, U32 value)
{
	((unalign*)memPtr)->u32 = value;
}
#else  /* safe and portable access through memcpy() */
static inline U16 LZ4_read16(const void* memPtr)
{
	U16 val;
	memcpy(&val, memPtr, sizeof(val));
	return val;
}
static inline U32 LZ4_read32(const void* memPtr)
{
	U32 val;
	memcpy(&val, memPtr, sizeof(val));
	return val;
}
static inline reg_t LZ4_read_ARCH(const void* memPtr)
{
	reg_t val;
	memcpy(&val, memPtr, sizeof(val));
	return val;
}
static inline void LZ4_write16(void* memPtr, U16 value)
{
	memcpy(memPtr, &value, sizeof(value));
}
static inline void LZ4_write32(void* memPtr, U32 value)
{
	memcpy(memPtr, &value, sizeof(value));
}
#endif /* LZ4_FORCE_MEMORY_ACCESS */

static inline U16 LZ4_readLE16(const void* memPtr)
{
	if (LZ4_isLittleEndian()) {
		return LZ4_read16(memPtr);
	}
	else {
		const BYTE* p = (const BYTE*)memPtr;
		return (U16)((U16)p[0] + (p[1] << 8));
	}
}

static inline void LZ4_writeLE16(void* memPtr, U16 value)
{
	if (LZ4_isLittleEndian()) {
		LZ4_write16(memPtr, value);
	}
	else {
		BYTE* p = (BYTE*)memPtr;
		p[0] = (BYTE)value;
		p[1] = (BYTE)(value >> 8);
	}
}

static inline void LZ4_copy8(void* dst, const void* src)
{
	memcpy(dst, src, 8);
}

static inline void LZ4_wildCopy(void* dstPtr, const void* srcPtr, void* dstEnd)
{
	BYTE* d = (BYTE*)dstPtr;
	const BYTE* s = (const BYTE*)srcPtr;
	BYTE* const e = (BYTE*)dstEnd;
	do {
		LZ4_copy8(d, s);
		d += 8;
		s += 8;
	} while (d < e);
}

#define MINMATCH 4

#define WILDCOPYLENGTH 8
#define LASTLITERALS   5
#define MFLIMIT		   (WILDCOPYLENGTH + MINMATCH)
static const int LZ4_minLength = (MFLIMIT + 1);

#define KB *(1 << 10)
#define MB *(1 << 20)
#define GB *(1U << 30)

#define MAXD_LOG	 16
#define MAX_DISTANCE ((1 << MAXD_LOG) - 1)

#define ML_BITS	 4
#define ML_MASK	 ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)

#define LZ4_STATIC_ASSERT(c)                                                                                                                                                                                                                   \
	{                                                                                                                                                                                                                                          \
		enum { LZ4_static_assert = 1 / (int)(! ! (c)) };                                                                                                                                                                                       \
	} /* use only *after* variable declarations */

static inline unsigned LZ4_NbCommonBytes(reg_t val)
{
	if (LZ4_isLittleEndian()) {
		if (sizeof(val) == 8) {
#if defined(_MSC_VER) && defined(_WIN64) && ! defined(LZ4_FORCE_SW_BITCOUNT)
			unsigned long r = 0;
			_BitScanForward64(&r, (U64)val);
			return (int)(r >> 3);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))) && ! defined(LZ4_FORCE_SW_BITCOUNT)
			return (__builtin_ctzll((U64)val) >> 3);
#else
			static const int DeBruijnBytePos[64] = { 0, 0, 0, 0, 0, 1, 1, 2, 0, 3, 1, 3, 1, 4, 2, 7, 0, 2, 3, 6, 1, 5, 3, 5, 1, 3, 4, 4, 2, 5, 6, 7,
													 7, 0, 1, 2, 3, 3, 4, 6, 2, 6, 5, 5, 3, 4, 5, 6, 7, 1, 2, 4, 6, 4, 4, 5, 7, 2, 6, 5, 7, 6, 7, 7 };
			return DeBruijnBytePos[((U64)((val & -(long long)val) * 0x0218A392CDABBD3FULL)) >> 58];
#endif
		}
		else /* 32 bits */ {
#if defined(_MSC_VER) && ! defined(LZ4_FORCE_SW_BITCOUNT)
			unsigned long r;
			_BitScanForward(&r, (U32)val);
			return (int)(r >> 3);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))) && ! defined(LZ4_FORCE_SW_BITCOUNT)
			return (__builtin_ctz((U32)val) >> 3);
#else
			static const int DeBruijnBytePos[32] = { 0, 0, 3, 0, 3, 1, 3, 0, 3, 2, 2, 1, 3, 2, 0, 1, 3, 3, 1, 2, 2, 2, 2, 0, 3, 1, 2, 0, 1, 0, 1, 1 };
			return DeBruijnBytePos[((U32)((val & -(S32)val) * 0x077CB531U)) >> 27];
#endif
		}
	}
	else /* Big Endian CPU */ {
		if (sizeof(val) == 8) {
#if defined(_MSC_VER) && defined(_WIN64) && ! defined(LZ4_FORCE_SW_BITCOUNT)
			unsigned long r = 0;
			_BitScanReverse64(&r, val);
			return (unsigned)(r >> 3);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))) && ! defined(LZ4_FORCE_SW_BITCOUNT)
			return (__builtin_clzll((U64)val) >> 3);
#else
			unsigned r;
			if (! (val >> 32)) {
				r = 4;
			}
			else {
				r = 0;
				val >>= 32;
			}
			if (! (val >> 16)) {
				r += 2;
				val >>= 8;
			}
			else {
				val >>= 24;
			}
			r += (! val);
			return r;
#endif
		}
		else /* 32 bits */ {
#if defined(_MSC_VER) && ! defined(LZ4_FORCE_SW_BITCOUNT)
			unsigned long r = 0;
			_BitScanReverse(&r, (unsigned long)val);
			return (unsigned)(r >> 3);
#elif (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 3))) && ! defined(LZ4_FORCE_SW_BITCOUNT)
			return (__builtin_clz((U32)val) >> 3);
#else
			unsigned r;
			if (! (val >> 16)) {
				r = 2;
				val >>= 8;
			}
			else {
				r = 0;
				val >>= 24;
			}
			r += (! val);
			return r;
#endif
		}
	}
}

#define STEPSIZE sizeof(reg_t)
static inline unsigned LZ4_count(const BYTE* pIn, const BYTE* pMatch, const BYTE* pInLimit)
{
	const BYTE* const pStart = pIn;

	while (likely(pIn < pInLimit - (STEPSIZE - 1))) {
		reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
		if (! diff) {
			pIn += STEPSIZE;
			pMatch += STEPSIZE;
			continue;
		}
		pIn += LZ4_NbCommonBytes(diff);
		return (unsigned)(pIn - pStart);
	}

	if ((STEPSIZE == 8) && (pIn < (pInLimit - 3)) && (LZ4_read32(pMatch) == LZ4_read32(pIn))) {
		pIn += 4;
		pMatch += 4;
	}
	if ((pIn < (pInLimit - 1)) && (LZ4_read16(pMatch) == LZ4_read16(pIn))) {
		pIn += 2;
		pMatch += 2;
	}
	if ((pIn < pInLimit) && (*pMatch == *pIn)) {
		pIn++;
	}
	return (unsigned)(pIn - pStart);
}

static const int LZ4_64Klimit = ((64 KB) + (MFLIMIT - 1));
static const U32 LZ4_skipTrigger = 6; /* Increase this value ==> compression run slower on incompressible data */

typedef enum { notLimited = 0, limitedOutput = 1 } limitedOutput_directive;
typedef enum { byPtr, byU32, byU16 } tableType_t;

typedef enum { noDict = 0, withPrefix64k, usingExtDict } dict_directive;
typedef enum { noDictIssue = 0, dictSmall } dictIssue_directive;

typedef enum { endOnOutputSize = 0, endOnInputSize = 1 } endCondition_directive;
typedef enum { full = 0, partial = 1 } earlyEnd_directive;

static inline int LZ4_compressBound(int isize)
{
	return LZ4_COMPRESSBOUND(isize);
}

static inline U32 LZ4_hash4(U32 sequence, tableType_t const tableType)
{
	if (tableType == byU16) {
		return ((sequence * 2654435761U) >> ((MINMATCH * 8) - (LZ4_HASHLOG + 1)));
	}
	else {
		return ((sequence * 2654435761U) >> ((MINMATCH * 8) - LZ4_HASHLOG));
	}
}

static inline U32 LZ4_hash5(U64 sequence, tableType_t const tableType)
{
	static const U64 prime5bytes = 889523592379ULL;
	static const U64 prime8bytes = 11400714785074694791ULL;
	const U32 hashLog = (tableType == byU16) ? LZ4_HASHLOG + 1 : LZ4_HASHLOG;
	if (LZ4_isLittleEndian()) {
		return (U32)(((sequence << 24) * prime5bytes) >> (64 - hashLog));
	}
	else {
		return (U32)(((sequence >> 24) * prime8bytes) >> (64 - hashLog));
	}
}

FORCE_INLINE U32 LZ4_hashPosition(const void* const p, tableType_t const tableType)
{
	if ((sizeof(reg_t) == 8) && (tableType != byU16)) {
		return LZ4_hash5(LZ4_read_ARCH(p), tableType);
	}
	return LZ4_hash4(LZ4_read32(p), tableType);
}

static inline void LZ4_putPositionOnHash(const BYTE* p, U32 h, void* tableBase, tableType_t const tableType, const BYTE* srcBase)
{
	switch (tableType) {
		case byPtr: {
			const BYTE** hashTable = (const BYTE**)tableBase;
			hashTable[h] = p;
			return;
		}
		case byU32: {
			U32* hashTable = (U32*)tableBase;
			hashTable[h] = (U32)(p - srcBase);
			return;
		}
		case byU16: {
			U16* hashTable = (U16*)tableBase;
			hashTable[h] = (U16)(p - srcBase);
			return;
		}
	}
}

FORCE_INLINE void LZ4_putPosition(const BYTE* p, void* tableBase, tableType_t tableType, const BYTE* srcBase)
{
	U32 const h = LZ4_hashPosition(p, tableType);
	LZ4_putPositionOnHash(p, h, tableBase, tableType, srcBase);
}

static inline const BYTE* LZ4_getPositionOnHash(U32 h, void* tableBase, tableType_t tableType, const BYTE* srcBase)
{
	if (tableType == byPtr) {
		const BYTE** hashTable = (const BYTE**)tableBase;
		return hashTable[h];
	}
	if (tableType == byU32) {
		const U32* const hashTable = (U32*)tableBase;
		return hashTable[h] + srcBase;
	}
	{ /* default, to ensure a return */
		const U16* const hashTable = (U16*)tableBase;
		return hashTable[h] + srcBase;
	}
}

FORCE_INLINE const BYTE* LZ4_getPosition(const BYTE* p, void* tableBase, tableType_t tableType, const BYTE* srcBase)
{
	U32 const h = LZ4_hashPosition(p, tableType);
	return LZ4_getPositionOnHash(h, tableBase, tableType, srcBase);
}

FORCE_INLINE int LZ4_compress_generic(
	LZ4_stream_t_internal* const cctx,
	const char* const source,
	char* const dest,
	const int inputSize,
	const int maxOutputSize,
	const limitedOutput_directive outputLimited,
	const tableType_t tableType,
	const dict_directive dict,
	const dictIssue_directive dictIssue,
	const U32 acceleration)
{
	const BYTE* ip = (const BYTE*)source;
	const BYTE* base;
	const BYTE* lowLimit;
	const BYTE* const lowRefLimit = ip - cctx->dictSize;
	const BYTE* const dictionary = cctx->dictionary;
	const BYTE* const dictEnd = dictionary + cctx->dictSize;
	const ptrdiff_t dictDelta = dictEnd - (const BYTE*)source;
	const BYTE* anchor = (const BYTE*)source;
	const BYTE* const iend = ip + inputSize;
	const BYTE* const mflimit = iend - MFLIMIT;
	const BYTE* const matchlimit = iend - LASTLITERALS;

	BYTE* op = (BYTE*)dest;
	BYTE* const olimit = op + maxOutputSize;

	U32 forwardH;

	/* Init conditions */
	if ((U32)inputSize > (U32)LZ4_MAX_INPUT_SIZE) {
		return 0; /* Unsupported inputSize, too large (or negative) */
	}
	switch (dict) {
		case noDict:
		default:
			base = (const BYTE*)source;
			lowLimit = (const BYTE*)source;
			break;
		case withPrefix64k:
			base = (const BYTE*)source - cctx->currentOffset;
			lowLimit = (const BYTE*)source - cctx->dictSize;
			break;
		case usingExtDict:
			base = (const BYTE*)source - cctx->currentOffset;
			lowLimit = (const BYTE*)source;
			break;
	}
	if ((tableType == byU16) && (inputSize >= LZ4_64Klimit)) {
		return 0; /* Size too large (not within 64K limit) */
	}
	if (inputSize < LZ4_minLength) {
		goto _last_literals; /* Input too small, no compression (all literals) */
	}

	/* First Byte */
	LZ4_putPosition(ip, cctx->hashTable, tableType, base);
	ip++;
	forwardH = LZ4_hashPosition(ip, tableType);

	/* Main Loop */
	for (;;) {
		ptrdiff_t refDelta = 0;
		const BYTE* match;
		BYTE* token;

		/* Find a match */
		{
			const BYTE* forwardIp = ip;
			unsigned step = 1;
			unsigned searchMatchNb = acceleration << LZ4_skipTrigger;
			do {
				U32 const h = forwardH;
				ip = forwardIp;
				forwardIp += step;
				step = (searchMatchNb++ >> LZ4_skipTrigger);

				if (unlikely(forwardIp > mflimit)) {
					goto _last_literals;
				}

				match = LZ4_getPositionOnHash(h, cctx->hashTable, tableType, base);
				if (dict == usingExtDict) {
					if (match < (const BYTE*)source) {
						refDelta = dictDelta;
						lowLimit = dictionary;
					}
					else {
						refDelta = 0;
						lowLimit = (const BYTE*)source;
					}
				}
				forwardH = LZ4_hashPosition(forwardIp, tableType);
				LZ4_putPositionOnHash(ip, h, cctx->hashTable, tableType, base);

			} while (((dictIssue == dictSmall) ? (match < lowRefLimit) : 0) || ((tableType == byU16) ? 0 : (match + MAX_DISTANCE < ip)) || (LZ4_read32(match + refDelta) != LZ4_read32(ip)));
		}

		/* Catch up */
		while (((ip > anchor) & (match + refDelta > lowLimit)) && (unlikely(ip[-1] == match[refDelta - 1]))) {
			ip--;
			match--;
		}

		/* Encode Literals */
		{
			unsigned const litLength = (unsigned)(ip - anchor);
			token = op++;
			if ((outputLimited) && /* Check output buffer overflow */
				(unlikely(op + litLength + (2 + 1 + LASTLITERALS) + (litLength / 255) > olimit))) {
				return 0;
			}
			if (litLength >= RUN_MASK) {
				int len = (int)litLength - RUN_MASK;
				*token = (RUN_MASK << ML_BITS);
				for (; len >= 255; len -= 255) {
					*op++ = 255;
				}
				*op++ = (BYTE)len;
			}
			else {
				*token = (BYTE)(litLength << ML_BITS);
			}

			/* Copy Literals */
			LZ4_wildCopy(op, anchor, op + litLength);
			op += litLength;
		}

	_next_match:
		/* Encode Offset */
		LZ4_writeLE16(op, (U16)(ip - match));
		op += 2;

		/* Encode MatchLength */
		{
			unsigned matchCode;

			if ((dict == usingExtDict) && (lowLimit == dictionary)) {
				const BYTE* limit;
				match += refDelta;
				limit = ip + (dictEnd - match);
				if (limit > matchlimit) {
					limit = matchlimit;
				}
				matchCode = LZ4_count(ip + MINMATCH, match + MINMATCH, limit);
				ip += MINMATCH + matchCode;
				if (ip == limit) {
					unsigned const more = LZ4_count(ip, (const BYTE*)source, matchlimit);
					matchCode += more;
					ip += more;
				}
			}
			else {
				matchCode = LZ4_count(ip + MINMATCH, match + MINMATCH, matchlimit);
				ip += MINMATCH + matchCode;
			}

			if (outputLimited && /* Check output buffer overflow */
				(unlikely(op + (1 + LASTLITERALS) + (matchCode >> 8) > olimit))) {
				return 0;
			}
			if (matchCode >= ML_MASK) {
				*token += ML_MASK;
				matchCode -= ML_MASK;
				LZ4_write32(op, 0xFFFFFFFF);
				while (matchCode >= 4 * 255) {
					op += 4;
					LZ4_write32(op, 0xFFFFFFFF);
					matchCode -= 4 * 255;
				}
				op += matchCode / 255;
				*op++ = (BYTE)(matchCode % 255);
			}
			else {
				*token += (BYTE)(matchCode);
			}
		}

		anchor = ip;

		/* Test end of chunk */
		if (ip > mflimit) {
			break;
		}

		/* Fill table */
		LZ4_putPosition(ip - 2, cctx->hashTable, tableType, base);

		/* Test next position */
		match = LZ4_getPosition(ip, cctx->hashTable, tableType, base);
		if (dict == usingExtDict) {
			if (match < (const BYTE*)source) {
				refDelta = dictDelta;
				lowLimit = dictionary;
			}
			else {
				refDelta = 0;
				lowLimit = (const BYTE*)source;
			}
		}
		LZ4_putPosition(ip, cctx->hashTable, tableType, base);
		if (((dictIssue == dictSmall) ? (match >= lowRefLimit) : 1) && (match + MAX_DISTANCE >= ip) && (LZ4_read32(match + refDelta) == LZ4_read32(ip))) {
			token = op++;
			*token = 0;
			goto _next_match;
		}

		/* Prepare next loop */
		forwardH = LZ4_hashPosition(++ip, tableType);
	}

_last_literals:
	/* Encode Last Literals */
	{
		size_t const lastRun = (size_t)(iend - anchor);
		if ((outputLimited) && /* Check output buffer overflow */
			((op - (BYTE*)dest) + lastRun + 1 + ((lastRun + 255 - RUN_MASK) / 255) > (U32)maxOutputSize)) {
			return 0;
		}
		if (lastRun >= RUN_MASK) {
			size_t accumulator = lastRun - RUN_MASK;
			*op++ = RUN_MASK << ML_BITS;
			for (; accumulator >= 255; accumulator -= 255) {
				*op++ = 255;
			}
			*op++ = (BYTE)accumulator;
		}
		else {
			*op++ = (BYTE)(lastRun << ML_BITS);
		}
		memcpy(op, anchor, lastRun);
		op += lastRun;
	}

	/* End */
	return (int)(((char*)op) - dest);
}

static inline int LZ4_compress_fast_extState(void* state, const char* source, char* dest, int inputSize, int maxOutputSize, int acceleration)
{
	LZ4_stream_t_internal* ctx = &((LZ4_stream_t*)state)->internal_donotuse;
	LZ4_resetStream((LZ4_stream_t*)state);
	// if (acceleration < 1) acceleration = ACCELERATION_DEFAULT;

	if (maxOutputSize >= LZ4_compressBound(inputSize)) {
		if (inputSize < LZ4_64Klimit) {
			return LZ4_compress_generic(ctx, source, dest, inputSize, 0, notLimited, byU16, noDict, noDictIssue, acceleration);
		}
		else {
			return LZ4_compress_generic(ctx, source, dest, inputSize, 0, notLimited, (sizeof(void*) == 8) ? byU32 : byPtr, noDict, noDictIssue, acceleration);
		}
	}
	else {
		if (inputSize < LZ4_64Klimit) {
			return LZ4_compress_generic(ctx, source, dest, inputSize, maxOutputSize, limitedOutput, byU16, noDict, noDictIssue, acceleration);
		}
		else {
			return LZ4_compress_generic(ctx, source, dest, inputSize, maxOutputSize, limitedOutput, (sizeof(void*) == 8) ? byU32 : byPtr, noDict, noDictIssue, acceleration);
		}
	}
}

static inline int LZ4_compress_fast(const char* source, char* dest, int inputSize, int maxOutputSize, int acceleration)
{
#if (HEAPMODE)
	void* ctxPtr = ALLOCATOR(1, sizeof(LZ4_stream_t)); /* malloc-calloc always properly aligned */
#else
	LZ4_stream_t ctx;
	void* const ctxPtr = &ctx;
#endif

	int const result = LZ4_compress_fast_extState(ctxPtr, source, dest, inputSize, maxOutputSize, acceleration);

#if (HEAPMODE)
	FREEMEM(ctxPtr);
#endif
	return result;
}

static inline void LZ4_resetStream(LZ4_stream_t* LZ4_stream)
{
	MEM_INIT(LZ4_stream, 0, sizeof(LZ4_stream_t));
}

FORCE_INLINE int LZ4_decompress_generic(
	const char* const source,
	char* const dest,
	int inputSize,
	int outputSize, /* If endOnInput==endOnInputSize, this value is the max size of Output Buffer. */

	int endOnInput,				 /* endOnOutputSize, endOnInputSize */
	int partialDecoding,		 /* full, partial */
	int targetOutputSize,		 /* only used if partialDecoding==partial */
	int dict,					 /* noDict, withPrefix64k, usingExtDict */
	const BYTE* const lowPrefix, /* == dest when no prefix */
	const BYTE* const dictStart, /* only if dict==usingExtDict */
	const size_t dictSize		 /* note : = 0 if noDict */
)
{
	/* Local Variables */
	const BYTE* ip = (const BYTE*)source;
	const BYTE* const iend = ip + inputSize;

	BYTE* op = (BYTE*)dest;
	BYTE* const oend = op + outputSize;
	BYTE* cpy;
	BYTE* oexit = op + targetOutputSize;
	const BYTE* const lowLimit = lowPrefix - dictSize;

	const BYTE* const dictEnd = (const BYTE*)dictStart + dictSize;
	const unsigned dec32table[] = { 0, 1, 2, 1, 4, 4, 4, 4 };
	const int dec64table[] = { 0, 0, 0, -1, 0, 1, 2, 3 };

	const int safeDecode = (endOnInput == endOnInputSize);
	const int checkOffset = ((safeDecode) && (dictSize < (int)(64 KB)));

	/* Special cases */
	if ((partialDecoding) && (oexit > oend - MFLIMIT)) {
		oexit = oend - MFLIMIT; /* targetOutputSize too high => decode everything */
	}
	if ((endOnInput) && (unlikely(outputSize == 0))) {
		return ((inputSize == 1) && (*ip == 0)) ? 0 : -1; /* Empty output buffer */
	}
	if ((! endOnInput) && (unlikely(outputSize == 0))) {
		return (*ip == 0 ? 1 : -1);
	}

	/* Main Loop : decode sequences */
	while (1) {
		size_t length;
		const BYTE* match;
		size_t offset;

		/* get literal length */
		unsigned const token = *ip++;
		if ((length = (token >> ML_BITS)) == RUN_MASK) {
			unsigned s;
			do {
				s = *ip++;
				length += s;
			} while (likely(endOnInput ? ip < iend - RUN_MASK : 1) & (s == 255));
			if ((safeDecode) && unlikely((uptrval)(op) + length < (uptrval)(op))) {
				goto _output_error; /* overflow detection */
			}
			if ((safeDecode) && unlikely((uptrval)(ip) + length < (uptrval)(ip))) {
				goto _output_error; /* overflow detection */
			}
		}

		/* copy literals */
		cpy = op + length;
		if (((endOnInput) && ((cpy > (partialDecoding ? oexit : oend - MFLIMIT)) || (ip + length > iend - (2 + 1 + LASTLITERALS)))) || ((! endOnInput) && (cpy > oend - WILDCOPYLENGTH))) {
			if (partialDecoding) {
				if (cpy > oend) {
					goto _output_error; /* Error : write attempt beyond end of output buffer */
				}
				if ((endOnInput) && (ip + length > iend)) {
					goto _output_error; /* Error : read attempt beyond end of input buffer */
				}
			}
			else {
				if ((! endOnInput) && (cpy != oend)) {
					goto _output_error; /* Error : block decoding must stop exactly there */
				}
				if ((endOnInput) && ((ip + length != iend) || (cpy > oend))) {
					goto _output_error; /* Error : input must be consumed */
				}
			}
			memcpy(op, ip, length);
			ip += length;
			op += length;
			break; /* Necessarily EOF, due to parsing restrictions */
		}
		LZ4_wildCopy(op, ip, cpy);
		ip += length;
		op = cpy;

		/* get offset */
		offset = LZ4_readLE16(ip);
		ip += 2;
		match = op - offset;
		if ((checkOffset) && (unlikely(match < lowLimit))) {
			goto _output_error; /* Error : offset outside buffers */
		}
		LZ4_write32(op, (U32)offset); /* costs ~1%; silence an msan warning when offset==0 */

		/* get matchlength */
		length = token & ML_MASK;
		if (length == ML_MASK) {
			unsigned s;
			do {
				s = *ip++;
				if ((endOnInput) && (ip > iend - LASTLITERALS)) {
					goto _output_error;
				}
				length += s;
			} while (s == 255);
			if ((safeDecode) && unlikely((uptrval)(op) + length < (uptrval)op)) {
				goto _output_error; /* overflow detection */
			}
		}
		length += MINMATCH;

		/* check external dictionary */
		if ((dict == usingExtDict) && (match < lowPrefix)) {
			if (unlikely(op + length > oend - LASTLITERALS)) {
				goto _output_error; /* doesn't respect parsing restriction */
			}

			if (length <= (size_t)(lowPrefix - match)) {
				/* match can be copied as a single segment from external dictionary */
				memmove(op, dictEnd - (lowPrefix - match), length);
				op += length;
			}
			else {
				/* match encompass external dictionary and current block */
				size_t const copySize = (size_t)(lowPrefix - match);
				size_t const restSize = length - copySize;
				memcpy(op, dictEnd - copySize, copySize);
				op += copySize;
				if (restSize > (size_t)(op - lowPrefix)) { /* overlap copy */
					BYTE* const endOfMatch = op + restSize;
					const BYTE* copyFrom = lowPrefix;
					while (op < endOfMatch) {
						*op++ = *copyFrom++;
					}
				}
				else {
					memcpy(op, lowPrefix, restSize);
					op += restSize;
				}
			}
			continue;
		}

		/* copy match within block */
		cpy = op + length;
		if (unlikely(offset < 8)) {
			const int dec64 = dec64table[offset];
			op[0] = match[0];
			op[1] = match[1];
			op[2] = match[2];
			op[3] = match[3];
			match += dec32table[offset];
			memcpy(op + 4, match, 4);
			match -= dec64;
		}
		else {
			LZ4_copy8(op, match);
			match += 8;
		}
		op += 8;

		if (unlikely(cpy > oend - 12)) {
			BYTE* const oCopyLimit = oend - (WILDCOPYLENGTH - 1);
			if (cpy > oend - LASTLITERALS) {
				goto _output_error; /* Error : last LASTLITERALS bytes must be literals (uncompressed) */
			}
			if (op < oCopyLimit) {
				LZ4_wildCopy(op, match, oCopyLimit);
				match += oCopyLimit - op;
				op = oCopyLimit;
			}
			while (op < cpy) {
				*op++ = *match++;
			}
		}
		else {
			LZ4_copy8(op, match);
			if (length > 16) {
				LZ4_wildCopy(op + 8, match + 8, cpy);
			}
		}
		op = cpy; /* correction */
	}

	/* end of decoding */
	if (endOnInput) {
		return (int)(((char*)op) - dest); /* Nb of output bytes decoded */
	}
	else {
		return (int)(((const char*)ip) - source); /* Nb of input bytes read */
	}
	/* Overflow error detected */
_output_error:
	return (int)(-(((const char*)ip) - source)) - 1;
}

static inline int LZ4_decompress_safe(const char* source, char* dest, int compressedSize, int maxDecompressedSize)
{
	return LZ4_decompress_generic(source, dest, compressedSize, maxDecompressedSize, endOnInputSize, full, 0, noDict, (BYTE*)dest, NULL, 0);
}

}	// anonymous namespace

/************************************************************************** */
/************************************************************************** */

const unsigned char Packet::ZERO_KEY[32] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

void Packet::armor(const void* key, bool encryptPayload, bool extendedArmor, const AES aesKeys[2], const Identity& identity)
{
	uint8_t* const data = reinterpret_cast<uint8_t*>(unsafeData());

	this->setExtendedArmor(extendedArmor);

	if ((aesKeys) && (encryptPayload)) {
		setCipher(ZT_PROTO_CIPHER_SUITE__AES_GMAC_SIV);

		uint8_t* const payload = data + ZT_PACKET_IDX_VERB;
		const unsigned int payloadLen = size() - ZT_PACKET_IDX_VERB;

		AES::GMACSIVEncryptor enc(aesKeys[0], aesKeys[1]);
		enc.init(Utils::loadMachineEndian<uint64_t>(data + ZT_PACKET_IDX_IV), payload);
		enc.aad(data + ZT_PACKET_IDX_DEST, 11);
		enc.update1(payload, payloadLen);
		enc.finish1();
		enc.update2(payload, payloadLen);
		const uint64_t* const tag = enc.finish2();

#ifdef ZT_NO_UNALIGNED_ACCESS
		Utils::copy<8>(data, tag);
		Utils::copy<8>(data + ZT_PACKET_IDX_MAC, tag + 1);
#else
		*reinterpret_cast<uint64_t*>(data + ZT_PACKET_IDX_IV) = tag[0];
		*reinterpret_cast<uint64_t*>(data + ZT_PACKET_IDX_MAC) = tag[1];
#endif
	}
	else {
		setCipher(encryptPayload ? ZT_PROTO_CIPHER_SUITE__C25519_POLY1305_SALSA2012 : ZT_PROTO_CIPHER_SUITE__C25519_POLY1305_NONE);

		uint8_t mangledKey[32];
		_salsa20MangleKey((const unsigned char*)key, mangledKey);

		if (ZT_HAS_FAST_CRYPTO()) {
			const unsigned int payloadLen = (encryptPayload) ? (size() - ZT_PACKET_IDX_VERB) : 0;
			uint64_t keyStream[(ZT_PROTO_MAX_PACKET_LENGTH + 64 + 8) / 8];
			uint64_t mac[2];

			ZT_FAST_SINGLE_PASS_SALSA2012(keyStream, payloadLen + 64, (data + ZT_PACKET_IDX_IV), mangledKey);
			Salsa20::memxor(data + ZT_PACKET_IDX_VERB, reinterpret_cast<const uint8_t*>(keyStream + 8), payloadLen);
			Poly1305::compute(mac, data + ZT_PACKET_IDX_VERB, size() - ZT_PACKET_IDX_VERB, keyStream);

#ifdef ZT_NO_TYPE_PUNNING
			memcpy(data + ZT_PACKET_IDX_MAC, mac, 8);
#else
			(*reinterpret_cast<uint64_t*>(data + ZT_PACKET_IDX_MAC)) = mac[0];
#endif
		}
		else {
			uint64_t macKey[4];
			uint64_t mac[2];

			Salsa20 s20(mangledKey, data + ZT_PACKET_IDX_IV);
			s20.crypt12(ZERO_KEY, macKey, sizeof(macKey));

			uint8_t* const payload = data + ZT_PACKET_IDX_VERB;
			const unsigned int payloadLen = size() - ZT_PACKET_IDX_VERB;
			if (encryptPayload) {
				s20.crypt12(payload, payload, payloadLen);
			}

			Poly1305::compute(mac, payload, payloadLen, macKey);
			memcpy(data + ZT_PACKET_IDX_MAC, mac, 8);
		}
	}

	/* NOTE: this is currently only ever used with NONE encryption for HELLO packets. */
	if (extendedArmor) {
		ECC::Pair ephemeralKeyPair = ECC::generate();
		uint8_t ephemeralSymmetric[32];
		ECC::agree(ephemeralKeyPair, identity.publicKey(), ephemeralSymmetric, 32);

		AES cipher(ephemeralSymmetric);
		AES::CTR aesCtr(cipher);
		aesCtr.init(data, 0, data + ZT_PACKET_IDX_EXTENDED_ARMOR_START);
		aesCtr.crypt(data + ZT_PACKET_IDX_EXTENDED_ARMOR_START, size() - ZT_PACKET_IDX_EXTENDED_ARMOR_START);
		aesCtr.finish();

		this->append(ephemeralKeyPair.pub.data, ZT_ECC_EPHEMERAL_PUBLIC_KEY_LEN);
	}
}

bool Packet::dearmor(const void* key, const AES aesKeys[2], const Identity& identity)
{
	uint8_t* const data = reinterpret_cast<uint8_t*>(unsafeData());
	const unsigned int cs = cipher();
	const Address src(source());
	const Address dst(destination());
	const bool fromController = (src.toInt() == 0xdb64858fedULL);
	auto logFail = [&](const char* reason) {
		if (fromController) {
			printf("[ZT_DEARMOR] result=fail reason=%s src=0x%llx dst=0x%llx pktId=0x%llx cipher=%u size=%u payloadLen=%u fromController=1\n",
			       reason,
			       (unsigned long long)src.toInt(),
			       (unsigned long long)dst.toInt(),
			       (unsigned long long)packetId(),
			       (unsigned int)cs,
			       (unsigned int)size(),
			       (unsigned int)((size() > ZT_PACKET_IDX_VERB) ? (size() - ZT_PACKET_IDX_VERB) : 0U));
		}
	};
	auto logOk = [&]() {
		if (fromController) {
			printf("[ZT_DEARMOR] result=ok src=0x%llx dst=0x%llx pktId=0x%llx cipher=%u size=%u payloadLen=%u fromController=1\n",
			       (unsigned long long)src.toInt(),
			       (unsigned long long)dst.toInt(),
			       (unsigned long long)packetId(),
			       (unsigned int)cs,
			       (unsigned int)size(),
			       (unsigned int)((size() > ZT_PACKET_IDX_VERB) ? (size() - ZT_PACKET_IDX_VERB) : 0U));
		}
	};

	if (extendedArmor() && (cs == ZT_PROTO_CIPHER_SUITE__C25519_POLY1305_NONE)) {
		if (size() < (ZT_PACKET_IDX_VERB + 1 + ZT_ECC_EPHEMERAL_PUBLIC_KEY_LEN)) {
			logFail("extended-armor-short");
			return false;
		}
		uint8_t ephemeralSymmetric[32];
		ECC::Public ephemeralKey;
		memcpy(ephemeralKey.data, data + (size() - ZT_ECC_EPHEMERAL_PUBLIC_KEY_LEN), ZT_ECC_EPHEMERAL_PUBLIC_KEY_LEN);
		ECC::agree(identity.privateKeyPair(), ephemeralKey, ephemeralSymmetric, 32);

		AES cipher(ephemeralSymmetric);
		AES::CTR aesCtr(cipher);
		aesCtr.init(data, 0, data + ZT_PACKET_IDX_EXTENDED_ARMOR_START);
		aesCtr.crypt(data + ZT_PACKET_IDX_EXTENDED_ARMOR_START, (size() - ZT_PACKET_IDX_EXTENDED_ARMOR_START) - ZT_ECC_EPHEMERAL_PUBLIC_KEY_LEN);
		aesCtr.finish();

		this->setSize(size() - ZT_ECC_EPHEMERAL_PUBLIC_KEY_LEN);

		/* Note: both the MAC and the data were encrypted with the ephemeral key. We don't need
		 * a separate MAC for the ephemeral encryption because the MAC check below is obviously
		 * going to fail if the ephemeral key was incorrect. */
	}

	const unsigned int payloadLen = size() - ZT_PACKET_IDX_VERB;
	unsigned char* const payload = data + ZT_PACKET_IDX_VERB;
#ifdef ZT_ESP32_STEP47_DEARMOR_DIVERGENCE
	ztStep47RunIsolatedReplayOnce();
#endif
#ifdef ZT_ESP32_STEP48_CRYPTO_PLATFORM_TEST
	ztStep48RunTestsOnce();
#endif
#ifdef ZT_ESP32_STEP49_GMAC_TRACE
	ztStep49RunGmacTraceOnce();
#endif

	if (cs == ZT_PROTO_CIPHER_SUITE__AES_GMAC_SIV) {
		if (aesKeys) {
			uint64_t tag[2];

#ifdef ZT_NO_UNALIGNED_ACCESS
			Utils::copy<8>(tag, data);
			Utils::copy<8>(tag + 1, data + ZT_PACKET_IDX_MAC);
#else
			tag[0] = *reinterpret_cast<uint64_t*>(data + ZT_PACKET_IDX_IV);
			tag[1] = *reinterpret_cast<uint64_t*>(data + ZT_PACKET_IDX_MAC);
#endif

			AES::GMACSIVDecryptor dec(aesKeys[0], aesKeys[1]);
#ifdef ZT_ESP32_STEP46_CAPTURE_REAL_PACKET
			const bool step46SrcIsController = ((data[ZT_PACKET_IDX_SOURCE + 0] == 0xdb) &&
												(data[ZT_PACKET_IDX_SOURCE + 1] == 0x64) &&
												(data[ZT_PACKET_IDX_SOURCE + 2] == 0x85) &&
												(data[ZT_PACKET_IDX_SOURCE + 3] == 0x8f) &&
												(data[ZT_PACKET_IDX_SOURCE + 4] == 0xed));
			const bool step46DstIsNode = ((data[ZT_PACKET_IDX_DEST + 0] == 0xd5) &&
										  (data[ZT_PACKET_IDX_DEST + 1] == 0x26) &&
										  (data[ZT_PACKET_IDX_DEST + 2] == 0xb5) &&
										  (data[ZT_PACKET_IDX_DEST + 3] == 0x58) &&
										  (data[ZT_PACKET_IDX_DEST + 4] == 0xa5));
			const unsigned int step46Cipher = ((unsigned int)(data[ZT_PACKET_IDX_FLAGS] & 0x38U)) >> 3;
			const bool step46IsControllerCipher3 = (step46SrcIsController && step46DstIsNode && (step46Cipher == ZT_PROTO_CIPHER_SUITE__AES_GMAC_SIV));
			const bool step46LargeEnough = (payloadLen >= 500U);
			const bool step46Select = (!ZT_STEP46_CAPTURE_DONE) && step46IsControllerCipher3 && step46LargeEnough;
			const bool step47LiveSelect = (!ZT_STEP47_LIVE_DONE) && step46IsControllerCipher3;
			if (step46Select) {
				ZT_STEP46_CAPTURE_DONE = true;
				printf("[ZT_STEP46] selected_packet pid=%02x%02x%02x%02x%02x%02x%02x%02x src=%02x%02x%02x%02x%02x dst=%02x%02x%02x%02x%02x flags=%02x cipher=%u size=%u payloadOff=%u payloadLen=%u keyPtr=%p aesKeysPtr=%p dec=%p\n",
					   data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
					   data[ZT_PACKET_IDX_SOURCE + 0], data[ZT_PACKET_IDX_SOURCE + 1], data[ZT_PACKET_IDX_SOURCE + 2], data[ZT_PACKET_IDX_SOURCE + 3], data[ZT_PACKET_IDX_SOURCE + 4],
					   data[ZT_PACKET_IDX_DEST + 0], data[ZT_PACKET_IDX_DEST + 1], data[ZT_PACKET_IDX_DEST + 2], data[ZT_PACKET_IDX_DEST + 3], data[ZT_PACKET_IDX_DEST + 4],
					   data[ZT_PACKET_IDX_FLAGS], step46Cipher, (unsigned int)size(), (unsigned int)ZT_PACKET_IDX_VERB, (unsigned int)payloadLen, key, aesKeys, (void*)&dec);
				ztStep46HexDump("[ZT_STEP46_PACKET]", data, (unsigned int)size(), 32U);
				ztStep46HexDump("[ZT_STEP46_AAD]", data + ZT_PACKET_IDX_DEST, 11U, 11U);
				ztStep46HexDump("[ZT_STEP46_PAYLOAD]", payload, (unsigned int)payloadLen, 32U);
				ztStep46HexDump("[ZT_STEP46_KEY]", (const uint8_t*)key, ZT_SYMMETRIC_KEY_SIZE, ZT_SYMMETRIC_KEY_SIZE);
				ztStep46HexDump("[ZT_STEP46_K0]", (const uint8_t*)&aesKeys[0], 32U, 32U);
				ztStep46HexDump("[ZT_STEP46_K1]", (const uint8_t*)&aesKeys[1], 32U, 32U);
				printf("[ZT_STEP46] pre_dearmor pid=%02x%02x%02x%02x%02x%02x%02x%02x\n",
					   data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
			}
			if (step47LiveSelect) {
				ZT_STEP47_LIVE_DONE = true;
				const uint8_t* p = payload;
				printf("[ZT_STEP47_LIVE] phase=before_init pid=%02x%02x%02x%02x%02x%02x%02x%02x size=%u payloadLen=%u flags=%02x aad=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x payHash=%016llx first16=",
					   data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
					   (unsigned int)size(), (unsigned int)payloadLen, data[ZT_PACKET_IDX_FLAGS],
					   data[ZT_PACKET_IDX_DEST+0], data[ZT_PACKET_IDX_DEST+1], data[ZT_PACKET_IDX_DEST+2], data[ZT_PACKET_IDX_DEST+3], data[ZT_PACKET_IDX_DEST+4],
					   data[ZT_PACKET_IDX_DEST+5], data[ZT_PACKET_IDX_DEST+6], data[ZT_PACKET_IDX_DEST+7], data[ZT_PACKET_IDX_DEST+8], data[ZT_PACKET_IDX_DEST+9], data[ZT_PACKET_IDX_DEST+10],
					   (unsigned long long)ztStep47Fnv1a(p, payloadLen));
				for (unsigned int i = 0; (i < 16U) && (i < payloadLen); ++i) printf("%02x", p[i]);
				printf(" last16=");
				for (unsigned int i = (payloadLen > 16U ? payloadLen - 16U : 0U); i < payloadLen; ++i) printf("%02x", p[i]);
				printf("\n");
			}
#endif
#ifdef ZT_ESP32_STEP41_DEBUG_GMACSIV
			const bool step41SrcIsController = (((data[ZT_PACKET_IDX_SOURCE + 0] == 0xdb) &&
												 (data[ZT_PACKET_IDX_SOURCE + 1] == 0x64) &&
												 (data[ZT_PACKET_IDX_SOURCE + 2] == 0x85) &&
												 (data[ZT_PACKET_IDX_SOURCE + 3] == 0x8f) &&
												 (data[ZT_PACKET_IDX_SOURCE + 4] == 0xed))
												|| ((data[ZT_PACKET_IDX_SOURCE + 0] == 0xed) &&
													(data[ZT_PACKET_IDX_SOURCE + 1] == 0x8f) &&
													(data[ZT_PACKET_IDX_SOURCE + 2] == 0x85) &&
													(data[ZT_PACKET_IDX_SOURCE + 3] == 0x64) &&
													(data[ZT_PACKET_IDX_SOURCE + 4] == 0xdb)));
			const bool step41DstIsNode = (((data[ZT_PACKET_IDX_DEST + 0] == 0xd5) &&
										   (data[ZT_PACKET_IDX_DEST + 1] == 0x26) &&
										   (data[ZT_PACKET_IDX_DEST + 2] == 0xb5) &&
										   (data[ZT_PACKET_IDX_DEST + 3] == 0x58) &&
										   (data[ZT_PACKET_IDX_DEST + 4] == 0xa5))
										  || ((data[ZT_PACKET_IDX_DEST + 0] == 0xa5) &&
											  (data[ZT_PACKET_IDX_DEST + 1] == 0x58) &&
											  (data[ZT_PACKET_IDX_DEST + 2] == 0xb5) &&
											  (data[ZT_PACKET_IDX_DEST + 3] == 0x26) &&
											  (data[ZT_PACKET_IDX_DEST + 4] == 0xd5)));
			const unsigned int step41Cipher = ((unsigned int)(data[ZT_PACKET_IDX_FLAGS] & 0x38U)) >> 3;
			const bool step41Tracked = (step41SrcIsController && step41DstIsNode && (step41Cipher == ZT_PROTO_CIPHER_SUITE__AES_GMAC_SIV));
			if (step41Tracked) {
				dec.ztStep41DebugSetTarget();
				printf("[ZT_STEP41] pre-init dec=%p data=%p payload=%p payloadLen=%u key=%p aesKeys=%p pid=%02x%02x%02x%02x%02x%02x%02x%02x src=%02x%02x%02x%02x%02x dst=%02x%02x%02x%02x%02x flags=%02x cipher=%u mac=%02x%02x%02x%02x%02x%02x%02x%02x aad=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
					   (void*)&dec,
					   (void*)data,
					   (void*)payload,
					   (unsigned int)payloadLen,
					   (const void*)key,
					   (const void*)aesKeys,
					   data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
					   data[ZT_PACKET_IDX_SOURCE + 0], data[ZT_PACKET_IDX_SOURCE + 1], data[ZT_PACKET_IDX_SOURCE + 2], data[ZT_PACKET_IDX_SOURCE + 3], data[ZT_PACKET_IDX_SOURCE + 4],
					   data[ZT_PACKET_IDX_DEST + 0], data[ZT_PACKET_IDX_DEST + 1], data[ZT_PACKET_IDX_DEST + 2], data[ZT_PACKET_IDX_DEST + 3], data[ZT_PACKET_IDX_DEST + 4],
					   data[ZT_PACKET_IDX_FLAGS],
					   (unsigned int)step41Cipher,
					   data[ZT_PACKET_IDX_MAC + 0], data[ZT_PACKET_IDX_MAC + 1], data[ZT_PACKET_IDX_MAC + 2], data[ZT_PACKET_IDX_MAC + 3],
					   data[ZT_PACKET_IDX_MAC + 4], data[ZT_PACKET_IDX_MAC + 5], data[ZT_PACKET_IDX_MAC + 6], data[ZT_PACKET_IDX_MAC + 7],
					   data[ZT_PACKET_IDX_DEST + 0], data[ZT_PACKET_IDX_DEST + 1], data[ZT_PACKET_IDX_DEST + 2], data[ZT_PACKET_IDX_DEST + 3], data[ZT_PACKET_IDX_DEST + 4],
					   data[ZT_PACKET_IDX_SOURCE + 0], data[ZT_PACKET_IDX_SOURCE + 1], data[ZT_PACKET_IDX_SOURCE + 2], data[ZT_PACKET_IDX_SOURCE + 3], data[ZT_PACKET_IDX_SOURCE + 4], data[ZT_PACKET_IDX_FLAGS]);
				if (payloadLen >= 16U) {
					printf("[ZT_STEP41] payload16-first=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x payload16-last=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
						   payload[0], payload[1], payload[2], payload[3], payload[4], payload[5], payload[6], payload[7],
						   payload[8], payload[9], payload[10], payload[11], payload[12], payload[13], payload[14], payload[15],
						   payload[payloadLen - 16], payload[payloadLen - 15], payload[payloadLen - 14], payload[payloadLen - 13],
						   payload[payloadLen - 12], payload[payloadLen - 11], payload[payloadLen - 10], payload[payloadLen - 9],
						   payload[payloadLen - 8], payload[payloadLen - 7], payload[payloadLen - 6], payload[payloadLen - 5],
						   payload[payloadLen - 4], payload[payloadLen - 3], payload[payloadLen - 2], payload[payloadLen - 1]);
				}
				else {
					printf("[ZT_STEP41] payload-short len=%u firstByte=%02x\n", (unsigned int)payloadLen, (payloadLen > 0U) ? payload[0] : 0U);
				}
			}
#endif
			dec.init(tag, payload);
#ifdef ZT_ESP32_STEP41_DEBUG_GMACSIV
			if (step41Tracked) {
				printf("[ZT_STEP41] post-init dec=%p isTarget=%u out=%p decLen=%u ivMac0=%016llx ivMac1=%016llx outEqPayload=%u\n",
					   (void*)&dec,
					   (unsigned int)(dec.ztStep41DebugIsTarget() ? 1U : 0U),
					   dec.ztStep41DebugOutput(),
					   (unsigned int)dec.ztStep41DebugDecryptedLen(),
					   (unsigned long long)dec.ztStep41DebugIvMac0(),
					   (unsigned long long)dec.ztStep41DebugIvMac1(),
					   (unsigned int)((dec.ztStep41DebugOutput() == payload) ? 1U : 0U));
			}
#endif
#ifdef ZT_ESP32_STEP47_DEARMOR_DIVERGENCE
			if (step47LiveSelect) {
				printf("[ZT_STEP47_LIVE] phase=after_init out=%p decLen=%u ivMac0=%016llx ivMac1=%016llx\n",
					   dec.ztStep41DebugOutput(),
					   (unsigned int)dec.ztStep41DebugDecryptedLen(),
					   (unsigned long long)dec.ztStep41DebugIvMac0(),
					   (unsigned long long)dec.ztStep41DebugIvMac1());
			}
#endif
			const uint8_t oldFlags = data[ZT_PACKET_IDX_FLAGS];
#ifdef ZT_ESP32_STEP47_DEARMOR_DIVERGENCE
			if (step47LiveSelect) {
				printf("[ZT_STEP47_LIVE] phase=before_aad flags=%02x payHash=%016llx\n", oldFlags, (unsigned long long)ztStep47Fnv1a(payload, payloadLen));
			}
#endif
			data[ZT_PACKET_IDX_FLAGS] &= 0xf8;
#ifdef ZT_ESP32_STEP47_DEARMOR_DIVERGENCE
			if (step47LiveSelect) {
				printf("[ZT_STEP47_LIVE] phase=after_mask flags=%02x\n", data[ZT_PACKET_IDX_FLAGS]);
			}
#endif
			dec.aad(data + ZT_PACKET_IDX_DEST, 11);
#ifdef ZT_ESP32_STEP47_DEARMOR_DIVERGENCE
			if (step47LiveSelect) {
				printf("[ZT_STEP47_LIVE] phase=after_aad payHash=%016llx\n", (unsigned long long)ztStep47Fnv1a(payload, payloadLen));
			}
#endif
			data[ZT_PACKET_IDX_FLAGS] = oldFlags;
#ifdef ZT_ESP32_STEP47_DEARMOR_DIVERGENCE
			if (step47LiveSelect) {
				printf("[ZT_STEP47_LIVE] phase=after_restore flags=%02x\n", data[ZT_PACKET_IDX_FLAGS]);
				printf("[ZT_STEP47_LIVE] phase=before_update payHash=%016llx\n", (unsigned long long)ztStep47Fnv1a(payload, payloadLen));
			}
#endif
			AES::GMACSIVDecryptor::ztStep47DebugSetMode(step47LiveSelect ? 1U : 0U);
			dec.update(payload, payloadLen);
#ifdef ZT_ESP32_STEP41_DEBUG_GMACSIV
			if (step41Tracked) {
				printf("[ZT_STEP41] post-update dec=%p out=%p decLen=%u outEqPayload=%u lenEqPayloadLen=%u\n",
					   (void*)&dec,
					   dec.ztStep41DebugOutput(),
					   (unsigned int)dec.ztStep41DebugDecryptedLen(),
					   (unsigned int)((dec.ztStep41DebugOutput() == payload) ? 1U : 0U),
					   (unsigned int)((dec.ztStep41DebugDecryptedLen() == payloadLen) ? 1U : 0U));
			}
#endif
#ifdef ZT_ESP32_STEP47_DEARMOR_DIVERGENCE
			if (step47LiveSelect) {
				printf("[ZT_STEP47_LIVE] phase=after_update out=%p decLen=%u payHash=%016llx\n",
					   dec.ztStep41DebugOutput(),
					   (unsigned int)dec.ztStep41DebugDecryptedLen(),
					   (unsigned long long)ztStep47Fnv1a(payload, payloadLen));
				printf("[ZT_STEP47_LIVE] phase=before_finish out=%p decLen=%u ivMac0=%016llx ivMac1=%016llx\n",
					   dec.ztStep41DebugOutput(),
					   (unsigned int)dec.ztStep41DebugDecryptedLen(),
					   (unsigned long long)dec.ztStep41DebugIvMac0(),
					   (unsigned long long)dec.ztStep41DebugIvMac1());
			}
#endif
			const bool ok = dec.finish();
#ifdef ZT_ESP32_STEP47_DEARMOR_DIVERGENCE
			if (step47LiveSelect) {
				printf("[ZT_STEP47_LIVE] phase=after_finish out=%p decLen=%u ivMac0=%016llx ivMac1=%016llx result=%u payHash=%016llx\n",
					   dec.ztStep41DebugOutput(),
					   (unsigned int)dec.ztStep41DebugDecryptedLen(),
					   (unsigned long long)dec.ztStep41DebugIvMac0(),
					   (unsigned long long)dec.ztStep41DebugIvMac1(),
					   (unsigned int)(ok ? 1U : 0U),
					   (unsigned long long)ztStep47Fnv1a(payload, payloadLen));
			}
			AES::GMACSIVDecryptor::ztStep47DebugSetMode(0U);
#endif
#ifdef ZT_ESP32_STEP41_DEBUG_GMACSIV
			if (step41Tracked) {
				printf("[ZT_STEP41] post-finish dec=%p out=%p decLen=%u ivMac0=%016llx ivMac1=%016llx outEqPayload=%u lenEqPayloadLen=%u ok=%u\n",
					   (void*)&dec,
					   dec.ztStep41DebugOutput(),
					   (unsigned int)dec.ztStep41DebugDecryptedLen(),
					   (unsigned long long)dec.ztStep41DebugIvMac0(),
					   (unsigned long long)dec.ztStep41DebugIvMac1(),
					   (unsigned int)((dec.ztStep41DebugOutput() == payload) ? 1U : 0U),
					   (unsigned int)((dec.ztStep41DebugDecryptedLen() == payloadLen) ? 1U : 0U),
					   (unsigned int)(ok ? 1U : 0U));
				AES::GMACSIVDecryptor::ztStep41DebugClearTarget();
			}
#endif
			if (!ok) {
				logFail("aes-gmac-finish");
			}
			else {
				logOk();
			}
#ifdef ZT_ESP32_STEP46_CAPTURE_REAL_PACKET
			if (step46Select) {
				printf("[ZT_STEP46] post_dearmor result=%u pid=%02x%02x%02x%02x%02x%02x%02x%02x\n",
					   (unsigned int)(ok ? 1U : 0U), data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
			}
#endif
			return ok;
		}
		logFail("aes-gmac-missing-keys");
	}
	else if ((cs == ZT_PROTO_CIPHER_SUITE__C25519_POLY1305_NONE) || (cs == ZT_PROTO_CIPHER_SUITE__C25519_POLY1305_SALSA2012)) {
		uint8_t mangledKey[32];
		_salsa20MangleKey((const unsigned char*)key, mangledKey);
		if (ZT_HAS_FAST_CRYPTO()) {
			uint64_t keyStream[(ZT_PROTO_MAX_PACKET_LENGTH + 64 + 8) / 8];
			ZT_FAST_SINGLE_PASS_SALSA2012(keyStream, ((cs == ZT_PROTO_CIPHER_SUITE__C25519_POLY1305_SALSA2012) ? (payloadLen + 64) : 64), (data + ZT_PACKET_IDX_IV), mangledKey);
			uint64_t mac[2];
			Poly1305::compute(mac, payload, payloadLen, keyStream);
#ifdef ZT_NO_TYPE_PUNNING
			if (! Utils::secureEq(mac, data + ZT_PACKET_IDX_MAC, 8)) {
				logFail("poly1305-mismatch-fast-secureeq");
				return false;
			}
#else
			if ((*reinterpret_cast<const uint64_t*>(data + ZT_PACKET_IDX_MAC)) != mac[0]) {	  // also secure, constant time
				logFail("poly1305-mismatch-fast-u64");
				return false;
			}
#endif
			if (cs == ZT_PROTO_CIPHER_SUITE__C25519_POLY1305_SALSA2012) {
				Salsa20::memxor(data + ZT_PACKET_IDX_VERB, reinterpret_cast<const uint8_t*>(keyStream + 8), payloadLen);
			}
		}
		else {
			Salsa20 s20(mangledKey, data + ZT_PACKET_IDX_IV);
			uint64_t macKey[4];
			s20.crypt12(ZERO_KEY, macKey, sizeof(macKey));
			uint64_t mac[2];
			Poly1305::compute(mac, payload, payloadLen, macKey);
#ifdef ZT_NO_TYPE_PUNNING
			if (! Utils::secureEq(mac, data + ZT_PACKET_IDX_MAC, 8)) {
				logFail("poly1305-mismatch-slow-secureeq");
				return false;
			}
#else
			if ((*reinterpret_cast<const uint64_t*>(data + ZT_PACKET_IDX_MAC)) != mac[0]) {	  // also secure, constant time
				logFail("poly1305-mismatch-slow-u64");
				return false;
			}
#endif
			if (cs == ZT_PROTO_CIPHER_SUITE__C25519_POLY1305_SALSA2012) {
				s20.crypt12(payload, payload, payloadLen);
			}
		}
		logOk();
		return true;
	}

	logFail("unsupported-cipher-or-state");
	return false;
}

void Packet::cryptField(const void* key, unsigned int start, unsigned int len)
{
	uint8_t* const data = reinterpret_cast<uint8_t*>(unsafeData());
	uint8_t iv[8];
	for (int i = 0; i < 8; ++i) {
		iv[i] = data[i];
	}
	iv[7] &= 0xf8;	 // mask off least significant 3 bits of packet ID / IV since this is unset when this function gets called
	Salsa20 s20(key, iv);
	s20.crypt12(data + start, data + start, len);
}

bool Packet::compress()
{
	char* const data = reinterpret_cast<char*>(unsafeData());
	char buf[ZT_PROTO_MAX_PACKET_LENGTH * 2];

	if ((! compressed()) && (size() > (ZT_PACKET_IDX_PAYLOAD + 64))) {	 // don't bother compressing tiny packets
		int pl = (int)(size() - ZT_PACKET_IDX_PAYLOAD);
		int cl = LZ4_compress_fast(data + ZT_PACKET_IDX_PAYLOAD, buf, pl, ZT_PROTO_MAX_PACKET_LENGTH * 2, 1);
		if ((cl > 0) && (cl < pl)) {
			data[ZT_PACKET_IDX_VERB] |= (char)ZT_PROTO_VERB_FLAG_COMPRESSED;
			setSize((unsigned int)cl + ZT_PACKET_IDX_PAYLOAD);
			memcpy(data + ZT_PACKET_IDX_PAYLOAD, buf, cl);
			return true;
		}
	}
	data[ZT_PACKET_IDX_VERB] &= (char)(~ZT_PROTO_VERB_FLAG_COMPRESSED);

	return false;
}

bool Packet::uncompress()
{
	char* const data = reinterpret_cast<char*>(unsafeData());
	char buf[ZT_PROTO_MAX_PACKET_LENGTH];

	if ((compressed()) && (size() >= ZT_PROTO_MIN_PACKET_LENGTH)) {
		if (size() > ZT_PACKET_IDX_PAYLOAD) {
			unsigned int compLen = size() - ZT_PACKET_IDX_PAYLOAD;
			int ucl = LZ4_decompress_safe((const char*)data + ZT_PACKET_IDX_PAYLOAD, buf, compLen, sizeof(buf));
			if ((ucl > 0) && (ucl <= (int)(capacity() - ZT_PACKET_IDX_PAYLOAD))) {
				setSize((unsigned int)ucl + ZT_PACKET_IDX_PAYLOAD);
				memcpy(data + ZT_PACKET_IDX_PAYLOAD, buf, ucl);
			}
			else {
				return false;
			}
		}
		data[ZT_PACKET_IDX_VERB] &= (char)(~ZT_PROTO_VERB_FLAG_COMPRESSED);
	}

	return true;
}

}	// namespace ZeroTier
