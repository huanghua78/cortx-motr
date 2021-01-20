/* -*- C -*- */
/*
 * Copyright (c) 2013-2020 Seagate Technology LLC and/or its Affiliates
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * For any questions about this software or licensing,
 * please email opensource@seagate.com or cortx-questions@seagate.com.
 *
 */

#pragma once
#ifndef __MOTR_BE_DTM0_LOG_H__
#define __MOTR_BE_DTM0_LOG_H__

/**
 *  @page dtm0 log implementation
 *
 *  @section Overview
 *  DTM0 log module will be working on incoming message request, the
 *  goal of this module is to track journaling of incoming message either on
 *  persistent or volatile memory based on whether logging is happening on
 *  participant or on originator, so that in the phase failure consistency of
 *  failed participant can be restored by iterating over logged journal to
 *  decide which of the logged messages needs to be sent as a redo request.
 *
 *  During normal operation when every participant and originator is online
 *  every transaction will be logged and at any point state of transaction can
 *  be any of the following,
 *
 *	enum m0_dtm0_log_participant_state {
 *		M0_DTML_STATE_UNKNOWN,
 *		M0_DTML_STATE_SENT,
 *		M0_DTML_STATE_EXECUTED,
 *		M0_DTML_STATE_PERSISTENT,
 *	};
 *
 *  Every participant maintains the journal record corresponds to each
 *  transaction and it can be described by "struct m0_dtm0_log_record" which
 *  will be stored in persistent storage. Basically this record will maintain
 *  txr id, state of transaction on all the participant and payload.
 *
 *  The state of transaction for which participant is adding log can be
 *  M0_DTML_STATE_EXECUTED or M0_DTML_STATE_PERSISTENT. For the same transaction
 *  state of remote participant can be either M0_DTML_STATE_UNKNOWN or
 *  M0_DTML_STATE_PERSISTENT, this is because remote participant will only send
 *  state of it's transaction to the other participant when transaction become
 *  persistent.
 *
 *  Originator also maintains the same transaction record in volatile memory.
 *  Originator is expecting to get replys from participant when transaction on
 *  participant is M0_DTML_STATE_EXECUTED or M0_DTML_STATE_PERSISTENT.
 *  On originator the state of the transaction can be M0_DTML_STATE_UNKNOWN to
 *  M0_DTML_STATE_PERSISTENT for each participant. O
 *
 *  During recovery operation of any of the participant, rest of the participant
 *  and originator will iterate over the logged journal and extract the state
 *  of each transation for participant under recovery from logged information
 *  and will send redo request for those transaction which  are not
 *  M0_DTML_STATE_PERSISTENT on participant being recovered.
 *
 *  Upon receiving redo request participant under recovery will log the state
 *  of same transaction of remote participant in persistent store.
 *
 * @section Usecases
 *
 * 1. When transaction successfully executed on participant, DTM0 log will be
 *    added by participant to persistent store and state of transaction will be
 *    M0_DTML_STATE_EXECUTED, if log is create first time then state for same
 *    transaction for remote participant will be logged as M0_DTML_STATE_UNKNOWN
 *    later upon getting persistent notice from remote participant appropriate
 *    state for particular participant will be updated.
 *
 * {
 *    struct log*   l;
 *    struct be_tx* tx;
 *    struct be_tx_credit cred;
 *
 *    log_tx_credit(cred)
 *    tx_open(tx, cred);
 *
 *    if (!log_already_present) {
 *	l = log_create(tx);
 *      txr = m0_be_dtm0_log_find(l, tx_id);
 *    } else {
 *      txr = m0_be_dtm0_log_find(l, tx_id);
 *    }
 *    ...
 *    log_update(l, tx, M0_DTML_STATE_EXECUTED, (struct txr*){ ... });
 *    tx_close(tx);
 * }
 *
 * 2. When transaction become persistent on particular participant, the state
 *    of the transaction for this participant will be updated as
 *    M0_DTML_STATE_PERSISTENT.
 *
 * {
 *    struct log*   l;
 *    struct be_tx* tx;
 *    struct be_tx_credit cred;
 *
 *    log_tx_credit(cred)
 *    tx_open(tx, cred);
 *
 *    txr = m0_be_dtm0_log_find(l, tx_id);
 *
 *    ...
 *    log_update(l, tx, M0_DTML_STATE_PERSISTENT, (struct txr*){ ... });
 *    tx_close(tx);
 * }
 *
 * 3. When transaction become persistent on remote participant, it will send the
 *    persistent notice to rest of the participant and originator stating that
 *    transaction is persistent on it's store. Upon receiving this message each
 *    of participant and originators will update the state of transaction for
 *    remote participant as M0_DTML_STATE_PERSISTENT.
 *
 *    If persistent message from remote participant arrives before receiving
 *    participant or originator has logged the same transaction on it's
 *    persistent/volatile store then with this event log will be created and
 *    remote participant state and txr id will be logged but txr payload will
 *    be invalid as persistent message from remote participant does not carry
 *    payload.
 *
 * {
 *    struct log*   l;
 *    struct be_tx* tx;
 *    struct be_tx_credit cred;
 *
 *    log_tx_credit(cred)
 *    tx_open(tx, cred);
 *
 *    if (!log_already_present) {
 *	l = log_create(tx);
 *      txr = m0_be_dtm0_log_find(l, tx_id);
 *    } else {
 *      txr = m0_be_dtm0_log_find(l, tx_id);
 *    }
 *
 *    log_update(l, tx, M0_DTML_STATE_PERSISTENT, (struct txr*){ ... });
 *    tx_close(tx);
 * }
 *
 */

/* Participant state */
enum m0_dtm0_log_participant_state {
	M0_DTML_STATE_UNKNOWN,
	M0_DTML_STATE_SENT,
	M0_DTML_STATE_EXECUTED,
	M0_DTML_STATE_PERSISTENT,
};

/* Event on which log operation occurs */
enum m0_be_dtm0_log_op {
	M0_DTML_ON_SENT,
	M0_DTML_ON_EXECUTED,
	M0_DTML_ON_PERSISTENT,
	M0_DTML_ON_REDO
};

/* Unique identifier for request */
struct m0_dtm0_dtx_id {
	struct m0_fid   fid;
	uint64_t        clock_id;
};

struct m0_dtm0_log_participant {
	struct m0_fid                      pfid;
	enum m0_dtm0_log_participant_state pstate;
};

/* TODO: define in a separate header. */
struct m0_dtm0_txr {
	struct m0_dtm0_dtx_id           dt_tid;
	struct m0_dtm0_log_participant *dt_participants;
	uint16_t                        dt_participants_nr;
	struct m0_buf                   dt_txr_payload;
};

struct m0_be_dtm0_list_link {
	uin64_t                      dll_magic;
	struct m0_be_dtm0_list_link *dll_next;
};

struct m0_dtm0_log_record {
	struct m0_dtm0_txr          dlr_txr;
	struct m0_be_dtm0_list_link dlr_link; /* link into m0_be_dtm0_log::list */
};

struct m0_be_dtm0_list {
	struct m0_be_dtm0_list_link *dl_head;
	struct m0_be_dtm0_list_link *dl_tail;
}

struct m0_be_dtm0_log {
	struct m0_mutex         dl_lock;  /* volatile structure */
	struct m0_be_dtm0_list *dl_list;  /* persistent structure */
	struct m0_dtm0_list    *dl_vlist; /* Volatile list */
};

// init/fini (for volatile fields)
M0_INTERNAL void m0_be_dtm0_log_init(struct m0_be_dtm0_log *log);
M0_INTERNAL void m0_be_dtm0_log_fini(struct m0_be_dtm0_log *log);

// credit interface
M0_INTERNAL void m0_be_dtm0_log_credit(enum m0_be_dtm0_log_op optype,
				       m0_bcount_t             nr,
				       struct m0_be_tx_credit *accum);
// create/destroy
M0_INTERNAL struct m0_be_dtm0_log *m0_be_dtm0_log_create(struct m0_be_tx *tx);

M0_INTERNAL void m0_be_dtm0_log_destroy(struct m0_be_dtm0_log **log,
					struct m0_be_tx        *tx);

// operational interfaces
M0_INTERNAL void m0_be_dtm0_log_update(struct m0_be_dtm0_log     *log,
				       struct m0_be_tx           *tx,
				       enum m0_dtm0_log_operation op,
				       struct m0_dtm0_txr        *txr);

M0_INTERNAL struct m0_dtm0_txr *m0_be_dtm0_log_find(struct m0_be_dtm0_log *log,
						    struct m0_dtm0_dtx_id *id);
/*
 *       0  -- if left == right
 *      -1  -- if left <  right
 *       1  -- if left >  right
 */
M0_INTERNAL int m0_dtm0_cmp_dtx_id(struct m0_dtm0_dtx_id* left,
				   struct m0_dtm0_dtx_id *right);
#endif /* __MOTR_BE_DTM0_LOG_H__ */
