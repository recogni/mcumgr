/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <zephyr.h>
#include <fs/fs.h>
#include <mgmt/mgmt.h>
#include <fs_mgmt/fs_mgmt_impl.h>

int
fs_mgmt_impl_filelen(const char *path, size_t *out_len)
{
    struct fs_dirent dirent;
    int rc;

    rc = fs_stat(path, &dirent);
    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    }

    if (dirent.type != FS_DIR_ENTRY_FILE) {
        return MGMT_ERR_EUNKNOWN;
    }

    *out_len = dirent.size;

    return 0;
}

int
fs_mgmt_impl_read(const char *path, size_t offset, size_t len,
                  void *out_data, size_t *out_len)
{
    struct fs_file_t file;
    ssize_t bytes_read;
    int rc;

    fs_file_t_init(&file);
    rc = fs_open(&file, path, FS_O_READ);
    if (rc != 0) {
        return MGMT_ERR_ENOENT;
    }

    rc = fs_seek(&file, offset, FS_SEEK_SET);
    if (rc != 0) {
        goto done;
    }

    bytes_read = fs_read(&file, out_data, len);
    if (bytes_read < 0) {
        goto done;
    }

    *out_len = bytes_read;

done:
    fs_close(&file);

    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    } else {
        return 0;
    }
}

static int
zephyr_fs_mgmt_truncate(const char *path)
{
    size_t len;
    int rc;

    /* Attempt to get the length of the file at the specified path.  This is a
     * quick way to determine if there is already a file there.
     */
    rc = fs_mgmt_impl_filelen(path, &len);
    if (rc == 0) {
        /* There is already a file with the specified path.  Unlink it to
         * simulate a truncate operation.
         *
         * XXX: This isn't perfect - if the file is currently open, the unlink
         * operation won't actually delete the file.  Consequently, the file
         * will get partially overwritten rather than truncated.  The NFFS port
         * doesn't support the truncate operation, so this is an imperfect
         * workaround.
         */
        rc = fs_unlink(path);
        if (rc != 0) {
            return MGMT_ERR_EUNKNOWN;
        }
    }

    return 0;
}

int
fs_mgmt_impl_write(const char *path, size_t offset, const void *data,
                   size_t len)
{
    static struct fs_file_t file;
    static char *previous_path = NULL;
    int rc;

    /* If this is the write of the first chunk or there isn't a previously-opened
     * file path or this write is for a different file path than that previous one...
     */
    if (offset == 0 || previous_path == NULL || strcmp(path, previous_path) != 0) {
        /* If there is a previously-opened file path, close the
         * file and free the storage allocated for the file path
         */
        if (previous_path != NULL) {
            fs_close(&file);
            k_free(previous_path);
            previous_path = NULL;
        }

        /* Truncate the file before writing the first chunk.  This is done to
         * properly handle an overwrite of an existing file.
         *
         */
        if (offset == 0) {
            rc = zephyr_fs_mgmt_truncate(path);
            if (rc != 0) {
                return rc;
            }
        }

        fs_file_t_init(&file);
        rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
        if (rc != 0) {
            return MGMT_ERR_EUNKNOWN;
        }

        /* Allocate storage for the opened file path and and duplicate it */
        previous_path = k_malloc(strlen(path) + 1);
        if (previous_path == NULL) {
            fs_close(&file);
            return MGMT_ERR_ENOMEM;
        }
        strcpy(previous_path, path);
    }

    rc = fs_seek(&file, offset, FS_SEEK_SET);
    if (rc != 0) {
        goto done;
    }

    rc = fs_write(&file, data, len);
    if (rc < 0) {
        goto done;
    }

done:
    rc = fs_sync(&file);
    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    } else {
        return 0;
    }
}
