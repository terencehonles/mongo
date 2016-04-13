/*-
 * Public Domain 2014-2016 MongoDB, Inc.
 * Public Domain 2008-2014 WiredTiger, Inc.
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "format.h"

/*
 * check_copy --
 *	Confirm the backup worked.
 */
static void
check_copy(void)
{
	WT_CONNECTION *conn;
	WT_SESSION *session;

	wts_open(g.home_backup, 0, &conn);

	testutil_checkfmt(
	    conn->open_session(conn, NULL, NULL, &session),
	    "%s", g.home_backup);

	testutil_checkfmt(
	    session->verify(session, g.uri, NULL),
	    "%s: %s", g.home_backup, g.uri);

	testutil_checkfmt(conn->close(conn, NULL), "%s", g.home_backup);
}

/*
 * copy_file --
 *	Copy a single file into the backup directories.
 */
static void
copy_file(WT_SESSION *session, const char *name)
{
	size_t len;
	char *to;

	len = strlen("BACKUP") + strlen(name) + 10;
	to = dmalloc(len);

	(void)snprintf(to, len, "BACKUP/%s", name);
	testutil_check(__wt_copy_and_sync(session, name, to));

	(void)snprintf(to, len, "BACKUP_COPY/%s", name);
	testutil_check(__wt_copy_and_sync(session, name, to));

	free(to);
}

/*
 * backup --
 *	Periodically do a backup and verify it.
 */
void *
backup(void *arg)
{
	WT_CONNECTION *conn;
	WT_CURSOR *backup_cursor;
	WT_DECL_RET;
	WT_SESSION *session;
	u_int period;
	const char *key;

	(void)(arg);

	conn = g.wts_conn;

	/* Backups aren't supported for non-standard data sources. */
	if (DATASOURCE("helium") || DATASOURCE("kvsbdb"))
		return (NULL);

	/* Open a session. */
	testutil_check(conn->open_session(conn, NULL, NULL, &session));

	/*
	 * Perform a backup at somewhere under 10 seconds (so we get at
	 * least one done), and then at 45 second intervals.
	 */
	for (period = mmrand(NULL, 1, 10);; period = 45) {
		/* Sleep for short periods so we don't make the run wait. */
		while (period > 0 && !g.workers_finished) {
			--period;
			sleep(1);
		}

		/* Lock out named checkpoints */
		testutil_check(pthread_rwlock_wrlock(&g.backup_lock));
		if (g.workers_finished) {
			testutil_check(pthread_rwlock_unlock(&g.backup_lock));
			break;
		}

		/* Re-create the backup directory. */
		testutil_checkfmt(
		    system(g.home_backup_init),
		    "%s", "backup directory creation failed");

		/*
		 * open_cursor can return EBUSY if a metadata operation is
		 * currently happening - retry in that case.
		 */
		while ((ret = session->open_cursor(session,
		    "backup:", NULL, NULL, &backup_cursor)) == EBUSY)
			sleep(1);
		if (ret != 0)
			testutil_die(ret, "session.open_cursor: backup");

		while ((ret = backup_cursor->next(backup_cursor)) == 0) {
			testutil_check(
			    backup_cursor->get_key(backup_cursor, &key));
			copy_file(session, key);
		}
		if (ret != WT_NOTFOUND)
			testutil_die(ret, "backup-cursor");

		testutil_check(backup_cursor->close(backup_cursor));
		testutil_check(pthread_rwlock_unlock(&g.backup_lock));

		check_copy();
	}

	testutil_check(session->close(session, NULL));

	return (NULL);
}
