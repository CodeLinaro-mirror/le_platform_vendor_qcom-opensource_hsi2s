
/*
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <semaphore.h>
#include <pthread.h>
#include "habmm.h"

#include "hsi2s_umd_protocol.h"
#include "shm_ops.h"

int g_stop = 0;
void sigint_handler(int signo)
{
	(void)signo;
	g_stop = 1;
	exit(0);
}

#define BUF_SIZE (4 *1024 *1024)
struct intf_priv {
	int interface;

	volatile unsigned char *txva;
	volatile unsigned char *rxva;
	volatile unsigned char *ctrl;

	sem_t sem_tx_full;
	sem_t sem_tx_empt;

	int data_tx_thread_stop;
	int data_rx_thread_stop;

	long long ts;
};

static struct intf_priv gprivs[5];

void *data_tx_thread(void *arg)
{
	struct intf_priv *priv = arg;
	int interface = priv->interface;
	struct client_cmd cmd = {0};
	int ret;

	while(1) {
		sem_wait(&priv->sem_tx_full);
		if(priv->data_tx_thread_stop) {
			sem_post(&priv->sem_tx_empt);
			break;
		}
		unsigned char buf[4];
		memcpy(buf, priv->txva, 4);
		printf("interface %d: %s: send: %.2x %.2x %.2x %.2x\n", interface, __func__, buf[0], buf[1], buf[2], buf[3]);
		app_write_to_umd(priv->interface, priv->txva, BUF_SIZE, 0);
		sem_post(&priv->sem_tx_empt);
	}

	priv->data_tx_thread_stop = 0;
	printf("interface %d: %s: TX thread exit\n", interface, __func__);

	return NULL;
}

static int consume(void *arg, unsigned char *data, int data_len, long long ts)
{
	struct intf_priv *priv = arg;
	int interface = priv->interface;
	unsigned int* ctrl = (unsigned int*)(priv->ctrl);

	printf("interface %d: %s: len(%d) %.2x %.2x %.2x %.2x\n", interface, __func__, data_len, data[0], data[1], data[2], data[3]);
	if (ts < priv->ts) {
		printf("dropped previous data(%lu < %lu )\n", ts, priv->ts);
		return 0;
	}
	ctrl[1] = data_len;
	memcpy(priv->rxva, data, data_len);
	ctrl[0] += 1;

	while(ctrl[1] != 0) {
		struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 }; // 100 ms
		nanosleep(&ts, NULL);
	}

	printf("interface %d: %s: done ctrl[0] = %u\n", interface, __func__, ctrl[0]);
	if (priv->data_rx_thread_stop) return 1;
	return 0;
}

void *data_rx_thread(void *arg)
{
	struct intf_priv *priv = arg;
	priv->ts = time(NULL);
	app_read_from_umd(priv->interface, consume, arg);
	printf("interface %d: %s: RX thread exit\n", priv->interface, __func__);
	priv->data_rx_thread_stop = 0;
	return NULL;
}

int handle_client_cmd(void *arg, struct client_cmd *cmd)
{
	int interface = cmd->interface;
	int ret;

	printf("interface %d: %s: got command: type 0x%x\n", interface, __func__, cmd->type);
	if (cmd->type == UMD_TEST) {
		struct intf_priv *priv = &gprivs[interface];
		char buf[4];
		memcpy(buf, &cmd->param, 4);
		printf("interface %d: buf: %.2x %.2x %.2x %.2x\n", interface, buf[0], buf[1], buf[2], buf[3]);
		sem_post(&priv->sem_tx_full);
		sem_wait(&priv->sem_tx_empt);
		cmd->result = 0;
		return 0;
	}

	if (cmd->type == UMD_CONNECT) {
		struct intf_priv *priv = &gprivs[interface];

		struct myBufInfo *info = get_shm(cmd);
		if (!info) {
			printf("%s: get_shm data failed\n", __func__);
			cmd->result = -1;
			return 0;
		}
		memset(priv, 0, sizeof(*priv));
		priv->interface = interface;

		priv->txva = info->buf;
		priv->rxva = priv->txva + BUF_SIZE;
		priv->ctrl = priv->rxva + BUF_SIZE;
		sem_init(&priv->sem_tx_full, 0, 0);
		sem_init(&priv->sem_tx_empt, 0, 0);

		pthread_t th_data_tx;
		pthread_create(&th_data_tx, NULL, data_tx_thread, priv);
		pthread_t th_data_rx;
		pthread_create(&th_data_rx, NULL, data_rx_thread, priv);
		cmd->result = 0;
		return 0;
	}

	if (cmd->type == UMD_DISCONNECT) {
		struct intf_priv *priv = &gprivs[interface];
		printf("stop tx/rx\n");
		priv->data_tx_thread_stop = 1;
		priv->data_rx_thread_stop = 1;

		sem_post(&priv->sem_tx_full);
		sem_wait(&priv->sem_tx_empt);

		sem_destroy(&priv->sem_tx_full);
		sem_destroy(&priv->sem_tx_empt);
		cmd->result = 0;
		return 0;
	}

	cmd->result = apptoumd(cmd);
	printf("interface %d: %s: command: type 0x%x, result %d\n", interface, __func__, cmd->type, cmd->result);

	return 0;
}

void *cmd_thread(void *arg)
{
	struct client_cmd cmd;

	while (1) {
		Register_cmd_handler(&cmd, sizeof(cmd), handle_client_cmd, arg);
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	printf("version: 1\n");
	signal(SIGINT, sigint_handler);

	pthread_t th_cmd_0;
	pthread_create(&th_cmd_0,  NULL, cmd_thread, "cmd thread0");

	pthread_t th_cmd_1;
	pthread_create(&th_cmd_1,  NULL, cmd_thread, "cmd thread1");

	pthread_join(th_cmd_0, NULL);
	pthread_join(th_cmd_1, NULL);

	return 0;
}
