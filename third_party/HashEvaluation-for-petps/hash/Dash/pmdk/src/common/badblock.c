/*
 * Copyright 2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * badblock.c - common part of implementation of bad blocks API
 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>

#include "file.h"
#include "os.h"
#include "out.h"
#include "extent.h"
#include "os_badblock.h"
#include "badblock.h"

/*
 * badblocks_new -- zalloc bad blocks structure
 */
struct badblocks *
badblocks_new(void)
{
	LOG(3, " ");

	struct badblocks *bbs = Zalloc(sizeof(struct badblocks));
	if (bbs == NULL) {
		ERR("!Zalloc");
	}

	return bbs;
}

/*
 * badblocks_delete -- free bad blocks structure
 */
void
badblocks_delete(struct badblocks *bbs)
{
	LOG(3, "badblocks %p", bbs);

	if (bbs == NULL)
		return;

	Free(bbs->bbv);
	Free(bbs);
}

/* helper structure for badblocks_check_file_cb() */
struct check_file_cb {
	int n_files_bbs;	/* number of files with bad blocks */
	int create;		/* poolset is just being created */
};

/*
 * badblocks_check_file_cb -- (internal) callback checking bad blocks
 *                               in the given file
 */
static int
badblocks_check_file_cb(struct part_file *pf, void *arg)
{
	LOG(3, "part_file %p arg %p", pf, arg);

	struct check_file_cb *pcfcb = arg;

	if (pf->is_remote) {
		/*
		 * Remote replicas are checked for bad blocks
		 * while opening in util_pool_open_remote().
		 */
		return 0;
	}

	int exists = util_file_exists(pf->part->path);
	if (exists < 0)
		return -1;

	if (!exists)
		/* the part does not exist, so it has no bad blocks */
		return 0;

	int ret = os_badblocks_check_file(pf->part->path);
	if (ret < 0) {
		ERR("checking the pool file for bad blocks failed -- '%s'",
			pf->part->path);
		return -1;
	}

	if (ret > 0) {
		ERR("part file contains bad blocks -- '%s'", pf->part->path);
		pcfcb->n_files_bbs++;
		pf->part->has_bad_blocks = 1;
	}

	return 0;
}

/*
 * badblocks_check_poolset -- checks if the pool set contains bad blocks
 *
 * Return value:
 * -1 error
 *  0 pool set does not contain bad blocks
 *  1 pool set contains bad blocks
 */
int
badblocks_check_poolset(struct pool_set *set, int create)
{
	LOG(3, "set %p create %i", set, create);

	struct check_file_cb cfcb;

	cfcb.n_files_bbs = 0;
	cfcb.create = create;

	if (util_poolset_foreach_part_struct(set, badblocks_check_file_cb,
						&cfcb)) {
		return -1;
	}

	if (cfcb.n_files_bbs) {
		LOG(1, "%i pool file(s) contain bad blocks", cfcb.n_files_bbs);
		set->has_bad_blocks = 1;
	}

	return (cfcb.n_files_bbs > 0);
}

/*
 * badblocks_clear_poolset_cb -- (internal) callback clearing bad blocks
 *                                  in the given file
 */
static int
badblocks_clear_poolset_cb(struct part_file *pf, void *arg)
{
	LOG(3, "part_file %p arg %p", pf, arg);

	int *create = arg;

	if (pf->is_remote) { /* XXX not supported yet */
		LOG(1,
			"WARNING: clearing bad blocks in remote replicas is not supported yet -- '%s:%s'",
			pf->remote->node_addr, pf->remote->pool_desc);
		return 0;
	}

	if (*create) {
		/*
		 * Poolset is just being created - check if file exists
		 * and if we can read it.
		 */
		int exists = util_file_exists(pf->part->path);
		if (exists < 0)
			return -1;

		if (!exists)
			return 0;
	}

	int ret = os_badblocks_clear_all(pf->part->path);
	if (ret < 0) {
		ERR("clearing bad blocks in the pool file failed -- '%s'",
			pf->part->path);
		errno = EIO;
		return -1;
	}

	pf->part->has_bad_blocks = 0;

	return 0;
}

/*
 * badblocks_clear_poolset -- clears bad blocks in the pool set
 */
int
badblocks_clear_poolset(struct pool_set *set, int create)
{
	LOG(3, "set %p create %i", set, create);

	if (util_poolset_foreach_part_struct(set, badblocks_clear_poolset_cb,
						&create)) {
		return -1;
	}

	set->has_bad_blocks = 0;

	return 0;
}

/*
 * badblocks_recovery_file_alloc -- allocate name of bad block recovery file,
 *                                  the allocated name has to be freed
 *                                  using Free()
 */
char *
badblocks_recovery_file_alloc(const char *file, unsigned rep, unsigned part)
{
	LOG(3, "file %s rep %u part %u", file, rep, part);

	char bbs_suffix[64];
	char *path;

	sprintf(bbs_suffix, "_r%u_p%u_badblocks.txt", rep, part);

	size_t len_file = strlen(file);
	size_t len_bbs_suffix = strlen(bbs_suffix);
	size_t len_path = len_file + len_bbs_suffix;

	path = Malloc(len_path + 1);
	if (path == NULL) {
		ERR("!Malloc");
		return NULL;
	}

	strcpy(path, file);
	strcat(path, bbs_suffix);

	return path;
}

/*
 * badblocks_recovery_file_exists -- check if any bad block recovery file exists
 *
 * Returns:
 *    0 when there are no bad block recovery files and
 *    1 when there is at least one bad block recovery file.
 */
int
badblocks_recovery_file_exists(struct pool_set *set)
{
	LOG(3, "set %p", set);

	int recovery_file_exists = 0;

	for (unsigned r = 0; r < set->nreplicas; ++r) {
		struct pool_replica *rep = set->replica[r];

		/* XXX: not supported yet */
		if (rep->remote)
			continue;

		for (unsigned p = 0; p < rep->nparts; ++p) {
			const char *path = PART(rep, p)->path;

			int exists = util_file_exists(path);
			if (exists < 0)
				return -1;

			if (!exists) {
				/* part file does not exist - skip it */
				continue;
			}

			char *rec_file =
				badblocks_recovery_file_alloc(set->path, r, p);
			if (rec_file == NULL) {
				LOG(1,
					"allocating name of bad block recovery file failed");
				return -1;
			}

			exists = util_file_exists(rec_file);
			if (exists < 0) {
				Free(rec_file);
				return -1;
			}

			if (exists) {
				LOG(3, "bad block recovery file exists: %s",
					rec_file);

				recovery_file_exists = 1;
			}

			Free(rec_file);

			if (recovery_file_exists)
				return 1;
		}
	}

	return 0;
}
