/*
 * validator/val_utils.h - validator utility functions.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains helper functions for the validator module.
 */

#ifndef VALIDATOR_VAL_UTILS_H
#define VALIDATOR_VAL_UTILS_H
#include "util/data/packed_rrset.h"
struct query_info;
struct reply_info;
struct val_env;
struct module_env;
struct ub_packed_rrset_key;
struct key_entry_key;
struct regional;
struct val_anchors;
struct rrset_cache;
struct sock_list;
struct module_qstate;
struct val_qstate;

/**
 * Response classifications for the validator. The different types of proofs.
 */
enum val_classification {
	/** Not subtyped yet. */
	VAL_CLASS_UNTYPED = 0,
	/** Not a recognized subtype. */
	VAL_CLASS_UNKNOWN,
	/** A positive, direct, response */
	VAL_CLASS_POSITIVE,
	/** A positive response, with a CNAME/DNAME chain. */
	VAL_CLASS_CNAME,
	/** A NOERROR/NODATA response. */
	VAL_CLASS_NODATA,
	/** A NXDOMAIN response. */
	VAL_CLASS_NAMEERROR,
	/** A CNAME/DNAME chain, and the offset is at the end of it,
	 * but there is no answer here, it can be NAMERROR or NODATA. */
	VAL_CLASS_CNAMENOANSWER,
	/** A referral, from cache with a nonRD query. */
	VAL_CLASS_REFERRAL,
	/** A response to a qtype=ANY query. */
	VAL_CLASS_ANY
};

/**
 * Given a response, classify ANSWER responses into a subtype.
 * @param query_flags: query flags for the original query.
 * @param origqinf: query info. The original query name.
 * @param qinf: query info. The chased query name.
 * @param rep: response. The original response.
 * @param skip: offset into the original response answer section.
 * @return A subtype, all values possible except UNTYPED .
 * 	Once CNAME type is returned you can increase skip.
 * 	Then, another CNAME type, CNAME_NOANSWER or POSITIVE are possible.
 */
enum val_classification val_classify_response(uint16_t query_flags,
	struct query_info* origqinf, struct query_info* qinf, 
	struct reply_info* rep, size_t skip);

/**
 * Given a response, determine the name of the "signer". This is primarily
 * to determine if the response is, in fact, signed at all, and, if so, what
 * is the name of the most pertinent keyset.
 *
 * @param subtype: the type from classify.
 * @param qinf: query, the chased query name.
 * @param rep: response to that, original response.
 * @param cname_skip: how many answer rrsets have been skipped due to CNAME
 * 	chains being chased around.
 * @param signer_name:  signer name, if the response is signed 
 * 	(even partially), or null if the response isn't signed.
 * @param signer_len: length of signer_name of 0 if signer_name is NULL.
 */
void val_find_signer(enum val_classification subtype, 
	struct query_info* qinf, struct reply_info* rep,
	size_t cname_skip, uint8_t** signer_name, size_t* signer_len);

/**
 * Verify RRset with keys
 * @param env: module environment (scratch buffer)
 * @param ve: validator environment (verification settings)
 * @param rrset: what to verify
 * @param keys: dnskey rrset to verify with.
 * @param reason: reason of failure. Fixed string or alloced in scratch.
 * @return security status of verification.
 */
enum sec_status val_verify_rrset(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key* rrset, struct ub_packed_rrset_key* keys,
	char** reason);

/**
 * Verify RRset with keys from a keyset.
 * @param env: module environment (scratch buffer)
 * @param ve: validator environment (verification settings)
 * @param rrset: what to verify
 * @param kkey: key_entry to verify with.
 * @param reason: reason of failure. Fixed string or alloced in scratch.
 * @return security status of verification.
 */
enum sec_status val_verify_rrset_entry(struct module_env* env, 
	struct val_env* ve, struct ub_packed_rrset_key* rrset, 
	struct key_entry_key* kkey, char** reason);

/**
 * Verify DNSKEYs with DS rrset. Like val_verify_new_DNSKEYs but
 * returns a sec_status instead of a key_entry.
 * @param env: module environment (scratch buffer)
 * @param ve: validator environment (verification settings)
 * @param dnskey_rrset: DNSKEY rrset to verify
 * @param ds_rrset: DS rrset to verify with.
 * @param reason: reason of failure. Fixed string or alloced in scratch.
 * @return: sec_status_secure if a DS matches.
 *     sec_status_insecure if end of trust (i.e., unknown algorithms).
 *     sec_status_bogus if it fails.
 */
enum sec_status val_verify_DNSKEY_with_DS(struct module_env* env, 
	struct val_env* ve, struct ub_packed_rrset_key* dnskey_rrset, 
	struct ub_packed_rrset_key* ds_rrset, char** reason);

/**
 * Verify new DNSKEYs with DS rrset. The DS contains hash values that should
 * match the DNSKEY keys.
 * match the DS to a DNSKEY and verify the DNSKEY rrset with that key.
 *
 * @param region: where to allocate key entry result.
 * @param env: module environment (scratch buffer)
 * @param ve: validator environment (verification settings)
 * @param dnskey_rrset: DNSKEY rrset to verify
 * @param ds_rrset: DS rrset to verify with.
 * @param reason: reason of failure. Fixed string or alloced in scratch.
 * @return a KeyEntry. This will either contain the now trusted
 *         dnskey_rrset, a "null" key entry indicating that this DS
 *         rrset/DNSKEY pair indicate an secure end to the island of trust
 *         (i.e., unknown algorithms), or a "bad" KeyEntry if the dnskey
 *         rrset fails to verify. Note that the "null" response should
 *         generally only occur in a private algorithm scenario: normally
 *         this sort of thing is checked before fetching the matching DNSKEY
 *         rrset.
 */
struct key_entry_key* val_verify_new_DNSKEYs(struct regional* region, 
	struct module_env* env, struct val_env* ve, 
	struct ub_packed_rrset_key* dnskey_rrset, 
	struct ub_packed_rrset_key* ds_rrset, char** reason);

/**
 * Determine if DS rrset is usable for validator or not.
 * Returns true if the algorithms for key and DShash are supported,
 * for at least one RR.
 *
 * @param ds_rrset: the newly received DS rrset.
 * @return true or false if not usable.
 */
int val_dsset_isusable(struct ub_packed_rrset_key* ds_rrset);

/**
 * Determine by looking at a signed RRset whether or not the RRset name was
 * the result of a wildcard expansion. If so, return the name of the
 * generating wildcard.
 * 
 * @param rrset The rrset to chedck.
 * @param wc: the wildcard name, if the rrset was synthesized from a wildcard.
 *         unchanged if not.  The wildcard name, without "*." in front, is 
 *         returned. This is a pointer into the rrset owner name.
 * @return false if the signatures are inconsistent in indicating the 
 * 	wildcard status; possible spoofing of wildcard response for other
 * 	responses is being tried. We lost the status which rrsig was verified
 * 	after the verification routine finished, so we simply check if
 * 	the signatures are consistent; inserting a fake signature is a denial
 * 	of service; but in that you could also have removed the real 
 * 	signature anyway.
 */
int val_rrset_wildcard(struct ub_packed_rrset_key* rrset, uint8_t** wc);

/**
 * Chase the cname to the next query name.
 * @param qchase: the current query name, updated to next target.
 * @param rep: original message reply to look at CNAMEs.
 * @param cname_skip: the skip into the answer section. Updated to skip
 * 	DNAME and CNAME to the next part of the answer.
 * @return false on error (bad rdata).
 */
int val_chase_cname(struct query_info* qchase, struct reply_info* rep,
	size_t* cname_skip);

/**
 * Fill up the chased reply with the content from the original reply;
 * as pointers to those rrsets. Select the part after the cname_skip into
 * the answer section, NS and AR sections that are signed with same signer.
 *
 * @param chase: chased reply, filled up.
 * @param orig: original reply.
 * @param cname_skip: which part of the answer section to skip.
 * 	The skipped part contains CNAME(and DNAME)s that have been chased.
 * @param name: the signer name to look for.
 * @param len: length of name.
 * @param signer: signer name or NULL if an unsigned RRset is considered.
 *	If NULL, rrsets with the lookup name are copied over.
 */
void val_fill_reply(struct reply_info* chase, struct reply_info* orig, 
	size_t cname_skip, uint8_t* name, size_t len, uint8_t* signer);

/**
 * Remove all unsigned or non-secure status rrsets from NS and AR sections.
 * So that unsigned data does not get let through to clients, when we have
 * found the data to be secure.
 *
 * @param ve: validator environment with cleaning options.
 * @param rep: reply to dump all nonsecure stuff out of.
 */
void val_check_nonsecure(struct val_env* ve, struct reply_info* rep);

/**
 * Mark all unchecked rrset entries not below a trust anchor as indeterminate.
 * Only security==unchecked rrsets are updated.
 * @param rep: the reply with rrsets.
 * @param anchors: the trust anchors.
 * @param r: rrset cache to store updated security status into.
 * @param env: module environment
 */
void val_mark_indeterminate(struct reply_info* rep, 
	struct val_anchors* anchors, struct rrset_cache* r, 
	struct module_env* env);

/**
 * Mark all unchecked rrset entries below a NULL key entry as insecure.
 * Only security==unchecked rrsets are updated.
 * @param rep: the reply with rrsets.
 * @param kname: end of secure space name.
 * @param r: rrset cache to store updated security status into.
 * @param env: module environment
 */
void val_mark_insecure(struct reply_info* rep, uint8_t* kname,
	struct rrset_cache* r, struct module_env* env);

/**
 * Find next unchecked rrset position, return it for skip.
 * @param rep: the original reply to look into.
 * @param skip: the skip now.
 * @return new skip, which may be at the rep->rrset_count position to signal
 * 	there are no unchecked items.
 */
size_t val_next_unchecked(struct reply_info* rep, size_t skip);

/**
 * Find the signer name for an RRset.
 * @param rrset: the rrset.
 * @param sname: signer name is returned or NULL if not signed.
 * @param slen: length of sname (or 0).
 */
void val_find_rrset_signer(struct ub_packed_rrset_key* rrset, uint8_t** sname,
	size_t* slen);

/**
 * Get string to denote the classification result.
 * @param subtype: from classification function.
 * @return static string to describe the classification.
 */
const char* val_classification_to_string(enum val_classification subtype);

/**
 * Add existing list to blacklist.
 * @param blacklist: the blacklist with result
 * @param region: the region where blacklist is allocated.
 *	Allocation failures are logged.
 * @param origin: origin list to add, if NULL, a cache-entry is added to
 *   the blacklist to stop cache from being used.
 * @param cross: if true this is a cross-qstate copy, and the 'origin'
 *   list is not allocated in the same region as the blacklist.
 */
void val_blacklist(struct sock_list** blacklist, struct regional* region,
	struct sock_list* origin, int cross);

/**
 * Append text to the error info for validation.
 * @param qstate: query state.
 * @param vq: validator state.
 * @param str: copied into query region and appended.
 * Failures to allocate are logged.
 */
void val_errinf(struct module_qstate* qstate, struct val_qstate* vq,
	const char* str);

/**
 * Append text to error info:  from 1.2.3.4
 * @param qstate: query state.
 * @param vq: validator state.
 * @param list: sock list with origin of trouble. 
 *	Every element added.
 *	If NULL: nothing is added.
 *	if 0len element: 'from cache' is added.
 */
void val_errinf_origin(struct module_qstate* qstate, struct val_qstate* vq, 
	struct sock_list *origin);

/**
 * Append text to error info:  for RRset name type class
 * @param qstate: query state.
 * @param vq: validator state.
 * @param rr: rrset_key.
 */
void val_errinf_rrset(struct module_qstate* qstate, struct val_qstate* vq, 
	struct ub_packed_rrset_key *rr);

/**
 * Append text to error info:  str dname
 * @param qstate: query state.
 * @param vq: validator state.
 * @param str: explanation string
 * @param dname: the dname.
 * @param rr: rrset_key.
 */
void val_errinf_dname(struct module_qstate* qstate, struct val_qstate* vq, 
	const char* str, uint8_t* dname);

/**
 * Create error info in string
 * @param qstate: query state. (for query name)
 * @param vq: validator state.
 * @return string or NULL on malloc failure (already logged).
 *    This string is malloced and has to be freed by caller.
 */
char* val_errinf_to_str(struct module_qstate* qstate, struct val_qstate* vq);

#endif /* VALIDATOR_VAL_UTILS_H */
