/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2014, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/llog_cat.c
 *
 * OST<->MDS recovery logging infrastructure.
 *
 * Invariants in implementation:
 * - we do not share logs among different OST<->MDS connections, so that
 *   if an OST or MDS fails it need only look at log(s) relevant to itself
 *
 * Author: Andreas Dilger <adilger@clusterfs.com>
 * Author: Alexey Zhuravlev <alexey.zhuravlev@intel.com>
 * Author: Mikhail Pershin <mike.pershin@intel.com>
 */

#define DEBUG_SUBSYSTEM S_LOG


#include <obd_class.h>

#include "llog_internal.h"

/* Create a new log handle and add it to the open list.
 * This log handle will be closed when all of the records in it are removed.
 *
 * Assumes caller has already pushed us into the kernel context and is locking.
 */
static int llog_cat_new_log(const struct lu_env *env,
			    struct llog_handle *cathandle,
			    struct llog_handle *loghandle,
			    struct thandle *th)
{
	struct llog_thread_info	*lgi = llog_info(env);
	struct llog_logid_rec	*rec = &lgi->lgi_logid;
	int			 rc;

	ENTRY;

	if (OBD_FAIL_CHECK(OBD_FAIL_MDS_LLOG_CREATE_FAILED))
		RETURN(-ENOSPC);

	rc = llog_create(env, loghandle, th);
	/* if llog is already created, no need to initialize it */
	if (rc == -EEXIST) {
		RETURN(0);
	} else if (rc != 0) {
		CERROR("%s: can't create new plain llog in catalog: rc = %d\n",
		       loghandle->lgh_ctxt->loc_obd->obd_name, rc);
		RETURN(rc);
	}

	rc = llog_init_handle(env, loghandle,
			      LLOG_F_IS_PLAIN | LLOG_F_ZAP_WHEN_EMPTY,
			      &cathandle->lgh_hdr->llh_tgtuuid);
	if (rc)
		GOTO(out_destroy, rc);

	/* build the record for this log in the catalog */
	rec->lid_hdr.lrh_len = sizeof(*rec);
	rec->lid_hdr.lrh_type = LLOG_LOGID_MAGIC;
	rec->lid_id = loghandle->lgh_id;

	/* append the new record into catalog. The new index will be
	 * assigned to the record and updated in rec header */
	rc = llog_write_rec(env, cathandle, &rec->lid_hdr,
			    &loghandle->u.phd.phd_cookie, LLOG_NEXT_IDX, th);
	if (rc < 0)
		GOTO(out_destroy, rc);

	CDEBUG(D_OTHER, "new recovery log "DOSTID":%x for index %u of catalog"
	       DOSTID"\n", POSTID(&loghandle->lgh_id.lgl_oi),
	       loghandle->lgh_id.lgl_ogen, rec->lid_hdr.lrh_index,
	       POSTID(&cathandle->lgh_id.lgl_oi));

	loghandle->lgh_hdr->llh_cat_idx = rec->lid_hdr.lrh_index;
	RETURN(0);
out_destroy:
	llog_destroy(env, loghandle);
	RETURN(rc);
}

/* Open an existent log handle and add it to the open list.
 * This log handle will be closed when all of the records in it are removed.
 *
 * Assumes caller has already pushed us into the kernel context and is locking.
 * We return a lock on the handle to ensure nobody yanks it from us.
 *
 * This takes extra reference on llog_handle via llog_handle_get() and require
 * this reference to be put by caller using llog_handle_put()
 */
int llog_cat_id2handle(const struct lu_env *env, struct llog_handle *cathandle,
		       struct llog_handle **res, struct llog_logid *logid)
{
	struct llog_handle	*loghandle;
	enum llog_flag		 fmt;
	int			 rc = 0;

	ENTRY;

	if (cathandle == NULL)
		RETURN(-EBADF);

	fmt = cathandle->lgh_hdr->llh_flags & LLOG_F_EXT_MASK;
	down_write(&cathandle->lgh_lock);
	list_for_each_entry(loghandle, &cathandle->u.chd.chd_head,
			    u.phd.phd_entry) {
		struct llog_logid *cgl = &loghandle->lgh_id;

		if (ostid_id(&cgl->lgl_oi) == ostid_id(&logid->lgl_oi) &&
		    ostid_seq(&cgl->lgl_oi) == ostid_seq(&logid->lgl_oi)) {
			if (cgl->lgl_ogen != logid->lgl_ogen) {
				CERROR("%s: log "DOSTID" generation %x != %x\n",
				       loghandle->lgh_ctxt->loc_obd->obd_name,
				       POSTID(&logid->lgl_oi), cgl->lgl_ogen,
				       logid->lgl_ogen);
				continue;
			}
			loghandle->u.phd.phd_cat_handle = cathandle;
			up_write(&cathandle->lgh_lock);
			GOTO(out, rc = 0);
		}
	}
	up_write(&cathandle->lgh_lock);

	rc = llog_open(env, cathandle->lgh_ctxt, &loghandle, logid, NULL,
		       LLOG_OPEN_EXISTS);
	if (rc < 0) {
		CERROR("%s: error opening log id "DOSTID":%x: rc = %d\n",
		       cathandle->lgh_ctxt->loc_obd->obd_name,
		       POSTID(&logid->lgl_oi), logid->lgl_ogen, rc);
		RETURN(rc);
	}

	rc = llog_init_handle(env, loghandle, LLOG_F_IS_PLAIN | fmt, NULL);
	if (rc < 0) {
		llog_close(env, loghandle);
		loghandle = NULL;
		RETURN(rc);
	}

	down_write(&cathandle->lgh_lock);
	list_add_tail(&loghandle->u.phd.phd_entry, &cathandle->u.chd.chd_head);
	up_write(&cathandle->lgh_lock);

	loghandle->u.phd.phd_cat_handle = cathandle;
	loghandle->u.phd.phd_cookie.lgc_lgl = cathandle->lgh_id;
	loghandle->u.phd.phd_cookie.lgc_index =
				loghandle->lgh_hdr->llh_cat_idx;
	EXIT;
out:
	llog_handle_get(loghandle);
	*res = loghandle;
	return 0;
}

int llog_cat_close(const struct lu_env *env, struct llog_handle *cathandle)
{
	struct llog_handle	*loghandle, *n;
	int			 rc;

	ENTRY;

	list_for_each_entry_safe(loghandle, n, &cathandle->u.chd.chd_head,
				 u.phd.phd_entry) {
		struct llog_log_hdr	*llh = loghandle->lgh_hdr;
		int			 index;

		/* unlink open-not-created llogs */
		list_del_init(&loghandle->u.phd.phd_entry);
		llh = loghandle->lgh_hdr;
		if (loghandle->lgh_obj != NULL && llh != NULL &&
		    (llh->llh_flags & LLOG_F_ZAP_WHEN_EMPTY) &&
		    (llh->llh_count == 1)) {
			rc = llog_destroy(env, loghandle);
			if (rc)
				CERROR("%s: failure destroying log during "
				       "cleanup: rc = %d\n",
				       loghandle->lgh_ctxt->loc_obd->obd_name,
				       rc);

			index = loghandle->u.phd.phd_cookie.lgc_index;
			llog_cat_cleanup(env, cathandle, NULL, index);
		}
		llog_close(env, loghandle);
	}
	/* if handle was stored in ctxt, remove it too */
	if (cathandle->lgh_ctxt->loc_handle == cathandle)
		cathandle->lgh_ctxt->loc_handle = NULL;
	rc = llog_close(env, cathandle);
	RETURN(rc);
}
EXPORT_SYMBOL(llog_cat_close);

/**
 * lockdep markers for nested struct llog_handle::lgh_lock locking.
 */
enum {
        LLOGH_CAT,
        LLOGH_LOG
};

/** Return the currently active log handle.  If the current log handle doesn't
 * have enough space left for the current record, start a new one.
 *
 * If reclen is 0, we only want to know what the currently active log is,
 * otherwise we get a lock on this log so nobody can steal our space.
 *
 * Assumes caller has already pushed us into the kernel context and is locking.
 *
 * NOTE: loghandle is write-locked upon successful return
 */
static struct llog_handle *llog_cat_current_log(struct llog_handle *cathandle,
						struct thandle *th)
{
        struct llog_handle *loghandle = NULL;
        ENTRY;

	down_read_nested(&cathandle->lgh_lock, LLOGH_CAT);
        loghandle = cathandle->u.chd.chd_current_log;
        if (loghandle) {
		struct llog_log_hdr *llh;

		down_write_nested(&loghandle->lgh_lock, LLOGH_LOG);
		llh = loghandle->lgh_hdr;
		if (llh == NULL ||
		    loghandle->lgh_last_idx < LLOG_BITMAP_SIZE(llh) - 1) {
			up_read(&cathandle->lgh_lock);
                        RETURN(loghandle);
                } else {
			up_write(&loghandle->lgh_lock);
                }
        }
	up_read(&cathandle->lgh_lock);

	/* time to use next log */

	/* first, we have to make sure the state hasn't changed */
	down_write_nested(&cathandle->lgh_lock, LLOGH_CAT);
	loghandle = cathandle->u.chd.chd_current_log;
	if (loghandle) {
		struct llog_log_hdr *llh;

		down_write_nested(&loghandle->lgh_lock, LLOGH_LOG);
		llh = loghandle->lgh_hdr;
		LASSERT(llh);
                if (loghandle->lgh_last_idx < LLOG_BITMAP_SIZE(llh) - 1) {
			up_write(&cathandle->lgh_lock);
                        RETURN(loghandle);
                } else {
			up_write(&loghandle->lgh_lock);
                }
        }

	CDEBUG(D_INODE, "use next log\n");

	loghandle = cathandle->u.chd.chd_next_log;
	cathandle->u.chd.chd_current_log = loghandle;
	cathandle->u.chd.chd_next_log = NULL;
	down_write_nested(&loghandle->lgh_lock, LLOGH_LOG);
	up_write(&cathandle->lgh_lock);
	LASSERT(loghandle);
	RETURN(loghandle);
}

/* Add a single record to the recovery log(s) using a catalog
 * Returns as llog_write_record
 *
 * Assumes caller has already pushed us into the kernel context.
 */
int llog_cat_add_rec(const struct lu_env *env, struct llog_handle *cathandle,
		     struct llog_rec_hdr *rec, struct llog_cookie *reccookie,
		     struct thandle *th)
{
        struct llog_handle *loghandle;
        int rc;
        ENTRY;

        LASSERT(rec->lrh_len <= LLOG_CHUNK_SIZE);
	loghandle = llog_cat_current_log(cathandle, th);
	LASSERT(!IS_ERR(loghandle));

	/* loghandle is already locked by llog_cat_current_log() for us */
	if (!llog_exist(loghandle)) {
		rc = llog_cat_new_log(env, cathandle, loghandle, th);
		if (rc < 0) {
			up_write(&loghandle->lgh_lock);
			RETURN(rc);
		}
	}
	/* now let's try to add the record */
	rc = llog_write_rec(env, loghandle, rec, reccookie, LLOG_NEXT_IDX, th);
	if (rc < 0)
		CDEBUG_LIMIT(rc == -ENOSPC ? D_HA : D_ERROR,
			     "llog_write_rec %d: lh=%p\n", rc, loghandle);
	up_write(&loghandle->lgh_lock);
        if (rc == -ENOSPC) {
		/* try to use next log */
		loghandle = llog_cat_current_log(cathandle, th);
		LASSERT(!IS_ERR(loghandle));
		/* new llog can be created concurrently */
		if (!llog_exist(loghandle)) {
			rc = llog_cat_new_log(env, cathandle, loghandle, th);
			if (rc < 0) {
				up_write(&loghandle->lgh_lock);
				RETURN(rc);
			}
		}
		/* now let's try to add the record */
		rc = llog_write_rec(env, loghandle, rec, reccookie,
				    LLOG_NEXT_IDX, th);
		if (rc < 0)
			CERROR("llog_write_rec %d: lh=%p\n", rc, loghandle);
		up_write(&loghandle->lgh_lock);
	}

	RETURN(rc);
}
EXPORT_SYMBOL(llog_cat_add_rec);

int llog_cat_declare_add_rec(const struct lu_env *env,
			     struct llog_handle *cathandle,
			     struct llog_rec_hdr *rec, struct thandle *th)
{
	struct llog_thread_info	*lgi = llog_info(env);
	struct llog_logid_rec	*lirec = &lgi->lgi_logid;
	struct llog_handle	*loghandle, *next;
	int			 rc = 0;

	ENTRY;

	if (cathandle->u.chd.chd_current_log == NULL) {
		/* declare new plain llog */
		down_write(&cathandle->lgh_lock);
		if (cathandle->u.chd.chd_current_log == NULL) {
			rc = llog_open(env, cathandle->lgh_ctxt, &loghandle,
				       NULL, NULL, LLOG_OPEN_NEW);
			if (rc == 0) {
				cathandle->u.chd.chd_current_log = loghandle;
				list_add_tail(&loghandle->u.phd.phd_entry,
					      &cathandle->u.chd.chd_head);
			}
		}
		up_write(&cathandle->lgh_lock);
	} else if (cathandle->u.chd.chd_next_log == NULL) {
		/* declare next plain llog */
		down_write(&cathandle->lgh_lock);
		if (cathandle->u.chd.chd_next_log == NULL) {
			rc = llog_open(env, cathandle->lgh_ctxt, &loghandle,
				       NULL, NULL, LLOG_OPEN_NEW);
			if (rc == 0) {
				cathandle->u.chd.chd_next_log = loghandle;
				list_add_tail(&loghandle->u.phd.phd_entry,
					      &cathandle->u.chd.chd_head);
			}
		}
		up_write(&cathandle->lgh_lock);
	}
	if (rc)
		GOTO(out, rc);

	lirec->lid_hdr.lrh_len = sizeof(*lirec);

	if (!llog_exist(cathandle->u.chd.chd_current_log)) {
		rc = llog_declare_create(env, cathandle->u.chd.chd_current_log,
					 th);
		if (rc)
			GOTO(out, rc);
		llog_declare_write_rec(env, cathandle, &lirec->lid_hdr, -1, th);
	}
	/* declare records in the llogs */
	rc = llog_declare_write_rec(env, cathandle->u.chd.chd_current_log,
				    rec, -1, th);
	if (rc)
		GOTO(out, rc);

	next = cathandle->u.chd.chd_next_log;
	if (next) {
		if (!llog_exist(next)) {
			rc = llog_declare_create(env, next, th);
			llog_declare_write_rec(env, cathandle, &lirec->lid_hdr,
					       -1, th);
		}
		/* XXX: we hope for declarations made for existing llog
		 *	this might be not correct with some backends
		 *	where declarations are expected against specific
		 *	object like ZFS with full debugging enabled */
		/*llog_declare_write_rec(env, next, rec, -1, th);*/
	}
out:
	RETURN(rc);
}
EXPORT_SYMBOL(llog_cat_declare_add_rec);

int llog_cat_add(const struct lu_env *env, struct llog_handle *cathandle,
		 struct llog_rec_hdr *rec, struct llog_cookie *reccookie)
{
	struct llog_ctxt	*ctxt;
	struct dt_device	*dt;
	struct thandle		*th = NULL;
	int			 rc;

	ctxt = cathandle->lgh_ctxt;
	LASSERT(ctxt);
	LASSERT(ctxt->loc_exp);

	LASSERT(cathandle->lgh_obj != NULL);
	dt = lu2dt_dev(cathandle->lgh_obj->do_lu.lo_dev);

	th = dt_trans_create(env, dt);
	if (IS_ERR(th))
		RETURN(PTR_ERR(th));

	rc = llog_cat_declare_add_rec(env, cathandle, rec, th);
	if (rc)
		GOTO(out_trans, rc);

	rc = dt_trans_start_local(env, dt, th);
	if (rc)
		GOTO(out_trans, rc);
	rc = llog_cat_add_rec(env, cathandle, rec, reccookie, th);
out_trans:
	dt_trans_stop(env, dt, th);
	RETURN(rc);
}
EXPORT_SYMBOL(llog_cat_add);

/* For each cookie in the cookie array, we clear the log in-use bit and either:
 * - the log is empty, so mark it free in the catalog header and delete it
 * - the log is not empty, just write out the log header
 *
 * The cookies may be in different log files, so we need to get new logs
 * each time.
 *
 * Assumes caller has already pushed us into the kernel context.
 */
int llog_cat_cancel_records(const struct lu_env *env,
			    struct llog_handle *cathandle, int count,
			    struct llog_cookie *cookies)
{
	int i, index, rc = 0, failed = 0;

	ENTRY;

	for (i = 0; i < count; i++, cookies++) {
		struct llog_handle	*loghandle;
		struct llog_logid	*lgl = &cookies->lgc_lgl;
		int			 lrc;

		rc = llog_cat_id2handle(env, cathandle, &loghandle, lgl);
		if (rc) {
			CERROR("%s: cannot find handle for llog "DOSTID": %d\n",
			       cathandle->lgh_ctxt->loc_obd->obd_name,
			       POSTID(&lgl->lgl_oi), rc);
			failed++;
			continue;
		}

		lrc = llog_cancel_rec(env, loghandle, cookies->lgc_index);
		if (lrc == LLOG_DEL_PLAIN) { /* log has been destroyed */
			index = loghandle->u.phd.phd_cookie.lgc_index;
			rc = llog_cat_cleanup(env, cathandle, loghandle,
					      index);
		} else if (lrc == -ENOENT) {
			if (rc == 0) /* ENOENT shouldn't rewrite any error */
				rc = lrc;
		} else if (lrc < 0) {
			failed++;
			rc = lrc;
		}
		llog_handle_put(loghandle);
	}
	if (rc)
		CERROR("%s: fail to cancel %d of %d llog-records: rc = %d\n",
		       cathandle->lgh_ctxt->loc_obd->obd_name, failed, count,
		       rc);

	RETURN(rc);
}
EXPORT_SYMBOL(llog_cat_cancel_records);

static int llog_cat_process_cb(const struct lu_env *env,
			       struct llog_handle *cat_llh,
			       struct llog_rec_hdr *rec, void *data)
{
        struct llog_process_data *d = data;
        struct llog_logid_rec *lir = (struct llog_logid_rec *)rec;
        struct llog_handle *llh;
	struct llog_log_hdr *hdr;
        int rc;

        ENTRY;
        if (rec->lrh_type != LLOG_LOGID_MAGIC) {
                CERROR("invalid record in catalog\n");
                RETURN(-EINVAL);
        }
	CDEBUG(D_HA, "processing log "DOSTID":%x at index %u of catalog "
	       DOSTID"\n", POSTID(&lir->lid_id.lgl_oi), lir->lid_id.lgl_ogen,
	       rec->lrh_index, POSTID(&cat_llh->lgh_id.lgl_oi));

	rc = llog_cat_id2handle(env, cat_llh, &llh, &lir->lid_id);
	if (rc) {
		CERROR("%s: cannot find handle for llog "DOSTID": %d\n",
		       cat_llh->lgh_ctxt->loc_obd->obd_name,
		       POSTID(&lir->lid_id.lgl_oi), rc);
		if (rc == -ENOENT || rc == -ESTALE) {
			/* After a server crash, a stub of index
			 * record in catlog could be kept, because
			 * plain log destroy + catlog index record
			 * deletion are not atomic. So we end up with
			 * an index but no actual record. Destroy the
			 * index and move on. */
			rc = llog_cat_cleanup(env, cat_llh, NULL,
					      rec->lrh_index);
		}

		RETURN(rc);
	}

	/* clean old empty llogs, do not consider current llog in use */
	/* ignore remote (lgh_obj=NULL) llogs */
	hdr = llh->lgh_hdr;
	if ((hdr->llh_flags & LLOG_F_ZAP_WHEN_EMPTY) &&
	    hdr->llh_count == 1 && cat_llh->lgh_obj != NULL &&
	    llh != cat_llh->u.chd.chd_current_log) {
		rc = llog_destroy(env, llh);
		if (rc)
			CERROR("%s: fail to destroy empty log: rc = %d\n",
			       llh->lgh_ctxt->loc_obd->obd_name, rc);
		GOTO(out, rc = LLOG_DEL_PLAIN);
	}

	if (rec->lrh_index < d->lpd_startcat) {
		/* Skip processing of the logs until startcat */
		rc = 0;
	} else if (d->lpd_startidx > 0) {
                struct llog_process_cat_data cd;

                cd.lpcd_first_idx = d->lpd_startidx;
                cd.lpcd_last_idx = 0;
		rc = llog_process_or_fork(env, llh, d->lpd_cb, d->lpd_data,
					  &cd, false);
		/* Continue processing the next log from idx 0 */
		d->lpd_startidx = 0;
	} else {
		rc = llog_process_or_fork(env, llh, d->lpd_cb, d->lpd_data,
					  NULL, false);
	}

out:
	/* The empty plain log was destroyed while processing */
	if (rc == LLOG_DEL_PLAIN)
		rc = llog_cat_cleanup(env, cat_llh, llh,
				      llh->u.phd.phd_cookie.lgc_index);
	llog_handle_put(llh);

	RETURN(rc);
}

int llog_cat_process_or_fork(const struct lu_env *env,
			     struct llog_handle *cat_llh,
			     llog_cb_t cb, void *data, int startcat,
			     int startidx, bool fork)
{
        struct llog_process_data d;
        struct llog_log_hdr *llh = cat_llh->lgh_hdr;
        int rc;
        ENTRY;

        LASSERT(llh->llh_flags & LLOG_F_IS_CAT);
        d.lpd_data = data;
        d.lpd_cb = cb;
        d.lpd_startcat = startcat;
        d.lpd_startidx = startidx;

        if (llh->llh_cat_idx > cat_llh->lgh_last_idx) {
                struct llog_process_cat_data cd;

                CWARN("catlog "DOSTID" crosses index zero\n",
                      POSTID(&cat_llh->lgh_id.lgl_oi));

                cd.lpcd_first_idx = llh->llh_cat_idx;
                cd.lpcd_last_idx = 0;
		rc = llog_process_or_fork(env, cat_llh, llog_cat_process_cb,
					  &d, &cd, fork);
		if (rc != 0)
			RETURN(rc);

		cd.lpcd_first_idx = 0;
		cd.lpcd_last_idx = cat_llh->lgh_last_idx;
		rc = llog_process_or_fork(env, cat_llh, llog_cat_process_cb,
					  &d, &cd, fork);
        } else {
		rc = llog_process_or_fork(env, cat_llh, llog_cat_process_cb,
					  &d, NULL, fork);
        }

        RETURN(rc);
}

int llog_cat_process(const struct lu_env *env, struct llog_handle *cat_llh,
		     llog_cb_t cb, void *data, int startcat, int startidx)
{
	return llog_cat_process_or_fork(env, cat_llh, cb, data, startcat,
					startidx, false);
}
EXPORT_SYMBOL(llog_cat_process);

static int llog_cat_reverse_process_cb(const struct lu_env *env,
				       struct llog_handle *cat_llh,
				       struct llog_rec_hdr *rec, void *data)
{
	struct llog_process_data *d = data;
	struct llog_logid_rec *lir = (struct llog_logid_rec *)rec;
	struct llog_handle *llh;
	struct llog_log_hdr *hdr;
	int rc;

	if (le32_to_cpu(rec->lrh_type) != LLOG_LOGID_MAGIC) {
		CERROR("invalid record in catalog\n");
		RETURN(-EINVAL);
	}
	CDEBUG(D_HA, "processing log "DOSTID":%x at index %u of catalog "
	       DOSTID"\n", POSTID(&lir->lid_id.lgl_oi), lir->lid_id.lgl_ogen,
	       le32_to_cpu(rec->lrh_index), POSTID(&cat_llh->lgh_id.lgl_oi));

	rc = llog_cat_id2handle(env, cat_llh, &llh, &lir->lid_id);
	if (rc) {
		CERROR("%s: cannot find handle for llog "DOSTID": %d\n",
		       cat_llh->lgh_ctxt->loc_obd->obd_name,
		       POSTID(&lir->lid_id.lgl_oi), rc);
		if (rc == -ENOENT || rc == -ESTALE) {
			/* After a server crash, a stub of index
			 * record in catlog could be kept, because
			 * plain log destroy + catlog index record
			 * deletion are not atomic. So we end up with
			 * an index but no actual record. Destroy the
			 * index and move on. */
			rc = llog_cat_cleanup(env, cat_llh, NULL,
					      rec->lrh_index);
		}

		RETURN(rc);
	}

	/* clean old empty llogs, do not consider current llog in use */
	hdr = llh->lgh_hdr;
	if ((hdr->llh_flags & LLOG_F_ZAP_WHEN_EMPTY) &&
	    hdr->llh_count == 1 &&
	    llh != cat_llh->u.chd.chd_current_log) {
		rc = llog_destroy(env, llh);
		if (rc)
			CERROR("%s: fail to destroy empty log: rc = %d\n",
			       llh->lgh_ctxt->loc_obd->obd_name, rc);
		GOTO(out, rc = LLOG_DEL_PLAIN);
	}

	rc = llog_reverse_process(env, llh, d->lpd_cb, d->lpd_data, NULL);

out:
	/* The empty plain was destroyed while processing */
	if (rc == LLOG_DEL_PLAIN)
		rc = llog_cat_cleanup(env, cat_llh, llh,
				      llh->u.phd.phd_cookie.lgc_index);

	llog_handle_put(llh);
	RETURN(rc);
}

int llog_cat_reverse_process(const struct lu_env *env,
			     struct llog_handle *cat_llh,
			     llog_cb_t cb, void *data)
{
        struct llog_process_data d;
        struct llog_process_cat_data cd;
        struct llog_log_hdr *llh = cat_llh->lgh_hdr;
        int rc;
        ENTRY;

        LASSERT(llh->llh_flags & LLOG_F_IS_CAT);
        d.lpd_data = data;
        d.lpd_cb = cb;

	if (llh->llh_cat_idx > cat_llh->lgh_last_idx) {
		CWARN("catalog "DOSTID" crosses index zero\n",
		      POSTID(&cat_llh->lgh_id.lgl_oi));

                cd.lpcd_first_idx = 0;
                cd.lpcd_last_idx = cat_llh->lgh_last_idx;
		rc = llog_reverse_process(env, cat_llh,
					  llog_cat_reverse_process_cb,
					  &d, &cd);
		if (rc != 0)
			RETURN(rc);

		cd.lpcd_first_idx = le32_to_cpu(llh->llh_cat_idx);
		cd.lpcd_last_idx = 0;
		rc = llog_reverse_process(env, cat_llh,
					  llog_cat_reverse_process_cb,
					  &d, &cd);
        } else {
		rc = llog_reverse_process(env, cat_llh,
					  llog_cat_reverse_process_cb,
					  &d, NULL);
        }

        RETURN(rc);
}
EXPORT_SYMBOL(llog_cat_reverse_process);

static int llog_cat_set_first_idx(struct llog_handle *cathandle, int index)
{
        struct llog_log_hdr *llh = cathandle->lgh_hdr;
        int i, bitmap_size, idx;
        ENTRY;

        bitmap_size = LLOG_BITMAP_SIZE(llh);
        if (llh->llh_cat_idx == (index - 1)) {
                idx = llh->llh_cat_idx + 1;
                llh->llh_cat_idx = idx;
                if (idx == cathandle->lgh_last_idx)
                        goto out;
                for (i = (index + 1) % bitmap_size;
                     i != cathandle->lgh_last_idx;
                     i = (i + 1) % bitmap_size) {
                        if (!ext2_test_bit(i, llh->llh_bitmap)) {
                                idx = llh->llh_cat_idx + 1;
                                llh->llh_cat_idx = idx;
                        } else if (i == 0) {
                                llh->llh_cat_idx = 0;
                        } else {
                                break;
                        }
                }
out:
		CDEBUG(D_RPCTRACE, "set catlog "DOSTID" first idx %u\n",
		       POSTID(&cathandle->lgh_id.lgl_oi), llh->llh_cat_idx);
	}

	RETURN(0);
}

/* Cleanup deleted plain llog traces from catalog */
int llog_cat_cleanup(const struct lu_env *env, struct llog_handle *cathandle,
		     struct llog_handle *loghandle, int index)
{
	int rc;

	LASSERT(index);
	if (loghandle != NULL) {
		/* remove destroyed llog from catalog list and
		 * chd_current_log variable */
		down_write(&cathandle->lgh_lock);
		if (cathandle->u.chd.chd_current_log == loghandle)
			cathandle->u.chd.chd_current_log = NULL;
		list_del_init(&loghandle->u.phd.phd_entry);
		up_write(&cathandle->lgh_lock);
		LASSERT(index == loghandle->u.phd.phd_cookie.lgc_index);
		/* llog was opened and keep in a list, close it now */
		llog_close(env, loghandle);
	}
	/* remove plain llog entry from catalog by index */
	llog_cat_set_first_idx(cathandle, index);
	rc = llog_cancel_rec(env, cathandle, index);
	if (rc == 0)
		CDEBUG(D_HA, "cancel plain log at index"
		       " %u of catalog "DOSTID"\n",
		       index, POSTID(&cathandle->lgh_id.lgl_oi));
	return rc;
}

/* helper to initialize catalog llog and process it to cancel */
int llog_cat_init_and_process(const struct lu_env *env,
			      struct llog_handle *llh)
{
	int rc;

	rc = llog_init_handle(env, llh, LLOG_F_IS_CAT, NULL);
	if (rc)
		RETURN(rc);

	RETURN(0);
}
EXPORT_SYMBOL(llog_cat_init_and_process);