


/*

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/dma-heap.h>
#include <errno.h>

#include "habmm.h"

#include "shm_ops.h"

#define MMID MM_HSI2S_1 //1201

#define CMD_BUF_REGISTER   1
#define CMD_BUF_UNREGISTER 20
struct SharedBufInfo {
	int interface;
	int type;
	int result;
	uint32_t export_id;
	uint32_t size;
	uint32_t index;
};

struct __myBufInfo {
	struct myBufInfo info;
	int handle;
	int dmabuf_fd;
	unsigned int export_id;
};

static struct __myBufInfo __infos[5];

static int g_handle;

static void* alloc_dmabuf_and_map(int buf_size, int *fd)
{
	int heap_fd;
	struct dma_heap_allocation_data data;

	int dmabuf_fd = -1;
	void *buf = NULL;

	heap_fd = open("/dev/dma_heap/qcom,system", O_RDONLY);
	if (heap_fd < 0) {
		perror("open /dev/dma_heap/qcom,system failed, try /dev/dma_heap/system");
		heap_fd = open("/dev/dma_heap/system", O_RDONLY);
	}
	if (heap_fd < 0) {
		perror("open /dev/dma_heap/system");
		return NULL;
	}

	memset(&data, 0, sizeof(data));
	data.len = buf_size;
	data.fd_flags = O_RDWR | O_CLOEXEC;
	data.heap_flags = 0;

	if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &data) < 0) {
		perror("DMA_HEAP_IOCTL_ALLOC");
		close(heap_fd);
		return NULL;
	}
	close(heap_fd);

	dmabuf_fd = (int)data.fd;

	buf = mmap(NULL, buf_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			dmabuf_fd, 0);
	if (buf == MAP_FAILED) {
		perror("mmap dmabuf");
		close(dmabuf_fd);
		buf = NULL;
		return NULL;
	}

	*fd = dmabuf_fd;
	memset(buf, 0, buf_size);
	printf("%s: buf =%p, buf_size=0x%x\n", __func__, buf, buf_size);
	return buf;
}
static void free_dmabuf_and_unmap(void *buf, int buf_size, int dmabuf_fd)
{
	munmap(buf, buf_size);
	close(dmabuf_fd);
}

static int get_mmid(void)
{
	int mmid = MMID;
	const char *path = "/data/hsi2s_mmid.txt";

	if (access("/data/hsi2s_mmid.txt", F_OK) < 0) {
		printf("file %s not exists\n", path);
		return mmid;
	}

	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		printf("open %s failed\n", path);
		return mmid;
	}

	char line[100] = {0};
	if (NULL == fgets(line, sizeof(line), fp)) {
		printf("read %s failed\n", path);
		fclose(fp);
		return mmid;
	}
	fclose(fp);
	mmid = atoi(line);
	return mmid;
}

int shm_setup_channel(void)
{
	int handle = -1;
	int ret = -1;
	int mmid = get_mmid();
	printf("mmid = %d\n", mmid);
	for(int i=0; i<3; i++) {
		ret = habmm_socket_open(&handle, mmid, 5000, 0);
		if (ret) {
			printf("GVM: habmm_socket_open ret=%d\n", ret);
		} else {
			break;
		}
	}
	if (ret) {
		return -1;
	}
	printf("GVM: HAB socket opened, handle=0x%x\n", handle);
	g_handle = handle;

	return handle;
}

void shm_shutdown_channel(int handle)
{
	if (handle >= 0) {
		habmm_socket_close(handle);
	}
	g_handle = 0;
}

int shm_notify_peer(void *buf, int buf_len)
{
	int handle = g_handle;
	int ret;

	ret = habmm_socket_send(handle, buf, buf_len, 0);
	if (ret) {
		printf("%s: habmm_socket_send ret=%d\n", __func__, ret);
		return ret;
	}

	unsigned int size = buf_len;
	ret = habmm_socket_recv(handle, buf, &size, -1, 0);
	if (ret) {
		printf("%s: habmm_socket_recv ret=%d\n", __func__, ret);
		return ret;
	}

	return 0;
}
/**
 **/
struct myBufInfo* create_shm(int interface, int buf_size)
{
	int handle = g_handle;
	int ret;

	printf("%s: handle=0x%x, buf_size=0x%x \n", __func__, handle, buf_size);
	int dmabuf_fd = -1;
	void *buf = alloc_dmabuf_and_map(buf_size, &dmabuf_fd);
	if (NULL == buf) {
		printf("alloc_dmabuf_and_map failed\n");
		return NULL;
	}
	printf("%s: buf=%p, dmabuf_fd=%d\n", __func__, buf, dmabuf_fd);

	unsigned int export_id = 0;
	printf("%s: handle = 0x%x, buf=%p, buf_size=0x%x\n", __func__, handle, buf, buf_size);
	ret = habmm_export(handle,
			buf,
			buf_size,
			&export_id,
			HABMM_EXP_MEM_TYPE_DMA);
	if (ret) {
		printf("%s: habmm_export ret=%d\n", __func__, ret);
		free_dmabuf_and_unmap(buf, buf_size, dmabuf_fd);
		return NULL;
	}
	printf("%s: export done, export_id=%u, size=0x%x\n", __func__, export_id, buf_size);

	struct SharedBufInfo msg = {
		.interface = interface,
		.type = CMD_BUF_REGISTER,
		.result = 0,
		.export_id = export_id,
		.size = buf_size,
		.index = 0,
	};

	uint32_t size = sizeof(struct SharedBufInfo);
	ret = habmm_socket_send(handle, &msg, size, 0);
	if (ret) {
		printf("%s: send connect ret=%d\n", __func__, ret);
		habmm_unexport(handle, export_id, 0);
		free_dmabuf_and_unmap(buf, buf_size, dmabuf_fd);
		return NULL;
	}
	printf("%s: sent connect \n", __func__);
	ret = habmm_socket_recv(handle, &msg, &size, -1, 0);
	if (msg.type != CMD_BUF_REGISTER || size != sizeof(struct SharedBufInfo)) {
		printf("%s: recv cmd not match : type %d, size %d\n", __func__, msg.type, size);
		habmm_unexport(handle, export_id, 0);
		free_dmabuf_and_unmap(buf, buf_size, dmabuf_fd);
		return NULL;
	}
	printf("%s: sent connect result %d\n", __func__, msg.result);

	__infos[interface].info.interface = interface;
	__infos[interface].info.buf = buf;
	__infos[interface].info.buf_size = buf_size;

	__infos[interface].handle = handle;
	__infos[interface].dmabuf_fd = dmabuf_fd;
	__infos[interface].export_id = export_id;

	return &__infos[interface].info;
}

struct myBufInfo* get_shm(void *cmd)
{
	int ret;

	struct SharedBufInfo *msg = cmd;
	int interface = msg->interface;
	int handle = g_handle;

	printf("%s: handle=0x%x\n", __func__, handle);
	printf("msg: type %d, size %d, export_id %d\n", msg->type, msg->size, msg->export_id);

	printf("import share buf info ...\n");
	void *buf = NULL;
	ret = habmm_import(handle,
			&buf,
			msg->size,
			msg->export_id,
			0);
	if (ret) {
		printf("%s: habmm_import ret=%d\n", __func__, ret);
		return NULL;
	}

	__infos[interface].info.interface = interface;
	__infos[interface].info.buf = buf;
	__infos[interface].info.buf_size = msg->size;

	__infos[interface].handle = handle;
	__infos[interface].export_id = msg->export_id;
	return &__infos[interface].info;
}

/**
 **/
void destroy_shm(struct myBufInfo *info)
{
	int handle = g_handle;
	int interface = info->interface;
	unsigned int export_id = __infos[interface].export_id;
	void *buf = __infos[interface].info.buf;
	int buf_size = __infos[interface].info.buf_size;
	int dmabuf_fd = __infos[interface].dmabuf_fd;

	struct SharedBufInfo msg = {
		.interface = interface,
		.type = CMD_BUF_UNREGISTER,
		.result = 0,
		.export_id = export_id,
		.size = buf_size,
		.index = 0,
	};

	uint32_t size = sizeof(struct SharedBufInfo);
	int ret = habmm_socket_send(handle, &msg, size, 0);
	if (ret) {
		printf("%s: send disconnect ret=%d\n", __func__, ret);
	} else {
		printf("%s: sent disconnect\n", __func__);
		ret = habmm_socket_recv(handle, &msg, &size, -1, 0);
		printf("%s: sent disconnect result %d\n", __func__, msg.result);
	}

	if (export_id) {
		habmm_unexport(handle, export_id, 0);
	}
	free_dmabuf_and_unmap(buf, buf_size, dmabuf_fd);

	__infos[interface].info.buf = NULL;
}


int Register_cmd_handler(void *cmd, unsigned int cmd_size, int (*Cmdhandler)(void *arg, void *cmd), void *arg)
{
	int handle = -1;
	int ret;

	char *name = arg;
	int mmid = get_mmid();
	printf("%s: mmid = %d\n", __func__, mmid);
	do {
		ret = habmm_socket_open(&handle, mmid, 5000, 0);
		if (ret == -ETIMEDOUT) {
			continue;
		}
		printf("%s: habmm_socket_open ret=%d\n", __func__, ret);
		if (ret == -EINTR) {
			continue;
		}
		if (ret) {
			return ret;
		}
	} while(ret != 0);
	printf("%s: HAB socket opened, handle=%d, name:%s\n", __func__, handle, name);
	g_handle = handle;

	while (1) {
		unsigned int size = cmd_size;
		ret = habmm_socket_recv(handle, cmd, &size, -1, 0);
		if (ret) {
			printf("%s(%d) ret=%d\n", __func__, __LINE__, ret);
			break;
		}

		ret = Cmdhandler(arg, cmd);

		ret = habmm_socket_send(handle, cmd, size, 0);
		if (ret) {
			printf("%s: send cmd result failed:  ret=%d\n", __func__, ret);
			break;
		}
	}

	habmm_socket_close(handle);

	printf("%s: CMD thread exit, name:%s\n", __func__, name);
	return ret;
}

