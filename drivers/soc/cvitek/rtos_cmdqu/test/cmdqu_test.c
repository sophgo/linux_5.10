// SPDX-License-Identifier: GPL-2.0-only

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define RTOS_CMDQU_DEV_NAME "/dev/cvi-rtos-cmdqu"

struct valid_t {
	unsigned char linux_valid;
	unsigned char rtos_valid;
};

union resv_t {
	struct valid_t valid;
	unsigned short mstime;
};

struct cmdqu_t {
	unsigned char ip_id;
	unsigned char cmd_id : 7;
	unsigned char block : 1;
	union resv_t resv;
	unsigned int param_ptr;
};

struct cmdqu_t_size_check {
	int x[(sizeof(struct cmdqu_t) == 8) ? 1 : -1];
};

enum SYSTEM_CMD_TYPE {
	CMDQU_SEND = 1,
	CMDQU_REQUEST,
	CMDQU_REQUEST_FREE,
	CMDQU_SEND_WAIT,
	CMDQU_SEND_WAKEUP,
	CMDQU_SYSTEM_LIMIT,
};

#define RTOS_CMDQU_SEND_WAIT _IOW('r', CMDQU_SEND_WAIT, unsigned long)

enum IP_TYPE {
	IP_ISP = 0,
	IP_VCODEC,
	IP_VIP,
	IP_VI,
	IP_RGN,
	IP_AUDIO,
	IP_SYSTEM,
	IP_CAMERA,
	IP_LIMIT,
};

enum SYS_CMD_ID {
	SYS_CMD_INFO_TRANS = 0x50,
	SYS_CMD_INFO_LINUX_INIT_DONE,
	SYS_CMD_INFO_RTOS_INIT_DONE,
	SYS_CMD_INFO_STOP_ISR,
	SYS_CMD_INFO_STOP_ISR_DONE,
	SYS_CMD_INFO_LINUX,
	SYS_CMD_INFO_RTOS,
	SYS_CMD_INFO_SYNC_TIME,
	SYS_CMD_INFO_DUMP_MSG,
	SYS_CMD_INFO_DUMP_EN,
	SYS_CMD_INFO_DUMP_DIS,
	SYS_CMD_INFO_DUMP_JPG,
	SYS_CMD_INFO_TRACE_SNAPSHOT_START,
	SYS_CMD_INFO_TRACE_SNAPSHOT_STOP,
	SYS_CMD_INFO_TRACE_STREAM_START,
	SYS_CMD_INFO_TRACE_STREAM_STOP,
	SYS_CMD_INFO_LIMIT,
};

static int read_irq101_count(void)
{
	FILE *f = fopen("/proc/interrupts", "r");
	char line[512];
	int count = -1;

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		char irq[16], agent1[32], agent2[32], name[32];
		int hwirq, n;
		unsigned long cnt;

		n = sscanf(line, "%15s %lu %31s %31s %d %31s",
			   irq, &cnt, agent1, agent2, &hwirq, name);
		if (n == 6 && hwirq == 101 && !strcmp(name, "mailbox")) {
			count = (int)cnt;
			break;
		}
	}
	fclose(f);
	return count;
}

int main(int argc, char *argv[])
{
	struct cmdqu_t cmdq;
	int fd, fail = 0;
	int before, after;
	int timeout_ms = 3000;

	if (argc > 1)
		timeout_ms = atoi(argv[1]);
	if (timeout_ms == 256) {
		printf("[INFO] timeout=256ms conflicts with rtos_valid check, using 3000\n");
		timeout_ms = 3000;
	}

	fd = open(RTOS_CMDQU_DEV_NAME, O_RDWR);
	if (fd < 0) {
		printf("[FAIL] open %s: %s\n", RTOS_CMDQU_DEV_NAME, strerror(errno));
		return 1;
	}
	printf("[PASS] open %s\n", RTOS_CMDQU_DEV_NAME);

	before = read_irq101_count();
	if (before < 0) {
		printf("[FAIL] parse /proc/interrupts (PLIC 101 mailbox)\n");
		close(fd);
		return 1;
	}
	printf("[PASS] irq101 count before = %d\n", before);

	memset(&cmdq, 0, sizeof(cmdq));
	cmdq.ip_id = IP_SYSTEM;
	cmdq.cmd_id = SYS_CMD_INFO_DUMP_MSG;
	cmdq.param_ptr = 0;
	cmdq.resv.mstime = (unsigned short)timeout_ms;

	printf("[INFO] SEND_WAIT ip_id=%d cmd_id=0x%x mstime=%dms ...\n",
	       cmdq.ip_id, cmdq.cmd_id, timeout_ms);

	ioctl(fd, RTOS_CMDQU_SEND_WAIT, &cmdq);

	printf("[INFO] reply: ip_id=%d cmd_id=0x%x block=%d rtos_valid=%d param_ptr=0x%x\n",
	       cmdq.ip_id, cmdq.cmd_id, cmdq.block,
	       cmdq.resv.valid.rtos_valid, cmdq.param_ptr);

	if (cmdq.ip_id == IP_SYSTEM && cmdq.cmd_id == SYS_CMD_INFO_DUMP_MSG &&
	    cmdq.block == 1 && cmdq.resv.valid.rtos_valid == 1) {
		printf("[PASS] reply payload (ip/cmd/block/rtos_valid)\n");
	} else {
		printf("[FAIL] reply payload mismatch (timeout? check dmesg SEND_WAIT timeout)\n");
		fail = 1;
	}

	after = read_irq101_count();
	if (after == before + 1) {
		printf("[PASS] irq101 count after = %d (+1)\n", after);
	} else {
		printf("[FAIL] irq101 count after = %d (expect %d)\n", after, before + 1);
		fail = 1;
	}

	close(fd);
	printf("RESULT: %s\n", fail ? "FAIL" : "PASS");
	return fail;
}
