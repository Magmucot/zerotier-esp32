/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * (c) ZeroTier, Inc.
 * https://www.zerotier.com/
 */

#include "IncomingPacket.hpp"

#include "../include/ZeroTierOne.h"
#include "../version.h"
#include "Bond.hpp"
#include "Capability.hpp"
#include "CertificateOfMembership.hpp"
#include "Constants.hpp"
#include "Metrics.hpp"
#include "NetworkController.hpp"
#include "Node.hpp"
#include "PacketMultiplexer.hpp"
#include "Path.hpp"
#include "Peer.hpp"
#include "Revocation.hpp"
#include "RuntimeEnvironment.hpp"
#include "SelfAwareness.hpp"
#include "Switch.hpp"
#include "Tag.hpp"
#include "Topology.hpp"
#include "Trace.hpp"
#include "World.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef ZT_ESP32_STEP45_DEBUG_KEYS
#define ZT_ESP32_STEP45_DEBUG_KEYS 0
#endif

namespace ZeroTier {

static const char* _zt_ip_node_name(uint64_t addr) {
	if (addr == 0xd526b558a5ULL) return "ESP32-QEMU";
	if (addr == 0x4c7d125767ULL) return "ZT-ALPINE-LINUX";
	if (addr == 0x02f5c63563ULL) return "ZT-DEV-LINUX";
	if (addr == 0x24a5a693a7ULL) return "MACBOOK";
	if (addr == 0xf0d8f9eec2ULL) return "SDWAN";
	return "unknown";
}
static const char* _zt_ip_managed_ip(uint64_t addr) {
	if (addr == 0xd526b558a5ULL) return "172.25.204.197";
	if (addr == 0x4c7d125767ULL) return "172.25.206.161";
	if (addr == 0x02f5c63563ULL) return "172.25.22.191";
	return "unknown";
}

#ifdef ZT_ESP32_STEP45_DEBUG_KEYS
static uint64_t ztStep45Fingerprint(const void* p, unsigned int len)
{
	const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
	uint64_t h = 1469598103934665603ULL;
	for (unsigned int i = 0; i < len; ++i) {
		h ^= (uint64_t)b[i];
		h *= 1099511628211ULL;
	}
	return h;
}
#endif

bool IncomingPacket::tryDecode(const RuntimeEnvironment* RR, void* tPtr, int32_t flowId)
{
	const Address sourceAddress(source());

	try {
		// Check for trusted paths or unencrypted HELLOs (HELLO is the only packet sent in the clear)
		const unsigned int c = cipher();
		if (c == ZT_PROTO_CIPHER_SUITE__NO_CRYPTO_TRUSTED_PATH) {
			// If this is marked as a packet via a trusted path, check source address and path ID.
			// Obviously if no trusted paths are configured this always returns false and such
			// packets are dropped on the floor.
			const uint64_t tpid = trustedPathId();
			if (RR->topology->shouldInboundPathBeTrusted(_path->address(), tpid)) {
				_authenticated = true;
			}
			else {
				RR->t->incomingPacketMessageAuthenticationFailure(tPtr, _path, packetId(), sourceAddress, hops(), "path not trusted");
				return true;
			}
		}
		else if ((c == ZT_PROTO_CIPHER_SUITE__C25519_POLY1305_NONE) && (verb() == Packet::VERB_HELLO)) {
			// Only HELLO is allowed in the clear, but will still have a MAC
			return _doHELLO(RR, tPtr, false);
		}

		const SharedPtr<Peer> peer(RR->topology->getPeer(tPtr, sourceAddress));
		if (peer) {
				if (! _authenticated) {
					if (sourceAddress.toInt() == 0xdb64858fedULL) {
						const unsigned char* k = peer->key();
					bool keyNonZero = false;
					for (unsigned int i = 0; i < ZT_SYMMETRIC_KEY_SIZE; ++i) {
						if (k[i]) {
							keyNonZero = true;
							break;
						}
					}
					const AES* aesAll = peer->aesKeys();
					const AES* aesIfSupported = peer->aesKeysIfSupported();
						printf("[ZT_PEER_KEY] stage=pre-dearmor src=0x%llx dst=0x%llx pktId=0x%llx cipher=%u size=%u fromController=1 peerExists=1 peerAddr=0x%llx keyNonZero=%u aesAllPtr=%p aesIfSupportedPtr=%p vProto=%u vMajor=%u vMinor=%u vRev=%u\n",
					       (unsigned long long)sourceAddress.toInt(),
					       (unsigned long long)destination().toInt(),
					       (unsigned long long)packetId(),
					       (unsigned int)c,
					       (unsigned int)size(),
					       (unsigned long long)peer->address().toInt(),
					       (unsigned int)(keyNonZero ? 1 : 0),
					       (void*)aesAll,
					       (void*)aesIfSupported,
					       (unsigned int)peer->remoteVersionProtocol(),
					       (unsigned int)peer->remoteVersionMajor(),
						       (unsigned int)peer->remoteVersionMinor(),
						       (unsigned int)peer->remoteVersionRevision());
#ifdef ZT_ESP32_STEP45_DEBUG_KEYS
						printf("[ZT_STEP45] try_decode_peer src=0x%llx dst=0x%llx cipher=%u peer=%p peerAddr=0x%llx vProto=%u vMaj=%u vMin=%u vRev=%u keyPtr=%p keyFp=%016llx key16=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x aesAllPtr=%p aesAllFp=%016llx aesIfPtr=%p aesIfFp=%016llx\n",
						       (unsigned long long)sourceAddress.toInt(),
						       (unsigned long long)destination().toInt(),
						       (unsigned int)c,
						       (void*)peer.operator->(),
						       (unsigned long long)peer->address().toInt(),
						       (unsigned int)peer->remoteVersionProtocol(),
						       (unsigned int)peer->remoteVersionMajor(),
						       (unsigned int)peer->remoteVersionMinor(),
						       (unsigned int)peer->remoteVersionRevision(),
						       (const void*)k,
						       (unsigned long long)ztStep45Fingerprint(k, ZT_SYMMETRIC_KEY_SIZE),
						       k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7], k[8], k[9], k[10], k[11], k[12], k[13], k[14], k[15],
						       (void*)aesAll,
						       (unsigned long long)ztStep45Fingerprint(aesAll, sizeof(AES) * 2U),
						       (void*)aesIfSupported,
						       (unsigned long long)(aesIfSupported ? ztStep45Fingerprint(aesIfSupported, sizeof(AES) * 2U) : 0ULL));
#endif
					}
					const bool dearmorOk = dearmor(peer->key(), peer->aesKeys(), RR->identity);
#ifdef ZT_ESP32_STEP45_DEBUG_KEYS
					if (sourceAddress.toInt() == 0xdb64858fedULL) {
						printf("[ZT_STEP45] post_dearmor src=0x%llx pktId=0x%llx peer=%p result=%u\n",
						       (unsigned long long)sourceAddress.toInt(),
						       (unsigned long long)packetId(),
						       (void*)peer.operator->(),
						       (unsigned int)(dearmorOk ? 1 : 0));
					}
#endif
					if (! dearmorOk) {
						printf("[ZT_DISPATCH_DROP] reason=dearmor-fail src=0x%llx pktId=0x%llx cipher=%u len=%u\n",
						       (unsigned long long)sourceAddress.toInt(),
						       (unsigned long long)packetId(),
					       (unsigned int)c,
					       (unsigned int)size());
					RR->t->incomingPacketMessageAuthenticationFailure(tPtr, _path, packetId(), sourceAddress, hops(), "invalid MAC");
					peer->recordIncomingInvalidPacket(_path);
					return true;
				}
			}

			if (! uncompress()) {
				printf("[ZT_DISPATCH_DROP] reason=uncompress-fail src=0x%llx pktId=0x%llx cipher=%u len=%u\n",
				       (unsigned long long)sourceAddress.toInt(),
				       (unsigned long long)packetId(),
				       (unsigned int)c,
				       (unsigned int)size());
				RR->t->incomingPacketInvalid(tPtr, _path, packetId(), sourceAddress, hops(), Packet::VERB_NOP, "LZ4 decompression failed");
				return true;
			}

			_authenticated = true;
			const Packet::Verb v = verb();
			const bool fromController = (sourceAddress.toInt() == 0xdb64858fedULL);
			uint64_t inRePid = 0;
			unsigned int inReVerb = 0;
			if (v == Packet::VERB_OK) {
				inReVerb = (unsigned int)(*this)[ZT_PROTO_VERB_OK_IDX_IN_RE_VERB];
				inRePid = at<uint64_t>(ZT_PROTO_VERB_OK_IDX_IN_RE_PACKET_ID);
			}
			else if (v == Packet::VERB_ERROR) {
				inReVerb = (unsigned int)(*this)[ZT_PROTO_VERB_ERROR_IDX_IN_RE_VERB];
				inRePid = at<uint64_t>(ZT_PROTO_VERB_ERROR_IDX_IN_RE_PACKET_ID);
			}
#if defined(ZT_DEBUG_PRINTS) && ZT_DEBUG_PRINTS
			printf("[ZT_DISPATCH] src=0x%llx dst=0x%llx pktId=0x%llx verb=0x%02x payloadLen=%u size=%u fromController=%u inReVerb=0x%02x inRePid=0x%llx\n",
			       (unsigned long long)sourceAddress.toInt(),
			       (unsigned long long)destination().toInt(),
			       (unsigned long long)packetId(),
			       (unsigned int)v,
			       (unsigned int)payloadLength(),
			       (unsigned int)size(),
			       (unsigned int)(fromController ? 1 : 0),
			       (unsigned int)inReVerb,
			       (unsigned long long)inRePid);
#endif

			bool r = true;
			switch (v) {
				// case Packet::VERB_NOP:
				default:   // ignore unknown verbs, but if they pass auth check they are "received"
					Metrics::pkt_nop_in++;
					peer->received(tPtr, _path, hops(), packetId(), payloadLength(), v, 0, Packet::VERB_NOP, false, 0, ZT_QOS_NO_FLOW);
					break;
				case Packet::VERB_HELLO:
					r = _doHELLO(RR, tPtr, true);
					break;
				case Packet::VERB_ACK:
					r = _doACK(RR, tPtr, peer);
					break;
				case Packet::VERB_QOS_MEASUREMENT:
					r = _doQOS_MEASUREMENT(RR, tPtr, peer);
					break;
				case Packet::VERB_ERROR:
					r = _doERROR(RR, tPtr, peer);
					break;
				case Packet::VERB_OK:
					r = _doOK(RR, tPtr, peer);
					break;
				case Packet::VERB_WHOIS:
					r = _doWHOIS(RR, tPtr, peer);
					break;
				case Packet::VERB_RENDEZVOUS:
					r = _doRENDEZVOUS(RR, tPtr, peer);
					break;
				case Packet::VERB_FRAME:
					r = _doFRAME(RR, tPtr, peer, flowId);
					break;
				case Packet::VERB_EXT_FRAME:
					r = _doEXT_FRAME(RR, tPtr, peer, flowId);
					break;
				case Packet::VERB_ECHO:
					r = _doECHO(RR, tPtr, peer);
					break;
				case Packet::VERB_MULTICAST_LIKE:
					r = _doMULTICAST_LIKE(RR, tPtr, peer);
					break;
				case Packet::VERB_NETWORK_CREDENTIALS:
					r = _doNETWORK_CREDENTIALS(RR, tPtr, peer);
					break;
				case Packet::VERB_NETWORK_CONFIG_REQUEST:
					r = _doNETWORK_CONFIG_REQUEST(RR, tPtr, peer);
					break;
				case Packet::VERB_NETWORK_CONFIG:
					r = _doNETWORK_CONFIG(RR, tPtr, peer);
					break;
				case Packet::VERB_MULTICAST_GATHER:
					r = _doMULTICAST_GATHER(RR, tPtr, peer);
					break;
				case Packet::VERB_MULTICAST_FRAME:
					r = _doMULTICAST_FRAME(RR, tPtr, peer);
					break;
				case Packet::VERB_PUSH_DIRECT_PATHS:
					r = _doPUSH_DIRECT_PATHS(RR, tPtr, peer);
					break;
				case Packet::VERB_USER_MESSAGE:
					r = _doUSER_MESSAGE(RR, tPtr, peer);
					break;
				case Packet::VERB_REMOTE_TRACE:
					r = _doREMOTE_TRACE(RR, tPtr, peer);
					break;
				case Packet::VERB_PATH_NEGOTIATION_REQUEST:
					r = _doPATH_NEGOTIATION_REQUEST(RR, tPtr, peer);
					break;
			}
			if (r) {
				RR->node->statsLogVerb((unsigned int)v, (unsigned int)size());
				return true;
			}
			return false;
		}
		else {
			RR->sw->requestWhois(tPtr, RR->node->now(), sourceAddress);
			return false;
		}
	}
	catch (...) {
		RR->t->incomingPacketInvalid(tPtr, _path, packetId(), sourceAddress, hops(), verb(), "unexpected exception in tryDecode()");
		return true;
	}
}

bool IncomingPacket::_doERROR(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	const Packet::Verb inReVerb = (Packet::Verb)(*this)[ZT_PROTO_VERB_ERROR_IDX_IN_RE_VERB];
	const uint64_t inRePacketId = at<uint64_t>(ZT_PROTO_VERB_ERROR_IDX_IN_RE_PACKET_ID);
	const Packet::ErrorCode errorCode = (Packet::ErrorCode)(*this)[ZT_PROTO_VERB_ERROR_IDX_ERROR_CODE];
	uint64_t networkId = 0;

	Metrics::pkt_error_in++;

	/* Security note: we do not gate doERROR() with expectingReplyTo() to
	 * avoid having to log every outgoing packet ID. Instead we put the
	 * logic to determine whether we should consider an ERROR in each
	 * error handler. In most cases these are only trusted in specific
	 * circumstances. */

	switch (errorCode) {
		case Packet::ERROR_OBJ_NOT_FOUND:
			// Object not found, currently only meaningful from network controllers.
			if (inReVerb == Packet::VERB_NETWORK_CONFIG_REQUEST) {
				const SharedPtr<Network> network(RR->node->network(at<uint64_t>(ZT_PROTO_VERB_ERROR_IDX_PAYLOAD)));
				if ((network) && (network->controller() == peer->address())) {
					network->setNotFound(tPtr);
				}
			}
			Metrics::pkt_error_obj_not_found_in++;
			break;

		case Packet::ERROR_UNSUPPORTED_OPERATION:
			// This can be sent in response to any operation, though right now we only
			// consider it meaningful from network controllers. This would indicate
			// that the queried node does not support acting as a controller.
			if (inReVerb == Packet::VERB_NETWORK_CONFIG_REQUEST) {
				const SharedPtr<Network> network(RR->node->network(at<uint64_t>(ZT_PROTO_VERB_ERROR_IDX_PAYLOAD)));
				if ((network) && (network->controller() == peer->address())) {
					network->setNotFound(tPtr);
				}
			}
			Metrics::pkt_error_unsupported_op_in++;
			break;

		case Packet::ERROR_IDENTITY_COLLISION:
			// FIXME: for federation this will need a payload with a signature or something.
			if (RR->topology->isUpstream(peer->identity())) {
				RR->node->postEvent(tPtr, ZT_EVENT_FATAL_ERROR_IDENTITY_COLLISION);
			}
			Metrics::pkt_error_identity_collision_in++;
			break;

		case Packet::ERROR_NEED_MEMBERSHIP_CERTIFICATE: {
			// Peers can send this in response to frames if they do not have a recent enough COM from us
			networkId = at<uint64_t>(ZT_PROTO_VERB_ERROR_IDX_PAYLOAD);
			const SharedPtr<Network> network(RR->node->network(networkId));
			const int64_t now = RR->node->now();
			if ((network) && (network->config().com)) {
				network->peerRequestedCredentials(tPtr, peer->address(), now);
			}
			Metrics::pkt_error_need_membership_cert_in++;
		} break;

		case Packet::ERROR_NETWORK_ACCESS_DENIED_: {
			// Network controller: network access denied.
			const SharedPtr<Network> network(RR->node->network(at<uint64_t>(ZT_PROTO_VERB_ERROR_IDX_PAYLOAD)));
			if ((network) && (network->controller() == peer->address())) {
				network->setAccessDenied(tPtr);
			}
			Metrics::pkt_error_network_access_denied_in++;
		} break;

		case Packet::ERROR_UNWANTED_MULTICAST: {
			// Members of networks can use this error to indicate that they no longer
			// want to receive multicasts on a given channel.
			networkId = at<uint64_t>(ZT_PROTO_VERB_ERROR_IDX_PAYLOAD);
			const SharedPtr<Network> network(RR->node->network(networkId));
			if ((network) && (network->gate(tPtr, peer))) {
				const MulticastGroup mg(MAC(field(ZT_PROTO_VERB_ERROR_IDX_PAYLOAD + 8, 6), 6), at<uint32_t>(ZT_PROTO_VERB_ERROR_IDX_PAYLOAD + 14));
				RR->mc->remove(network->id(), mg, peer->address());
			}
			Metrics::pkt_error_unwanted_multicast_in++;
		} break;

		case Packet::ERROR_NETWORK_AUTHENTICATION_REQUIRED: {
			// fprintf(stderr, "\nPacket::ERROR_NETWORK_AUTHENTICATION_REQUIRED\n\n");
			const SharedPtr<Network> network(RR->node->network(at<uint64_t>(ZT_PROTO_VERB_ERROR_IDX_PAYLOAD)));
			if ((network) && (network->controller() == peer->address())) {
				int s = (int)size() - (ZT_PROTO_VERB_ERROR_IDX_PAYLOAD + 8);
				if (s > 2) {
					const uint16_t errorDataSize = at<uint16_t>(ZT_PROTO_VERB_ERROR_IDX_PAYLOAD + 8);
					s -= 2;
					if (s >= (int)errorDataSize) {
						Dictionary<8192> authInfo(((const char*)this->data()) + (ZT_PROTO_VERB_ERROR_IDX_PAYLOAD + 10), errorDataSize);

						uint64_t authVer = authInfo.getUI(ZT_AUTHINFO_DICT_KEY_VERSION, 0ULL);

						if (authVer == 0) {
							char authenticationURL[2048];

							if (authInfo.get(ZT_AUTHINFO_DICT_KEY_AUTHENTICATION_URL, authenticationURL, sizeof(authenticationURL)) > 0) {
								authenticationURL[sizeof(authenticationURL) - 1] = 0;	// ensure always zero terminated
								network->setAuthenticationRequired(tPtr, authenticationURL);
							}
						}
						else if (authVer == 1) {
							char issuerURL[2048] = { 0 };
							char centralAuthURL[2048] = { 0 };
							char ssoNonce[64] = { 0 };
							char ssoState[128] = { 0 };
							char ssoClientID[256] = { 0 };
							char ssoProvider[64] = { 0 };

							if (authInfo.get(ZT_AUTHINFO_DICT_KEY_ISSUER_URL, issuerURL, sizeof(issuerURL)) > 0) {
								issuerURL[sizeof(issuerURL) - 1] = 0;
							}
							if (authInfo.get(ZT_AUTHINFO_DICT_KEY_CENTRAL_ENDPOINT_URL, centralAuthURL, sizeof(centralAuthURL)) > 0) {
								centralAuthURL[sizeof(centralAuthURL) - 1] = 0;
							}
							if (authInfo.get(ZT_AUTHINFO_DICT_KEY_NONCE, ssoNonce, sizeof(ssoNonce)) > 0) {
								ssoNonce[sizeof(ssoNonce) - 1] = 0;
							}
							if (authInfo.get(ZT_AUTHINFO_DICT_KEY_STATE, ssoState, sizeof(ssoState)) > 0) {
								ssoState[sizeof(ssoState) - 1] = 0;
							}
							if (authInfo.get(ZT_AUTHINFO_DICT_KEY_CLIENT_ID, ssoClientID, sizeof(ssoClientID)) > 0) {
								ssoClientID[sizeof(ssoClientID) - 1] = 0;
							}
							if (authInfo.get(ZT_AUTHINFO_DICT_KEY_SSO_PROVIDER, ssoProvider, sizeof(ssoProvider)) > 0) {
								ssoProvider[sizeof(ssoProvider) - 1] = 0;
							}
							else {
								strncpy(ssoProvider, "default", sizeof(ssoProvider));
							}

							network->setAuthenticationRequired(tPtr, issuerURL, centralAuthURL, ssoClientID, ssoProvider, ssoNonce, ssoState);
						}
					}
				}
				else {
					network->setAuthenticationRequired(tPtr, "");
				}
			}
			Metrics::pkt_error_authentication_required_in++;
		} break;

		default:
			break;
	}

	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_ERROR, inRePacketId, inReVerb, false, networkId, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doACK(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	/*
	if (! peer->rateGateACK(RR->node->now())) {
		return true;
	}
	int32_t ackedBytes;
	if (payloadLength() != sizeof(ackedBytes)) {
		return true;   // ignore
	}
	memcpy(&ackedBytes, payload(), sizeof(ackedBytes));
	peer->receivedAck(_path, RR->node->now(), Utils::ntoh(ackedBytes));
	*/
	Metrics::pkt_ack_in++;
	return true;
}

bool IncomingPacket::_doQOS_MEASUREMENT(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_qos_in++;
	if (! peer->rateGateQoS(RR->node->now(), _path)) {
		return true;
	}
	if (payloadLength() > ZT_QOS_MAX_PACKET_SIZE || payloadLength() < ZT_QOS_MIN_PACKET_SIZE) {
		return true;   // ignore
	}
	const int64_t now = RR->node->now();
	uint64_t rx_id[ZT_QOS_TABLE_SIZE];
	uint16_t rx_ts[ZT_QOS_TABLE_SIZE];
	char* begin = (char*)payload();
	char* ptr = begin;
	int count = 0;
	unsigned int len = payloadLength();
	// Read packet IDs and latency compensation intervals for each packet tracked by this QoS packet
	while (ptr < (begin + len) && (count < ZT_QOS_TABLE_SIZE)) {
		memcpy((void*)&rx_id[count], ptr, sizeof(uint64_t));
		ptr += sizeof(uint64_t);
		memcpy((void*)&rx_ts[count], ptr, sizeof(uint16_t));
		ptr += sizeof(uint16_t);
		count++;
	}
	peer->receivedQoS(_path, now, count, rx_id, rx_ts);
	return true;
}

bool IncomingPacket::_doHELLO(const RuntimeEnvironment* RR, void* tPtr, const bool alreadyAuthenticated)
{
	Metrics::pkt_hello_in++;
	const int64_t now = RR->node->now();

	const uint64_t pid = packetId();
	const Address fromAddress(source());
	const unsigned int protoVersion = (*this)[ZT_PROTO_VERB_HELLO_IDX_PROTOCOL_VERSION];
	const unsigned int vMajor = (*this)[ZT_PROTO_VERB_HELLO_IDX_MAJOR_VERSION];
	const unsigned int vMinor = (*this)[ZT_PROTO_VERB_HELLO_IDX_MINOR_VERSION];
	const unsigned int vRevision = at<uint16_t>(ZT_PROTO_VERB_HELLO_IDX_REVISION);
	const int64_t timestamp = at<int64_t>(ZT_PROTO_VERB_HELLO_IDX_TIMESTAMP);
	Identity id;
	unsigned int ptr = ZT_PROTO_VERB_HELLO_IDX_IDENTITY + id.deserialize(*this, ZT_PROTO_VERB_HELLO_IDX_IDENTITY);

	if (protoVersion < ZT_PROTO_VERSION_MIN) {
		RR->t->incomingPacketDroppedHELLO(tPtr, _path, pid, fromAddress, "protocol version too old");
		return true;
	}
	if (fromAddress != id.address()) {
		RR->t->incomingPacketDroppedHELLO(tPtr, _path, pid, fromAddress, "identity/address mismatch");
		return true;
	}

	SharedPtr<Peer> peer(RR->topology->getPeer(tPtr, id.address()));
	if (peer) {
		// We already have an identity with this address -- check for collisions
		if (! alreadyAuthenticated) {
			if (peer->identity() != id) {
				// Identity is different from the one we already have -- address collision

				// Check rate limits
				if (! RR->node->rateGateIdentityVerification(now, _path->address())) {
					return true;
				}

				uint8_t key[ZT_SYMMETRIC_KEY_SIZE];
				if (RR->identity.agree(id, key)) {
					if (dearmor(key, peer->aesKeysIfSupported(), RR->identity)) {	// ensure packet is authentic, otherwise drop
						RR->t->incomingPacketDroppedHELLO(tPtr, _path, pid, fromAddress, "address collision");
						Packet outp(id.address(), RR->identity.address(), Packet::VERB_ERROR);
						outp.append((uint8_t)Packet::VERB_HELLO);
						outp.append((uint64_t)pid);
						outp.append((uint8_t)Packet::ERROR_IDENTITY_COLLISION);
						outp.armor(key, true, false, peer->aesKeysIfSupported(), peer->identity());
						Metrics::pkt_error_out++;
						Metrics::pkt_error_identity_collision_out++;
						_path->send(RR, tPtr, outp.data(), outp.size(), RR->node->now());
					}
					else {
						RR->t->incomingPacketMessageAuthenticationFailure(tPtr, _path, pid, fromAddress, hops(), "invalid MAC");
					}
				}
				else {
					RR->t->incomingPacketMessageAuthenticationFailure(tPtr, _path, pid, fromAddress, hops(), "invalid identity");
				}

				return true;
			}
			else {
				// Identity is the same as the one we already have -- check packet integrity

				if (! dearmor(peer->key(), peer->aesKeysIfSupported(), RR->identity)) {
					RR->t->incomingPacketMessageAuthenticationFailure(tPtr, _path, pid, fromAddress, hops(), "invalid MAC");
					return true;
				}

				// Continue at // VALID
			}
		}	// else if alreadyAuthenticated then continue at // VALID
	}
	else {
		// We don't already have an identity with this address -- validate and learn it

		// Sanity check: this basically can't happen
		if (alreadyAuthenticated) {
			RR->t->incomingPacketDroppedHELLO(tPtr, _path, pid, fromAddress, "illegal alreadyAuthenticated state");
			return true;
		}

		// Check rate limits
		if (! RR->node->rateGateIdentityVerification(now, _path->address())) {
			RR->t->incomingPacketDroppedHELLO(tPtr, _path, pid, fromAddress, "rate limit exceeded");
			return true;
		}

		// Check packet integrity and MAC (this is faster than locallyValidate() so do it first to filter out total crap)
		SharedPtr<Peer> newPeer(new Peer(RR, RR->identity, id));
		if (! dearmor(newPeer->key(), newPeer->aesKeysIfSupported(), RR->identity)) {
			RR->t->incomingPacketMessageAuthenticationFailure(tPtr, _path, pid, fromAddress, hops(), "invalid MAC");
			return true;
		}

		// Check that identity's address is valid as per the derivation function
		if (! id.locallyValidate()) {
			RR->t->incomingPacketDroppedHELLO(tPtr, _path, pid, fromAddress, "invalid identity");
			return true;
		}

		peer = RR->topology->addPeer(tPtr, newPeer);

		// Continue at // VALID
	}

	// VALID -- if we made it here, packet passed identity and authenticity checks!

	// Get external surface address if present (was not in old versions)
	InetAddress externalSurfaceAddress;
	if (ptr < size()) {
		ptr += externalSurfaceAddress.deserialize(*this, ptr);
		if ((externalSurfaceAddress) && (hops() == 0)) {
			RR->sa->iam(tPtr, id.address(), _path->localSocket(), _path->address(), externalSurfaceAddress, RR->topology->isUpstream(id), now);
		}
	}

	// Get primary planet world ID and world timestamp if present
	uint64_t planetWorldId = 0;
	uint64_t planetWorldTimestamp = 0;
	if ((ptr + 16) <= size()) {
		planetWorldId = at<uint64_t>(ptr);
		ptr += 8;
		planetWorldTimestamp = at<uint64_t>(ptr);
		ptr += 8;
	}

	std::vector<std::pair<uint64_t, uint64_t> > moonIdsAndTimestamps;
	if (ptr < size()) {
		// Remainder of packet, if present, is encrypted
		cryptField(peer->key(), ptr, size() - ptr);

		// Get moon IDs and timestamps if present
		if ((ptr + 2) <= size()) {
			const unsigned int numMoons = at<uint16_t>(ptr);
			ptr += 2;
			for (unsigned int i = 0; i < numMoons; ++i) {
				if ((World::Type)(*this)[ptr++] == World::TYPE_MOON) {
					moonIdsAndTimestamps.push_back(std::pair<uint64_t, uint64_t>(at<uint64_t>(ptr), at<uint64_t>(ptr + 8)));
				}
				ptr += 16;
			}
		}
	}

	// Send OK(HELLO) with an echo of the packet's timestamp and some of the same
	// information about us: version, sent-to address, etc.

	Packet outp(id.address(), RR->identity.address(), Packet::VERB_OK);
	outp.append((unsigned char)Packet::VERB_HELLO);
	outp.append((uint64_t)pid);
	outp.append((uint64_t)timestamp);
	outp.append((unsigned char)ZT_PROTO_VERSION);
	outp.append((unsigned char)ZEROTIER_ONE_VERSION_MAJOR);
	outp.append((unsigned char)ZEROTIER_ONE_VERSION_MINOR);
	outp.append((uint16_t)ZEROTIER_ONE_VERSION_REVISION);
	_path->address().serialize(outp);
	const unsigned int worldUpdateSizeAt = outp.size();
	outp.addSize(2);   // make room for 16-bit size field
	if ((planetWorldId) && (RR->topology->planetWorldTimestamp() > planetWorldTimestamp) && (planetWorldId == RR->topology->planetWorldId())) {
		RR->topology->planet().serialize(outp, false);
	}
	if (! moonIdsAndTimestamps.empty()) {
		std::vector<World> moons(RR->topology->moons());
		for (std::vector<World>::const_iterator m(moons.begin()); m != moons.end(); ++m) {
			for (std::vector<std::pair<uint64_t, uint64_t> >::const_iterator i(moonIdsAndTimestamps.begin()); i != moonIdsAndTimestamps.end(); ++i) {
				if (i->first == m->id()) {
					if (m->timestamp() > i->second) {
						m->serialize(outp, false);
					}
					break;
				}
			}
		}
	}
	outp.setAt<uint16_t>(worldUpdateSizeAt, (uint16_t)(outp.size() - (worldUpdateSizeAt + 2)));

	outp.armor(peer->key(), true, false, peer->aesKeysIfSupported(), peer->identity());
	peer->recordOutgoingPacket(_path, outp.packetId(), outp.payloadLength(), outp.verb(), ZT_QOS_NO_FLOW, now);
	Metrics::pkt_ok_out++;
	_path->send(RR, tPtr, outp.data(), outp.size(), now);

	peer->setRemoteVersion(protoVersion, vMajor, vMinor, vRevision);   // important for this to go first so received() knows the version
	peer->received(tPtr, _path, hops(), pid, payloadLength(), Packet::VERB_HELLO, 0, Packet::VERB_NOP, false, 0, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doOK(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_ok_in++;
	const Packet::Verb inReVerb = (Packet::Verb)(*this)[ZT_PROTO_VERB_OK_IDX_IN_RE_VERB];
	const uint64_t inRePacketId = at<uint64_t>(ZT_PROTO_VERB_OK_IDX_IN_RE_PACKET_ID);
	uint64_t networkId = 0;
	printf("[ZT] OK RX src=0x%llx inReVerb=0x%02x inRePid=0x%llx pktId=0x%llx payloadLen=%u\n",
	       (unsigned long long)peer->address().toInt(),
	       (unsigned int)inReVerb,
	       (unsigned long long)inRePacketId,
	       (unsigned long long)packetId(),
	       (unsigned int)payloadLength());

	if (! RR->node->expectingReplyTo(inRePacketId)) {
		printf("[ZT] OK DROP unexpected-reply src=0x%llx inReVerb=0x%02x inRePid=0x%llx\n",
		       (unsigned long long)peer->address().toInt(),
		       (unsigned int)inReVerb,
		       (unsigned long long)inRePacketId);
		return true;
	}

	switch (inReVerb) {
		case Packet::VERB_HELLO: {
			const uint64_t latency = RR->node->now() - at<uint64_t>(ZT_PROTO_VERB_HELLO__OK__IDX_TIMESTAMP);
			const unsigned int vProto = (*this)[ZT_PROTO_VERB_HELLO__OK__IDX_PROTOCOL_VERSION];
			const unsigned int vMajor = (*this)[ZT_PROTO_VERB_HELLO__OK__IDX_MAJOR_VERSION];
			const unsigned int vMinor = (*this)[ZT_PROTO_VERB_HELLO__OK__IDX_MINOR_VERSION];
			const unsigned int vRevision = at<uint16_t>(ZT_PROTO_VERB_HELLO__OK__IDX_REVISION);
			if (vProto < ZT_PROTO_VERSION_MIN) {
				return true;
			}

			InetAddress externalSurfaceAddress;
			unsigned int ptr = ZT_PROTO_VERB_HELLO__OK__IDX_REVISION + 2;

			// Get reported external surface address if present
			if (ptr < size()) {
				ptr += externalSurfaceAddress.deserialize(*this, ptr);
			}

			// Handle planet or moon updates if present
			if ((ptr + 2) <= size()) {
				const unsigned int worldsLen = at<uint16_t>(ptr);
				ptr += 2;
				if (RR->topology->shouldAcceptWorldUpdateFrom(peer->address())) {
					const unsigned int endOfWorlds = ptr + worldsLen;
					while (ptr < endOfWorlds) {
						World w;
						ptr += w.deserialize(*this, ptr);
						RR->topology->addWorld(tPtr, w, false);
					}
				}
				else {
					ptr += worldsLen;
				}
			}

			if (! hops()) {
				_path->updateLatency((unsigned int)latency, RR->node->now());
			}

				peer->setRemoteVersion(vProto, vMajor, vMinor, vRevision);
#ifdef ZT_ESP32_STEP45_DEBUG_KEYS
				if (peer->address().toInt() == 0xdb64858fedULL) {
					printf("[ZT_STEP45] set_remote_version peer=%p addr=0x%llx vProto=%u vMaj=%u vMin=%u vRev=%u aesAll=%p aesIf=%p\n",
					       (void*)peer.operator->(),
					       (unsigned long long)peer->address().toInt(),
					       (unsigned int)vProto,
					       (unsigned int)vMajor,
					       (unsigned int)vMinor,
					       (unsigned int)vRevision,
					       (void*)peer->aesKeys(),
					       (void*)peer->aesKeysIfSupported());
				}
#endif

			if ((externalSurfaceAddress) && (hops() == 0)) {
				RR->sa->iam(tPtr, peer->address(), _path->localSocket(), _path->address(), externalSurfaceAddress, RR->topology->isUpstream(peer->identity()), RR->node->now());
			}
		} break;

		case Packet::VERB_WHOIS:
			printf("[ZT] OK WHOIS branch src=0x%llx upstream=%d\n",
			       (unsigned long long)peer->address().toInt(),
			       RR->topology->isUpstream(peer->identity()) ? 1 : 0);
			if (RR->topology->isUpstream(peer->identity())) {
				const Identity id(*this, ZT_PROTO_VERB_WHOIS__OK__IDX_IDENTITY);
				printf("[ZT] OK WHOIS identity=0x%llx expectedController=0xdb64858fed\n",
				       (unsigned long long)id.address().toInt());
				printf("[ZT] WHOIS OK: controller 0x%llx added to topology\n",
				       (unsigned long long)id.address().toInt());
				const bool isController = (id.address().toInt() == 0xdb64858fedULL);
				SharedPtr<Peer> existing = RR->topology->getPeerNoCache(id.address());
				SharedPtr<Peer> created(new Peer(RR, RR->identity, id));
				if (isController) {
					const unsigned char* ck = created->key();
					bool ckeyNonZero = false;
					for (unsigned int i = 0; i < ZT_SYMMETRIC_KEY_SIZE; ++i) {
						if (ck[i]) {
							ckeyNonZero = true;
							break;
						}
					}
					printf("[ZT_PEER_STATE] event=whois-ok-controller ctrl=0x%llx existingPeer=%u createdPeer=%u createdKeyNonZero=%u createdAesAllPtr=%p createdAesIfSupportedPtr=%p createdVProto=%u\n",
					       (unsigned long long)id.address().toInt(),
					       (unsigned int)(existing ? 1 : 0),
					       (unsigned int)(created ? 1 : 0),
					       (unsigned int)(ckeyNonZero ? 1 : 0),
					       (void*)created->aesKeys(),
					       (void*)created->aesKeysIfSupported(),
					       (unsigned int)created->remoteVersionProtocol());
				}
				SharedPtr<Peer> added = RR->topology->addPeer(tPtr, created);
				if (isController) {
					printf("[ZT_PEER_STATE] event=topology-add-peer ctrl=0x%llx reusedExisting=%u finalPeerAddr=0x%llx finalVProto=%u finalAesAllPtr=%p finalAesIfSupportedPtr=%p\n",
					       (unsigned long long)id.address().toInt(),
					       (unsigned int)((existing && (added == existing)) ? 1 : 0),
					       (unsigned long long)added->address().toInt(),
					       (unsigned int)added->remoteVersionProtocol(),
					       (void*)added->aesKeys(),
					       (void*)added->aesKeysIfSupported());
				}
				RR->sw->doAnythingWaitingForPeer(tPtr, added);
			}
			else {
				printf("[ZT] OK WHOIS REJECT non-upstream src=0x%llx\n",
				       (unsigned long long)peer->address().toInt());
			}
			break;

		case Packet::VERB_NETWORK_CONFIG_REQUEST: {
			networkId = at<uint64_t>(ZT_PROTO_VERB_OK_IDX_PAYLOAD);
			const SharedPtr<Network> network(RR->node->network(networkId));
			printf("[ZT_NETCONF_RX] path=OK_TO_NETWORK_CONFIG_REQUEST src=0x%llx inRePid=0x%llx pktId=0x%llx nwid=0x%llx payloadLen=%u offset=%u network=%d\n",
			       (unsigned long long)peer->address().toInt(),
			       (unsigned long long)inRePacketId,
			       (unsigned long long)packetId(),
			       (unsigned long long)networkId,
			       (unsigned int)payloadLength(),
			       (unsigned int)ZT_PROTO_VERB_OK_IDX_PAYLOAD,
			       network ? 1 : 0);
			if (network) {
				network->handleConfigChunk(tPtr, packetId(), source(), *this, ZT_PROTO_VERB_OK_IDX_PAYLOAD);
			}
		} break;

		case Packet::VERB_MULTICAST_GATHER: {
			networkId = at<uint64_t>(ZT_PROTO_VERB_MULTICAST_GATHER__OK__IDX_NETWORK_ID);
			const SharedPtr<Network> network(RR->node->network(networkId));
			if (network) {
				const MulticastGroup mg(MAC(field(ZT_PROTO_VERB_MULTICAST_GATHER__OK__IDX_MAC, 6), 6), at<uint32_t>(ZT_PROTO_VERB_MULTICAST_GATHER__OK__IDX_ADI));
				const unsigned int totalKnown = at<uint32_t>(ZT_PROTO_VERB_MULTICAST_GATHER__OK__IDX_GATHER_RESULTS);
				const unsigned int count = at<uint16_t>(ZT_PROTO_VERB_MULTICAST_GATHER__OK__IDX_GATHER_RESULTS + 4);
				char mac_str[24];
				const uint8_t* macb = (const uint8_t*)field(ZT_PROTO_VERB_MULTICAST_GATHER__OK__IDX_MAC, 6);
				snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
				         macb[0],macb[1],macb[2],macb[3],macb[4],macb[5]);
				const uint64_t self_addr = RR->identity.address().toInt();
				const uint64_t src_addr = peer->address().toInt();
				const uint32_t adi_val = mg.adi();
				printf("[ZT_GATHER_OK] node_name=%s node_id=0x%llx zt_managed_ip=%s nwid=0x%llx event=GATHER_OK src=0x%llx src_name=%s mac=%s adi=%u adi_hex=0x%08x group_key=%s/%u totalKnown=%u count=%u payloadLen=%u source=planet\n",
				       _zt_ip_node_name(self_addr),
				       (unsigned long long)self_addr,
				       _zt_ip_managed_ip(self_addr),
				       (unsigned long long)networkId,
				       (unsigned long long)src_addr,
				       _zt_ip_node_name(src_addr),
				       mac_str, (unsigned)adi_val, (unsigned)adi_val,
				       mac_str, (unsigned)adi_val,
				       totalKnown, count, (unsigned int)payloadLength());
				const uint8_t* peers_data = (const uint8_t*)field(ZT_PROTO_VERB_MULTICAST_GATHER__OK__IDX_GATHER_RESULTS + 6, count * 5);
				bool inc_esp32 = false, inc_alpine = false, inc_devlinux = false;
				for (unsigned int gi = 0; gi < count && gi < 8; ++gi) {
					uint64_t ztaddr = 0;
					for (int b = 0; b < 5; ++b) ztaddr = (ztaddr << 8) | peers_data[gi*5+b];
					if (ztaddr == 0xd526b558a5ULL) inc_esp32 = true;
					if (ztaddr == 0x4c7d125767ULL) inc_alpine = true;
					if (ztaddr == 0x02f5c63563ULL) inc_devlinux = true;
					printf("[ZT_GATHER_OK] member[%u]=0x%llx member_name=%s\n",
					       gi, (unsigned long long)ztaddr, _zt_ip_node_name(ztaddr));
				}
				printf("[ZT_GATHER_OK] includes_esp32_qemu_d526b558a5=%s includes_zt_alpine_linux_4c7d125767=%s includes_zt_dev_linux_02f5c63563=%s\n",
				       inc_esp32 ? "yes" : "no",
				       inc_alpine ? "yes" : "no",
				       inc_devlinux ? "yes" : "no");
				RR->mc->addMultiple(tPtr, RR->node->now(), networkId, mg, field(ZT_PROTO_VERB_MULTICAST_GATHER__OK__IDX_GATHER_RESULTS + 6, count * 5), count, totalKnown);
			}
		} break;

		case Packet::VERB_MULTICAST_FRAME: {
			const unsigned int flags = (*this)[ZT_PROTO_VERB_MULTICAST_FRAME__OK__IDX_FLAGS];
			networkId = at<uint64_t>(ZT_PROTO_VERB_MULTICAST_FRAME__OK__IDX_NETWORK_ID);
			const MulticastGroup mg(MAC(field(ZT_PROTO_VERB_MULTICAST_FRAME__OK__IDX_MAC, 6), 6), at<uint32_t>(ZT_PROTO_VERB_MULTICAST_FRAME__OK__IDX_ADI));

			const SharedPtr<Network> network(RR->node->network(networkId));
			if (network) {
				unsigned int offset = 0;

				if ((flags & 0x01) != 0) {	 // deprecated but still used by older peers
					CertificateOfMembership com;
					offset += com.deserialize(*this, ZT_PROTO_VERB_MULTICAST_FRAME__OK__IDX_COM_AND_GATHER_RESULTS);
					if (com) {
						network->addCredential(tPtr, com);
					}
				}

				if ((flags & 0x02) != 0) {
					// OK(MULTICAST_FRAME) includes implicit gather results
					offset += ZT_PROTO_VERB_MULTICAST_FRAME__OK__IDX_COM_AND_GATHER_RESULTS;
					unsigned int totalKnown = at<uint32_t>(offset);
					offset += 4;
					unsigned int count = at<uint16_t>(offset);
					offset += 2;
					RR->mc->addMultiple(tPtr, RR->node->now(), networkId, mg, field(offset, count * 5), count, totalKnown);
				}
			}
		} break;

		default:
			break;
	}

	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_OK, inRePacketId, inReVerb, false, networkId, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doWHOIS(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	if ((! RR->topology->amUpstream()) && (! peer->rateGateInboundWhoisRequest(RR->node->now()))) {
		return true;
	}

	Metrics::pkt_whois_in++;

	Packet outp(peer->address(), RR->identity.address(), Packet::VERB_OK);
	outp.append((unsigned char)Packet::VERB_WHOIS);
	outp.append(packetId());

	unsigned int count = 0;
	unsigned int ptr = ZT_PACKET_IDX_PAYLOAD;
	while ((ptr + ZT_ADDRESS_LENGTH) <= size()) {
		const Address addr(field(ptr, ZT_ADDRESS_LENGTH), ZT_ADDRESS_LENGTH);
		ptr += ZT_ADDRESS_LENGTH;

		const Identity id(RR->topology->getIdentity(tPtr, addr));
		if (id) {
			id.serialize(outp, false);
			++count;
		}
		else {
			// Request unknown WHOIS from upstream from us (if we have one)
			RR->sw->requestWhois(tPtr, RR->node->now(), addr);
		}
	}

	if (count > 0) {
		Metrics::pkt_ok_out++;
		outp.armor(peer->key(), true, false, peer->aesKeysIfSupported(), RR->identity);
		_path->send(RR, tPtr, outp.data(), outp.size(), RR->node->now());
	}

	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_WHOIS, 0, Packet::VERB_NOP, false, 0, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doRENDEZVOUS(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_rendezvous_in++;
	if (RR->topology->isUpstream(peer->identity())) {
		const Address with(field(ZT_PROTO_VERB_RENDEZVOUS_IDX_ZTADDRESS, ZT_ADDRESS_LENGTH), ZT_ADDRESS_LENGTH);
		const unsigned int rdv_port = at<uint16_t>(ZT_PROTO_VERB_RENDEZVOUS_IDX_PORT);
		const unsigned int rdv_addrlen = (*this)[ZT_PROTO_VERB_RENDEZVOUS_IDX_ADDRLEN];
		char rdv_ipbuf[64] = "?";
		if ((rdv_addrlen == 4) || (rdv_addrlen == 16)) {
			InetAddress rdv_addr(field(ZT_PROTO_VERB_RENDEZVOUS_IDX_ADDRESS, rdv_addrlen), rdv_addrlen, rdv_port);
			rdv_addr.toIpString(rdv_ipbuf);
		}
		const SharedPtr<Peer> rendezvousWith(RR->topology->getPeer(tPtr, with));
		printf("[ZT_RENDEZVOUS] src=0x%llx peer_zt=0x%llx suggested_ip=%s port=%u addrlen=%u peer_known=%d upstream=%d payloadLen=%u\n",
		       (unsigned long long)peer->address().toInt(),
		       (unsigned long long)with.toInt(),
		       rdv_ipbuf, rdv_port, rdv_addrlen,
		       rendezvousWith ? 1 : 0,
		       RR->topology->isUpstream(peer->identity()) ? 1 : 0,
		       (unsigned int)payloadLength());
		if (rendezvousWith) {
			if ((rdv_port > 0) && ((rdv_addrlen == 4) || (rdv_addrlen == 16))) {
				InetAddress atAddr(field(ZT_PROTO_VERB_RENDEZVOUS_IDX_ADDRESS, rdv_addrlen), rdv_addrlen, rdv_port);
				const bool usePath = RR->node->shouldUsePathForZeroTierTraffic(tPtr, with, _path->localSocket(), atAddr);
				char atbuf[64]; atAddr.toString(atbuf);
				printf("[ZT_RENDEZVOUS] action=%s peer_zt=0x%llx atAddr=%s\n",
				       usePath ? "attemptContact" : "pathRejected",
				       (unsigned long long)with.toInt(), atbuf);
				if (usePath) {
					const uint64_t junk = RR->node->prng();
					RR->node->putPacket(tPtr, _path->localSocket(), atAddr, &junk, 4, 2);
					rendezvousWith->attemptToContactAt(tPtr, _path->localSocket(), atAddr, RR->node->now(), false);
				}
			}
		}
	}

	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_RENDEZVOUS, 0, Packet::VERB_NOP, false, 0, ZT_QOS_NO_FLOW);

	return true;
}

// Returns true if packet appears valid; pos and proto will be set
static bool _ipv6GetPayload(const uint8_t* frameData, unsigned int frameLen, unsigned int& pos, unsigned int& proto)
{
	if (frameLen < 40) {
		return false;
	}
	pos = 40;
	proto = frameData[6];
	while (pos <= frameLen) {
		switch (proto) {
			case 0:		// hop-by-hop options
			case 43:	// routing
			case 60:	// destination options
			case 135:	// mobility options
				if ((pos + 8) > frameLen) {
					return false;	// invalid!
				}
				proto = frameData[pos];
				pos += ((unsigned int)frameData[pos + 1] * 8) + 8;
				break;

			// case 44: // fragment -- we currently can't parse these and they are deprecated in IPv6 anyway
			// case 50:
			// case 51: // IPSec ESP and AH -- we have to stop here since this is encrypted stuff
			default:
				return true;
		}
	}
	return false;	// overflow == invalid
}

bool IncomingPacket::_doFRAME(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer, int32_t flowId)
{
	Metrics::pkt_frame_in++;
	int32_t _flowId = ZT_QOS_NO_FLOW;

	if (peer->flowHashingSupported()) {
		if (size() > ZT_PROTO_VERB_FRAME_IDX_PAYLOAD) {
			const unsigned int etherType = at<uint16_t>(ZT_PROTO_VERB_FRAME_IDX_ETHERTYPE);
			const unsigned int frameLen = size() - ZT_PROTO_VERB_FRAME_IDX_PAYLOAD;
			const uint8_t* const frameData = reinterpret_cast<const uint8_t*>(data()) + ZT_PROTO_VERB_FRAME_IDX_PAYLOAD;

			if (etherType == ZT_ETHERTYPE_IPV4 && (frameLen >= 20)) {
				uint16_t srcPort = 0;
				uint16_t dstPort = 0;
				uint8_t proto = (reinterpret_cast<const uint8_t*>(frameData)[9]);
				const unsigned int headerLen = 4 * (reinterpret_cast<const uint8_t*>(frameData)[0] & 0xf);
				switch (proto) {
					case 0x01:	 // ICMP
						// flowId = 0x01;
						break;
					// All these start with 16-bit source and destination port in that order
					case 0x06:	 // TCP
					case 0x11:	 // UDP
					case 0x84:	 // SCTP
					case 0x88:	 // UDPLite
						if (frameLen > (headerLen + 4)) {
							unsigned int pos = headerLen + 0;
							srcPort = (reinterpret_cast<const uint8_t*>(frameData)[pos++]) << 8;
							srcPort |= (reinterpret_cast<const uint8_t*>(frameData)[pos]);
							pos++;
							dstPort = (reinterpret_cast<const uint8_t*>(frameData)[pos++]) << 8;
							dstPort |= (reinterpret_cast<const uint8_t*>(frameData)[pos]);
							_flowId = dstPort ^ srcPort ^ proto;
						}
						break;
				}
			}

			if (etherType == ZT_ETHERTYPE_IPV6 && (frameLen >= 40)) {
				uint16_t srcPort = 0;
				uint16_t dstPort = 0;
				unsigned int pos;
				unsigned int proto;
				_ipv6GetPayload((const uint8_t*)frameData, frameLen, pos, proto);
				switch (proto) {
					case 0x3A:	 // ICMPv6
						// flowId = 0x3A;
						break;
					// All these start with 16-bit source and destination port in that order
					case 0x06:	 // TCP
					case 0x11:	 // UDP
					case 0x84:	 // SCTP
					case 0x88:	 // UDPLite
						if (frameLen > (pos + 4)) {
							srcPort = (reinterpret_cast<const uint8_t*>(frameData)[pos++]) << 8;
							srcPort |= (reinterpret_cast<const uint8_t*>(frameData)[pos]);
							pos++;
							dstPort = (reinterpret_cast<const uint8_t*>(frameData)[pos++]) << 8;
							dstPort |= (reinterpret_cast<const uint8_t*>(frameData)[pos]);
							_flowId = dstPort ^ srcPort ^ proto;
						}
						break;
					default:
						break;
				}
			}
		}
	}

	const uint64_t nwid = at<uint64_t>(ZT_PROTO_VERB_FRAME_IDX_NETWORK_ID);
	const SharedPtr<Network> network(RR->node->network(nwid));
	bool trustEstablished = false;
	if (network) {
		if (network->gate(tPtr, peer)) {
			trustEstablished = true;
			if (size() > ZT_PROTO_VERB_FRAME_IDX_PAYLOAD) {
				const unsigned int etherType = at<uint16_t>(ZT_PROTO_VERB_FRAME_IDX_ETHERTYPE);
				const MAC sourceMac(peer->address(), nwid);
				const unsigned int frameLen = size() - ZT_PROTO_VERB_FRAME_IDX_PAYLOAD;
				const uint8_t* const frameData = reinterpret_cast<const uint8_t*>(data()) + ZT_PROTO_VERB_FRAME_IDX_PAYLOAD;
				if (network->filterIncomingPacket(tPtr, peer, RR->identity.address(), sourceMac, network->mac(), frameData, frameLen, etherType, 0) > 0) {
					RR->pm->putFrame(tPtr, nwid, network->userPtr(), sourceMac, network->mac(), etherType, 0, (const void*)frameData, frameLen, _flowId);
				}
			}
		}
		else {
			_sendErrorNeedCredentials(RR, tPtr, peer, nwid);
			return false;
		}
	}
	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_FRAME, 0, Packet::VERB_NOP, trustEstablished, nwid, _flowId);

	return true;
}

bool IncomingPacket::_doEXT_FRAME(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer, int32_t flowId)
{
	Metrics::pkt_ext_frame_in++;

	int32_t _flowId = ZT_QOS_NO_FLOW;
	if (peer->flowHashingSupported()) {
		if (size() > ZT_PROTO_VERB_EXT_FRAME_IDX_PAYLOAD) {
			const unsigned int etherType = at<uint16_t>(ZT_PROTO_VERB_EXT_FRAME_IDX_ETHERTYPE);
			const unsigned int frameLen = size() - ZT_PROTO_VERB_EXT_FRAME_IDX_PAYLOAD;
			const uint8_t* const frameData = reinterpret_cast<const uint8_t*>(data()) + ZT_PROTO_VERB_EXT_FRAME_IDX_PAYLOAD;

			if (etherType == ZT_ETHERTYPE_IPV4 && (frameLen >= 20)) {
				uint16_t srcPort = 0;
				uint16_t dstPort = 0;
				uint8_t proto = (reinterpret_cast<const uint8_t*>(frameData)[9]);
				const unsigned int headerLen = 4 * (reinterpret_cast<const uint8_t*>(frameData)[0] & 0xf);
				switch (proto) {
					case 0x01:	 // ICMP
						// flowId = 0x01;
						break;
					// All these start with 16-bit source and destination port in that order
					case 0x06:	 // TCP
					case 0x11:	 // UDP
					case 0x84:	 // SCTP
					case 0x88:	 // UDPLite
						if (frameLen > (headerLen + 4)) {
							unsigned int pos = headerLen + 0;
							srcPort = (reinterpret_cast<const uint8_t*>(frameData)[pos++]) << 8;
							srcPort |= (reinterpret_cast<const uint8_t*>(frameData)[pos]);
							pos++;
							dstPort = (reinterpret_cast<const uint8_t*>(frameData)[pos++]) << 8;
							dstPort |= (reinterpret_cast<const uint8_t*>(frameData)[pos]);
							_flowId = dstPort ^ srcPort ^ proto;
						}
						break;
				}
			}

			if (etherType == ZT_ETHERTYPE_IPV6 && (frameLen >= 40)) {
				uint16_t srcPort = 0;
				uint16_t dstPort = 0;
				unsigned int pos;
				unsigned int proto;
				_ipv6GetPayload((const uint8_t*)frameData, frameLen, pos, proto);
				switch (proto) {
					case 0x3A:	 // ICMPv6
						// flowId = 0x3A;
						break;
					// All these start with 16-bit source and destination port in that order
					case 0x06:	 // TCP
					case 0x11:	 // UDP
					case 0x84:	 // SCTP
					case 0x88:	 // UDPLite
						if (frameLen > (pos + 4)) {
							srcPort = (reinterpret_cast<const uint8_t*>(frameData)[pos++]) << 8;
							srcPort |= (reinterpret_cast<const uint8_t*>(frameData)[pos]);
							pos++;
							dstPort = (reinterpret_cast<const uint8_t*>(frameData)[pos++]) << 8;
							dstPort |= (reinterpret_cast<const uint8_t*>(frameData)[pos]);
							_flowId = dstPort ^ srcPort ^ proto;
						}
						break;
					default:
						break;
				}
			}
		}
	}

	const uint64_t nwid = at<uint64_t>(ZT_PROTO_VERB_EXT_FRAME_IDX_NETWORK_ID);
	const SharedPtr<Network> network(RR->node->network(nwid));
	if (network) {
		const unsigned int flags = (*this)[ZT_PROTO_VERB_EXT_FRAME_IDX_FLAGS];

		unsigned int comLen = 0;
		if ((flags & 0x01) != 0) {	 // inline COM with EXT_FRAME is deprecated but still used with old peers
			CertificateOfMembership com;
			comLen = com.deserialize(*this, ZT_PROTO_VERB_EXT_FRAME_IDX_COM);
			if (com) {
				network->addCredential(tPtr, com);
			}
		}

		if (! network->gate(tPtr, peer)) {
			RR->t->incomingNetworkAccessDenied(tPtr, network, _path, packetId(), size(), peer->address(), Packet::VERB_EXT_FRAME, true);
			_sendErrorNeedCredentials(RR, tPtr, peer, nwid);
			return false;
		}

		if (size() > ZT_PROTO_VERB_EXT_FRAME_IDX_PAYLOAD) {
			const unsigned int etherType = at<uint16_t>(comLen + ZT_PROTO_VERB_EXT_FRAME_IDX_ETHERTYPE);
			const MAC to(field(comLen + ZT_PROTO_VERB_EXT_FRAME_IDX_TO, ZT_PROTO_VERB_EXT_FRAME_LEN_TO), ZT_PROTO_VERB_EXT_FRAME_LEN_TO);
			const MAC from(field(comLen + ZT_PROTO_VERB_EXT_FRAME_IDX_FROM, ZT_PROTO_VERB_EXT_FRAME_LEN_FROM), ZT_PROTO_VERB_EXT_FRAME_LEN_FROM);
			const unsigned int frameLen = size() - (comLen + ZT_PROTO_VERB_EXT_FRAME_IDX_PAYLOAD);
			const uint8_t* const frameData = (const uint8_t*)field(comLen + ZT_PROTO_VERB_EXT_FRAME_IDX_PAYLOAD, frameLen);

			if ((! from) || (from == network->mac())) {
				peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_EXT_FRAME, 0, Packet::VERB_NOP, true, nwid, _flowId);	  // trustEstablished because COM is okay
				return true;
			}

			switch (network->filterIncomingPacket(tPtr, peer, RR->identity.address(), from, to, frameData, frameLen, etherType, 0)) {
				case 1:
					if (from != MAC(peer->address(), nwid)) {
						if (network->config().permitsBridging(peer->address())) {
							network->learnBridgeRoute(from, peer->address());
						}
						else {
							RR->t->incomingNetworkFrameDropped(tPtr, network, _path, packetId(), size(), peer->address(), Packet::VERB_EXT_FRAME, from, to, "bridging not allowed (remote)");
							peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_EXT_FRAME, 0, Packet::VERB_NOP, true, nwid, _flowId);	  // trustEstablished because COM is okay
							return true;
						}
					}
					else if (to != network->mac()) {
						if (to.isMulticast()) {
							if (network->config().multicastLimit == 0) {
								RR->t->incomingNetworkFrameDropped(tPtr, network, _path, packetId(), size(), peer->address(), Packet::VERB_EXT_FRAME, from, to, "multicast disabled");
								peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_EXT_FRAME, 0, Packet::VERB_NOP, true, nwid, _flowId);	  // trustEstablished because COM is okay
								return true;
							}
						}
						else if (! network->config().permitsBridging(RR->identity.address())) {
							RR->t->incomingNetworkFrameDropped(tPtr, network, _path, packetId(), size(), peer->address(), Packet::VERB_EXT_FRAME, from, to, "bridging not allowed (local)");
							peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_EXT_FRAME, 0, Packet::VERB_NOP, true, nwid, _flowId);	  // trustEstablished because COM is okay
							return true;
						}
					}
					// fall through -- 2 means accept regardless of bridging checks or other restrictions
				case 2:
					RR->pm->putFrame(tPtr, nwid, network->userPtr(), from, to, etherType, 0, (const void*)frameData, frameLen, _flowId);
					break;
			}
		}

		if ((flags & 0x10) != 0) {	 // ACK requested
			Packet outp(peer->address(), RR->identity.address(), Packet::VERB_OK);
			outp.append((uint8_t)Packet::VERB_EXT_FRAME);
			outp.append((uint64_t)packetId());
			outp.append((uint64_t)nwid);
			const int64_t now = RR->node->now();
			outp.armor(peer->key(), true, false, peer->aesKeysIfSupported(), peer->identity());
			peer->recordOutgoingPacket(_path, outp.packetId(), outp.payloadLength(), outp.verb(), ZT_QOS_NO_FLOW, now);
			Metrics::pkt_ok_out++;
			_path->send(RR, tPtr, outp.data(), outp.size(), RR->node->now());
		}

		peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_EXT_FRAME, 0, Packet::VERB_NOP, true, nwid, _flowId);
	}
	else {
		peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_EXT_FRAME, 0, Packet::VERB_NOP, false, nwid, _flowId);
	}

	return true;
}

bool IncomingPacket::_doECHO(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_echo_in++;
	uint64_t now = RR->node->now();
	if (! _path->rateGateEchoRequest(now)) {
		return true;
	}

	const uint64_t pid = packetId();
	Packet outp(peer->address(), RR->identity.address(), Packet::VERB_OK);
	outp.append((unsigned char)Packet::VERB_ECHO);
	outp.append((uint64_t)pid);
	if (size() > ZT_PACKET_IDX_PAYLOAD) {
		outp.append(reinterpret_cast<const unsigned char*>(data()) + ZT_PACKET_IDX_PAYLOAD, size() - ZT_PACKET_IDX_PAYLOAD);
	}
	outp.armor(peer->key(), true, false, peer->aesKeysIfSupported(), peer->identity());
	peer->recordOutgoingPacket(_path, outp.packetId(), outp.payloadLength(), outp.verb(), ZT_QOS_NO_FLOW, now);
	Metrics::pkt_ok_out++;
	_path->send(RR, tPtr, outp.data(), outp.size(), RR->node->now());

	peer->received(tPtr, _path, hops(), pid, payloadLength(), Packet::VERB_ECHO, 0, Packet::VERB_NOP, false, 0, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doMULTICAST_LIKE(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_multicast_like_in++;
	const int64_t now = RR->node->now();
	bool authorized = false;
	uint64_t lastNwid = 0;

	// Packet contains a series of 18-byte network,MAC,ADI tuples
	for (unsigned int ptr = ZT_PACKET_IDX_PAYLOAD; ptr < size(); ptr += 18) {
		const uint64_t nwid = at<uint64_t>(ptr);
		if (nwid != lastNwid) {
			lastNwid = nwid;
			SharedPtr<Network> network(RR->node->network(nwid));
			if (network) {
				authorized = network->gate(tPtr, peer);
			}
			if (! authorized) {
				authorized = ((RR->topology->amUpstream()) || (RR->node->localControllerHasAuthorized(now, nwid, peer->address())));
			}
		}
		if (authorized) {
			RR->mc->add(tPtr, now, nwid, MulticastGroup(MAC(field(ptr + 8, 6), 6), at<uint32_t>(ptr + 14)), peer->address());
		}
	}

	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_MULTICAST_LIKE, 0, Packet::VERB_NOP, false, 0, ZT_QOS_NO_FLOW);
	return true;
}

bool IncomingPacket::_doNETWORK_CREDENTIALS(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_network_credentials_in++;
	if (! peer->rateGateCredentialsReceived(RR->node->now())) {
		return true;
	}

	CertificateOfMembership com;
	Capability cap;
	Tag tag;
	Revocation revocation;
	CertificateOfOwnership coo;
	bool trustEstablished = false;
	SharedPtr<Network> network;

	unsigned int p = ZT_PACKET_IDX_PAYLOAD;
	while ((p < size()) && ((*this)[p] != 0)) {
		p += com.deserialize(*this, p);
		if (com) {
			network = RR->node->network(com.networkId());
			if (network) {
				switch (network->addCredential(tPtr, com)) {
					case Membership::ADD_REJECTED:
						break;
					case Membership::ADD_ACCEPTED_NEW:
					case Membership::ADD_ACCEPTED_REDUNDANT:
						trustEstablished = true;
						break;
					case Membership::ADD_DEFERRED_FOR_WHOIS:
						return false;
				}
			}
		}
	}
	++p;   // skip trailing 0 after COMs if present

	if (p < size()) {	// older ZeroTier versions do not send capabilities, tags, or revocations
		const unsigned int numCapabilities = at<uint16_t>(p);
		p += 2;
		for (unsigned int i = 0; i < numCapabilities; ++i) {
			p += cap.deserialize(*this, p);
			if ((! network) || (network->id() != cap.networkId())) {
				network = RR->node->network(cap.networkId());
			}
			if (network) {
				switch (network->addCredential(tPtr, cap)) {
					case Membership::ADD_REJECTED:
						break;
					case Membership::ADD_ACCEPTED_NEW:
					case Membership::ADD_ACCEPTED_REDUNDANT:
						trustEstablished = true;
						break;
					case Membership::ADD_DEFERRED_FOR_WHOIS:
						return false;
				}
			}
		}

		if (p >= size()) {
			return true;
		}

		const unsigned int numTags = at<uint16_t>(p);
		p += 2;
		for (unsigned int i = 0; i < numTags; ++i) {
			p += tag.deserialize(*this, p);
			if ((! network) || (network->id() != tag.networkId())) {
				network = RR->node->network(tag.networkId());
			}
			if (network) {
				switch (network->addCredential(tPtr, tag)) {
					case Membership::ADD_REJECTED:
						break;
					case Membership::ADD_ACCEPTED_NEW:
					case Membership::ADD_ACCEPTED_REDUNDANT:
						trustEstablished = true;
						break;
					case Membership::ADD_DEFERRED_FOR_WHOIS:
						return false;
				}
			}
		}

		if (p >= size()) {
			return true;
		}

		const unsigned int numRevocations = at<uint16_t>(p);
		p += 2;
		for (unsigned int i = 0; i < numRevocations; ++i) {
			p += revocation.deserialize(*this, p);
			if ((! network) || (network->id() != revocation.networkId())) {
				network = RR->node->network(revocation.networkId());
			}
			if (network) {
				switch (network->addCredential(tPtr, peer->address(), revocation)) {
					case Membership::ADD_REJECTED:
						break;
					case Membership::ADD_ACCEPTED_NEW:
					case Membership::ADD_ACCEPTED_REDUNDANT:
						trustEstablished = true;
						break;
					case Membership::ADD_DEFERRED_FOR_WHOIS:
						return false;
				}
			}
		}

		if (p >= size()) {
			return true;
		}

		const unsigned int numCoos = at<uint16_t>(p);
		p += 2;
		for (unsigned int i = 0; i < numCoos; ++i) {
			p += coo.deserialize(*this, p);
			if ((! network) || (network->id() != coo.networkId())) {
				network = RR->node->network(coo.networkId());
			}
			if (network) {
				switch (network->addCredential(tPtr, coo)) {
					case Membership::ADD_REJECTED:
						break;
					case Membership::ADD_ACCEPTED_NEW:
					case Membership::ADD_ACCEPTED_REDUNDANT:
						trustEstablished = true;
						break;
					case Membership::ADD_DEFERRED_FOR_WHOIS:
						return false;
				}
			}
		}
	}

	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_NETWORK_CREDENTIALS, 0, Packet::VERB_NOP, trustEstablished, (network) ? network->id() : 0, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doNETWORK_CONFIG_REQUEST(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_network_config_request_in++;
	const uint64_t nwid = at<uint64_t>(ZT_PROTO_VERB_NETWORK_CONFIG_REQUEST_IDX_NETWORK_ID);
	const unsigned int hopCount = hops();
	const uint64_t requestPacketId = packetId();

	if (RR->localNetworkController) {
		const unsigned int metaDataLength = (ZT_PROTO_VERB_NETWORK_CONFIG_REQUEST_IDX_DICT_LEN <= size()) ? at<uint16_t>(ZT_PROTO_VERB_NETWORK_CONFIG_REQUEST_IDX_DICT_LEN) : 0;
		const char* metaDataBytes = (metaDataLength != 0) ? (const char*)field(ZT_PROTO_VERB_NETWORK_CONFIG_REQUEST_IDX_DICT, metaDataLength) : (const char*)0;
		const Dictionary<ZT_NETWORKCONFIG_METADATA_DICT_CAPACITY> metaData(metaDataBytes, metaDataLength);
		RR->localNetworkController->request(nwid, (hopCount > 0) ? InetAddress() : _path->address(), requestPacketId, peer->identity(), metaData);
	}
	else {
		Packet outp(peer->address(), RR->identity.address(), Packet::VERB_ERROR);
		outp.append((unsigned char)Packet::VERB_NETWORK_CONFIG_REQUEST);
		outp.append(requestPacketId);
		outp.append((unsigned char)Packet::ERROR_UNSUPPORTED_OPERATION);
		outp.append(nwid);
		outp.armor(peer->key(), true, false, peer->aesKeysIfSupported(), peer->identity());
		Metrics::pkt_error_out++;
		Metrics::pkt_error_unsupported_op_out++;
		_path->send(RR, tPtr, outp.data(), outp.size(), RR->node->now());
	}

	peer->received(tPtr, _path, hopCount, requestPacketId, payloadLength(), Packet::VERB_NETWORK_CONFIG_REQUEST, 0, Packet::VERB_NOP, false, nwid, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doNETWORK_CONFIG(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_network_config_in++;
	const SharedPtr<Network> network(RR->node->network(at<uint64_t>(ZT_PACKET_IDX_PAYLOAD)));
	printf("[ZT_NETCONF_RX] path=DIRECT_NETWORK_CONFIG src=0x%llx pktId=0x%llx nwid=0x%llx payloadLen=%u offset=%u network=%d\n",
	       (unsigned long long)peer->address().toInt(),
	       (unsigned long long)packetId(),
	       (unsigned long long)at<uint64_t>(ZT_PACKET_IDX_PAYLOAD),
	       (unsigned int)payloadLength(),
	       (unsigned int)ZT_PACKET_IDX_PAYLOAD,
	       network ? 1 : 0);
	if (network) {
		const uint64_t configUpdateId = network->handleConfigChunk(tPtr, packetId(), source(), *this, ZT_PACKET_IDX_PAYLOAD);
		if (configUpdateId) {
			Packet outp(peer->address(), RR->identity.address(), Packet::VERB_OK);
			outp.append((uint8_t)Packet::VERB_ECHO);
			outp.append((uint64_t)packetId());
			outp.append((uint64_t)network->id());
			outp.append((uint64_t)configUpdateId);
			const int64_t now = RR->node->now();
			outp.armor(peer->key(), true, false, peer->aesKeysIfSupported(), peer->identity());
			peer->recordOutgoingPacket(_path, outp.packetId(), outp.payloadLength(), outp.verb(), ZT_QOS_NO_FLOW, now);
			Metrics::pkt_ok_out++;
			_path->send(RR, tPtr, outp.data(), outp.size(), RR->node->now());
		}
	}

	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_NETWORK_CONFIG, 0, Packet::VERB_NOP, false, (network) ? network->id() : 0, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doMULTICAST_GATHER(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_multicast_gather_in++;
	const uint64_t nwid = at<uint64_t>(ZT_PROTO_VERB_MULTICAST_GATHER_IDX_NETWORK_ID);
	const unsigned int flags = (*this)[ZT_PROTO_VERB_MULTICAST_GATHER_IDX_FLAGS];
	const MulticastGroup mg(MAC(field(ZT_PROTO_VERB_MULTICAST_GATHER_IDX_MAC, 6), 6), at<uint32_t>(ZT_PROTO_VERB_MULTICAST_GATHER_IDX_ADI));
	const unsigned int gatherLimit = at<uint32_t>(ZT_PROTO_VERB_MULTICAST_GATHER_IDX_GATHER_LIMIT);

	const SharedPtr<Network> network(RR->node->network(nwid));

	if ((flags & 0x01) != 0) {
		try {
			CertificateOfMembership com;
			com.deserialize(*this, ZT_PROTO_VERB_MULTICAST_GATHER_IDX_COM);
			if ((com) && (network)) {
				network->addCredential(tPtr, com);
			}
		}
		catch (...) {
		}	// discard invalid COMs
	}

	const bool trustEstablished = (network) ? network->gate(tPtr, peer) : false;
	const int64_t now = RR->node->now();
	if ((gatherLimit > 0) && ((trustEstablished) || (RR->topology->amUpstream()) || (RR->node->localControllerHasAuthorized(now, nwid, peer->address())))) {
		Packet outp(peer->address(), RR->identity.address(), Packet::VERB_OK);
		outp.append((unsigned char)Packet::VERB_MULTICAST_GATHER);
		outp.append(packetId());
		outp.append(nwid);
		mg.mac().appendTo(outp);
		outp.append((uint32_t)mg.adi());
		const unsigned int gatheredLocally = RR->mc->gather(peer->address(), nwid, mg, outp, gatherLimit);
		if (gatheredLocally > 0) {
			outp.armor(peer->key(), true, false, peer->aesKeysIfSupported(), peer->identity());
			peer->recordOutgoingPacket(_path, outp.packetId(), outp.payloadLength(), outp.verb(), ZT_QOS_NO_FLOW, now);
			Metrics::pkt_ok_out++;
			_path->send(RR, tPtr, outp.data(), outp.size(), now);
		}
	}

	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_MULTICAST_GATHER, 0, Packet::VERB_NOP, trustEstablished, nwid, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doMULTICAST_FRAME(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_multicast_frame_in++;
	const uint64_t nwid = at<uint64_t>(ZT_PROTO_VERB_MULTICAST_FRAME_IDX_NETWORK_ID);
	const unsigned int flags = (*this)[ZT_PROTO_VERB_MULTICAST_FRAME_IDX_FLAGS];

	const SharedPtr<Network> network(RR->node->network(nwid));
	if (network) {
		// Offset -- size of optional fields added to position of later fields
		unsigned int offset = 0;

		if ((flags & 0x01) != 0) {
			// This is deprecated but may still be sent by old peers
			CertificateOfMembership com;
			offset += com.deserialize(*this, ZT_PROTO_VERB_MULTICAST_FRAME_IDX_COM);
			if (com) {
				network->addCredential(tPtr, com);
			}
		}

		if (! network->gate(tPtr, peer)) {
			_sendErrorNeedCredentials(RR, tPtr, peer, nwid);
			return false;
		}

		unsigned int gatherLimit = 0;
		if ((flags & 0x02) != 0) {
			gatherLimit = at<uint32_t>(offset + ZT_PROTO_VERB_MULTICAST_FRAME_IDX_GATHER_LIMIT);
			offset += 4;
		}

		MAC from;
		if ((flags & 0x04) != 0) {
			from.setTo(field(offset + ZT_PROTO_VERB_MULTICAST_FRAME_IDX_SOURCE_MAC, 6), 6);
			offset += 6;
		}
		else {
			from.fromAddress(peer->address(), nwid);
		}

		const MulticastGroup to(MAC(field(offset + ZT_PROTO_VERB_MULTICAST_FRAME_IDX_DEST_MAC, 6), 6), at<uint32_t>(offset + ZT_PROTO_VERB_MULTICAST_FRAME_IDX_DEST_ADI));
		const unsigned int etherType = at<uint16_t>(offset + ZT_PROTO_VERB_MULTICAST_FRAME_IDX_ETHERTYPE);
		const unsigned int frameLen = size() - (offset + ZT_PROTO_VERB_MULTICAST_FRAME_IDX_FRAME);

		if (network->config().multicastLimit == 0) {
			RR->t->incomingNetworkFrameDropped(tPtr, network, _path, packetId(), size(), peer->address(), Packet::VERB_MULTICAST_FRAME, from, to.mac(), "multicast disabled");
			peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_MULTICAST_FRAME, 0, Packet::VERB_NOP, false, nwid, ZT_QOS_NO_FLOW);
			return true;
		}

		if ((frameLen > 0) && (frameLen <= ZT_MAX_MTU)) {
			if (! to.mac().isMulticast()) {
				RR->t->incomingPacketInvalid(tPtr, _path, packetId(), source(), hops(), Packet::VERB_MULTICAST_FRAME, "destination not multicast");
				peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_MULTICAST_FRAME, 0, Packet::VERB_NOP, true, nwid, ZT_QOS_NO_FLOW);   // trustEstablished because COM is okay
				return true;
			}
			if ((! from) || (from.isMulticast()) || (from == network->mac())) {
				RR->t->incomingPacketInvalid(tPtr, _path, packetId(), source(), hops(), Packet::VERB_MULTICAST_FRAME, "invalid source MAC");
				peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_MULTICAST_FRAME, 0, Packet::VERB_NOP, true, nwid, ZT_QOS_NO_FLOW);   // trustEstablished because COM is okay
				return true;
			}

			const uint8_t* const frameData = (const uint8_t*)field(offset + ZT_PROTO_VERB_MULTICAST_FRAME_IDX_FRAME, frameLen);

			if ((flags & 0x08) && (network->config().isMulticastReplicator(RR->identity.address()))) {
				RR->mc->send(tPtr, RR->node->now(), network, peer->address(), to, from, etherType, frameData, frameLen);
			}

			if (from != MAC(peer->address(), nwid)) {
				if (network->config().permitsBridging(peer->address())) {
					network->learnBridgeRoute(from, peer->address());
				}
				else {
					RR->t->incomingNetworkFrameDropped(tPtr, network, _path, packetId(), size(), peer->address(), Packet::VERB_MULTICAST_FRAME, from, to.mac(), "bridging not allowed (remote)");
					peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_MULTICAST_FRAME, 0, Packet::VERB_NOP, true, nwid, ZT_QOS_NO_FLOW);   // trustEstablished because COM is okay
					return true;
				}
			}

			if (network->filterIncomingPacket(tPtr, peer, RR->identity.address(), from, to.mac(), frameData, frameLen, etherType, 0) > 0) {
				RR->node->putFrame(tPtr, nwid, network->userPtr(), from, to.mac(), etherType, 0, (const void*)frameData, frameLen);
			}
		}

		if (gatherLimit) {
			Packet outp(source(), RR->identity.address(), Packet::VERB_OK);
			outp.append((unsigned char)Packet::VERB_MULTICAST_FRAME);
			outp.append(packetId());
			outp.append(nwid);
			to.mac().appendTo(outp);
			outp.append((uint32_t)to.adi());
			outp.append((unsigned char)0x02);	// flag 0x02 = contains gather results
			if (RR->mc->gather(peer->address(), nwid, to, outp, gatherLimit)) {
				const int64_t now = RR->node->now();
				outp.armor(peer->key(), true, false, peer->aesKeysIfSupported(), peer->identity());
				peer->recordOutgoingPacket(_path, outp.packetId(), outp.payloadLength(), outp.verb(), ZT_QOS_NO_FLOW, now);
				Metrics::pkt_ok_out++;
				_path->send(RR, tPtr, outp.data(), outp.size(), RR->node->now());
			}
		}

		peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_MULTICAST_FRAME, 0, Packet::VERB_NOP, true, nwid, ZT_QOS_NO_FLOW);
	}

	return true;
}

bool IncomingPacket::_doPUSH_DIRECT_PATHS(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_push_direct_paths_in++;
	const int64_t now = RR->node->now();

	if (! peer->rateGatePushDirectPaths(now)) {
		peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_PUSH_DIRECT_PATHS, 0, Packet::VERB_NOP, false, 0, ZT_QOS_NO_FLOW);
		return true;
	}

	// Second, limit addresses by scope and type
	uint8_t countPerScope[ZT_INETADDRESS_MAX_SCOPE + 1][2];	  // [][0] is v4, [][1] is v6
	memset(countPerScope, 0, sizeof(countPerScope));

	unsigned int count = at<uint16_t>(ZT_PACKET_IDX_PAYLOAD);
	unsigned int ptr = ZT_PACKET_IDX_PAYLOAD + 2;
	printf("[ZT_PUSH_DIRECT_PATHS] src=0x%llx total_paths=%u payloadLen=%u\n",
	       (unsigned long long)peer->address().toInt(),
	       count, (unsigned int)payloadLength());

	while (count--) {	// if ptr overflows Buffer will throw
		unsigned int flags = (*this)[ptr++];
		unsigned int extLen = at<uint16_t>(ptr);
		ptr += 2;
		ptr += extLen;	 // unused right now
		unsigned int addrType = (*this)[ptr++];
		unsigned int addrLen = (*this)[ptr++];

		switch (addrType) {
			case 4: {
				const InetAddress a(field(ptr, 4), 4, at<uint16_t>(ptr + 4));
				char pbuf[64]; a.toString(pbuf);
				printf("[ZT_PUSH_DIRECT_PATHS] path_v4=%s flags=0x%02x\n", pbuf, flags);
				if (((flags & ZT_PUSH_DIRECT_PATHS_FLAG_FORGET_PATH) == 0) &&												  // not being told to forget
					(! (((flags & ZT_PUSH_DIRECT_PATHS_FLAG_CLUSTER_REDIRECT) == 0) && (peer->hasActivePathTo(now, a)))) &&	  // not already known
					(RR->node->shouldUsePathForZeroTierTraffic(tPtr, peer->address(), _path->localSocket(), a)))			  // should use path
				{
					if ((flags & ZT_PUSH_DIRECT_PATHS_FLAG_CLUSTER_REDIRECT) != 0) {
						peer->clusterRedirect(tPtr, _path, a, now);
					}
					else if (++countPerScope[(int)a.ipScope()][0] <= ZT_PUSH_DIRECT_PATHS_MAX_PER_SCOPE_AND_FAMILY) {
						peer->attemptToContactAt(tPtr, InetAddress(), a, now, false);
					}
				}
			} break;
			case 6: {
				const InetAddress a(field(ptr, 16), 16, at<uint16_t>(ptr + 16));
				char pbuf[64]; a.toString(pbuf);
				printf("[ZT_PUSH_DIRECT_PATHS] path_v6=%s flags=0x%02x\n", pbuf, flags);
				if (((flags & ZT_PUSH_DIRECT_PATHS_FLAG_FORGET_PATH) == 0) &&												  // not being told to forget
					(! (((flags & ZT_PUSH_DIRECT_PATHS_FLAG_CLUSTER_REDIRECT) == 0) && (peer->hasActivePathTo(now, a)))) &&	  // not already known
					(RR->node->shouldUsePathForZeroTierTraffic(tPtr, peer->address(), _path->localSocket(), a)))			  // should use path
				{
					if ((flags & ZT_PUSH_DIRECT_PATHS_FLAG_CLUSTER_REDIRECT) != 0) {
						peer->clusterRedirect(tPtr, _path, a, now);
					}
					else if (++countPerScope[(int)a.ipScope()][1] <= ZT_PUSH_DIRECT_PATHS_MAX_PER_SCOPE_AND_FAMILY) {
						peer->attemptToContactAt(tPtr, InetAddress(), a, now, false);
					}
				}
			} break;
		}
		ptr += addrLen;
	}

	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_PUSH_DIRECT_PATHS, 0, Packet::VERB_NOP, false, 0, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doUSER_MESSAGE(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_user_message_in++;
	if (likely(size() >= (ZT_PACKET_IDX_PAYLOAD + 8))) {
		ZT_UserMessage um;
		um.origin = peer->address().toInt();
		um.typeId = at<uint64_t>(ZT_PACKET_IDX_PAYLOAD);
		um.data = reinterpret_cast<const void*>(reinterpret_cast<const uint8_t*>(data()) + ZT_PACKET_IDX_PAYLOAD + 8);
		um.length = size() - (ZT_PACKET_IDX_PAYLOAD + 8);
		RR->node->postEvent(tPtr, ZT_EVENT_USER_MESSAGE, reinterpret_cast<const void*>(&um));
	}

	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_USER_MESSAGE, 0, Packet::VERB_NOP, false, 0, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doREMOTE_TRACE(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_remote_trace_in++;
	ZT_RemoteTrace rt;
	const char* ptr = reinterpret_cast<const char*>(data()) + ZT_PACKET_IDX_PAYLOAD;
	const char* const eof = reinterpret_cast<const char*>(data()) + size();
	rt.origin = peer->address().toInt();
	rt.data = const_cast<char*>(ptr);	// start of first string
	while (ptr < eof) {
		if (! *ptr) {	// end of string
			rt.len = (unsigned int)(ptr - rt.data);
			if ((rt.len > 0) && (rt.len <= ZT_MAX_REMOTE_TRACE_SIZE)) {
				RR->node->postEvent(tPtr, ZT_EVENT_REMOTE_TRACE, &rt);
			}
			rt.data = const_cast<char*>(++ptr);	  // start of next string, if any
		}
		else {
			++ptr;
		}
	}

	peer->received(tPtr, _path, hops(), packetId(), payloadLength(), Packet::VERB_REMOTE_TRACE, 0, Packet::VERB_NOP, false, 0, ZT_QOS_NO_FLOW);

	return true;
}

bool IncomingPacket::_doPATH_NEGOTIATION_REQUEST(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer)
{
	Metrics::pkt_path_negotiation_request_in++;
	uint64_t now = RR->node->now();
	if (! peer->rateGatePathNegotiation(now, _path)) {
		return true;
	}
	if (payloadLength() != sizeof(int16_t)) {
		return true;
	}
	int16_t remoteUtility = 0;
	memcpy(&remoteUtility, payload(), sizeof(int16_t));
	peer->processIncomingPathNegotiationRequest(now, _path, Utils::ntoh(remoteUtility));
	return true;
}

void IncomingPacket::_sendErrorNeedCredentials(const RuntimeEnvironment* RR, void* tPtr, const SharedPtr<Peer>& peer, const uint64_t nwid)
{
	Packet outp(source(), RR->identity.address(), Packet::VERB_ERROR);
	outp.append((uint8_t)verb());
	outp.append(packetId());
	outp.append((uint8_t)Packet::ERROR_NEED_MEMBERSHIP_CERTIFICATE);
	outp.append(nwid);
	outp.armor(peer->key(), true, false, peer->aesKeysIfSupported(), peer->identity());
	Metrics::pkt_error_out++;
	Metrics::pkt_error_need_membership_cert_out++;
	_path->send(RR, tPtr, outp.data(), outp.size(), RR->node->now());
}

}	// namespace ZeroTier
