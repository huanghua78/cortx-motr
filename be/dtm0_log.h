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

/* TODO: Where it will be defined? */
#define M0_DTM0_MAX_PARTICIPANT 3
/**
 *  @page dtm0 log implementation
 *
 *  @section Overview
 *  DTM0 log module will be working on incoming message request, the
 *  goal of this module is to track journaling of incoming message either on
 *  persistent or volatile memory based on whether logging is happening on
 *  participant or on originator, so that in the phase failure consistency of
 *  failed participant can be restored by iterating over logged journal to
 *  decide which of the logged messages needs to be sent as a redo to request.
 *
 *  During normal operation when every participant and originator is online
 *  every transaction state is logged and it could be any of the following,
 *	enum m0_dtm0_log_participant_state {
 *		M0_DTML_STATE_UNKNOWN,
 *		M0_DTML_STATE_SENT,
 *		M0_DTML_STATE_EXECUTED,
 *		M0_DTML_STATE_PERSISTENT,
 *	};
 *
 *  Every participant maintains the state of it's own transaction which can be
 *  M0_DTML_STATE_EXECUTED or M0_DTML_STATE_PERSISTENT as well as
 *  state of other participant which can be either M0_DTML_STATE_UNKNOWN or
 *  M0_DTML_STATE_PERSISTENT
 *
 *  On originator the state of the transaction can be M0_DTML_STATE_UNKNOWN to
 *  M0_DTML_STATE_PERSISTENT for each participant.
 *
 *  During recovery operation of any of the participant rest of the participant
 *  and originator will iterate over the logged journal and extract the state
 *  of each transation for participant under recovery from logged information
 *  and will send redo request for those transaction which  are not
 *  M0_DTML_STATE_PERSISTENT on participant being recovered.
 *
 * @section Usecases
 *
 * 1. When transaction successfully executed on participant, log should be
 *    added with transaction state M0_DTML_STATE_EXECUTED by participant, if
 *    log is added first time then state for same tx on remote participant will
 *    be logged as M0_DTML_STATE_UNKNOWN by this participant and later upon
 *    getting persistent notice from remote participant appropriate state for
 *    particular participant will be updated.
 *
 * {
 *    struct log*   l;
 *    struct be_tx* tx;
 *    struct be_tx_credit cred;
 *
 *    log_tx_credit(cred)
 *    tx_open(tx, cred);
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
 * 2. When transaction become persistent on participant, the state of the tx
 *    will be updated as M0_DTML_STATE_PERSISTENT
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
 * 3. When transaction become persistent on other participant, corresponding
 *    state of participant in journal log will be updated.
 *    If log is not present the it will create the log and update the state,
 *    rest of the informatin will be invalid
 *    TODO: What about txr payload it will also be invalid, as persistent
 *    message do not contains the txr payload?
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
	M0_DTML_ON_REDO /* TODO: I don't think so it is needed */
};

struct m0_be_dtm0_log {
	struct m0_mutex   lock;  /* volatile structure */
	struct m0_be_list list;  /* persistent structure */
	struct m0_list    vlist; /* Volatile list */
};

/* Unique identifier for request */
struct m0_dtm0_dtx_id {
	struct m0_fid   fid;
	uint64_t        clock_id;
};

struct m0_dtm0_log_participant_list {
	struct m0_fid                      pfid;
	enum m0_dtm0_log_participant_state pstate;
};

// define in a separate header?
struct m0_dtm0_txr {
	struct m0_dtm0_dtx_id                tid;
	/*TODO: Should this be bufvec or something or static is fine? */
	struct m0_dtm0_log_participant_list  plist[M0_DTM0_MAX_PARTICIPANT];
	struct m0_buf                        txr_payload;
};

/* DTM0 log specific wraper for txr */
struct m0_dtm0_log_record {
	struct m0_dtm0_txr txr;
};

// init/fini (for volatile fields)
M0_INTERNAL void m0_be_dtm0_log_init(struct m0_be_dtm0_log *log);
M0_INTERNAL void m0_be_dtm0_log_fini(struct m0_be_dtm0_log *log);

// credit interface
M0_INTERNAL void m0_be_dtm0_log_credit(enum m0_be_dtm0_log_op optype,
				       m0_bcount_t             nr,
				       struct m0_be_tx_credit *accum);
// create/destroy
M0_INTERNAL struct m0_be_dtm0_log * m0_be_dtm0_log_create(struct m0_be_tx *tx);

M0_INTERNAL void m0_be_dtm0_log_destroy(struct m0_be_dtm0_log **log,
					struct m0_be_tx        *tx);

// operational interfaces
M0_INTERNAL void m0_be_dtm0_log_update(struct m0_be_dtm0_log     *log,
				       struct m0_be_tx           *tx,
				       enum m0_dtm0_log_operation op,
				       struct m0_dtm0_txr        *txr);


M0_INTERNAL struct m0_dtm0_txr *m0_be_dtm0_log_find(struct m0_be_dtm0_log *log,
						    struct m0_dtm0_dtx_id *id);
#endif /* __MOTR_BE_DTM0_LOG_H__ */
