/*
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
*/
#ifndef __SHM_OPS_H__
#define __SHM_OPS_H__
struct myBufInfo {
	int interface;
	int type;
	unsigned int buf_size;
	void *buf;
};
//
int shm_setup_channel(void);
void shm_shutdown_channel(int handle);
int shm_notify_peer(void *buf, int buf_len);

struct myBufInfo* create_shm(int interface, int buf_size);
void destroy_shm(struct myBufInfo *info);

//
int Register_cmd_handler(void *cmd, unsigned int cmd_size, int (*Cmdhandler)(void *arg, void *cmd), void *arg);
struct myBufInfo* get_shm(void *cmd);
#endif
