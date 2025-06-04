// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <stdint.h>
#include <string.h>

#define u32 uint32_t
#define u64 uint64_t
#include <linux/cvitek_spacc.h>

#define POOL_SIZE (1 * 1024)
#define DATA_SIZE (60 * 1024)
char buf[POOL_SIZE] = { 0 };
int test_base64(int fd);
int test_sha256(int fd);
int test_aes(int fd);
int test_sm4(int fd);
int test_tdes(int fd);
unsigned char key[32] = { 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61,
			  0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61,
			  0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
			  0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61 };
unsigned char iv[16] = { 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61,
			 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61 };
unsigned char __16B_bin[] = { 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x0b, 0x0b, 0x0b,
			      0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b };
int __16B_bin_len = 16;
int main(int argc, char **args)
{
	int fd;
	unsigned int pool_size = POOL_SIZE;

	fd = open("/dev/spacc", O_RDWR);
	if (fd < 0) {
		printf("open /dev/spacc failed\n");
		return -1;
	}

	if (ioctl(fd, IOCTL_SPACC_CREATE_MEMPOOL, &pool_size)) {
		printf("ioctl failed\n");
		return -1;
	}

	pool_size = 0;
	ioctl(fd, IOCTL_SPACC_GET_MEMPOOL_SIZE, &pool_size);
	printf("pool size: %d\n", pool_size);

	// test_base64(fd);
	test_sha256(fd);//OK
	// test_aes(fd); //OK
	// test_sm4(fd); //OK
	// test_tdes(fd); //OK
	// test_sm3(fd);
	close(fd);
	return 0;
}
int test_base64(int fd)
{
	unsigned int result_size;
	int ret;
	struct spacc_base64 b64 = { 0, 1 };
	memset(buf, 0, POOL_SIZE);
	memcpy(buf, "hello", 5);

	// Encode
	write(fd, buf, 5);

	result_size = ioctl(fd, IOCTL_SPACC_BASE64_ACTION, &b64);
	printf("Base64 Encode result_size : %d\n", result_size);
	buf[result_size] = 0;

	ret = read(fd, buf, result_size);
	printf("Base64 Encode ret : %d, %s\n", ret, buf);

	// Decode
	write(fd, buf, result_size);
	b64.action = 0;

	result_size = ioctl(fd, IOCTL_SPACC_BASE64_ACTION, &b64);
	printf("Base64 Decode result_size : %d\n", result_size);
	buf[result_size] = 0;

	ret = read(fd, buf, result_size);
	printf("Base64 Decode ret : %d, %s\n", ret, buf);

	// Customer code Encode
	buf[0] = 0xFB;
	buf[1] = 0xEF;
	buf[2] = 0xBE;

	write(fd, buf, 3);

	b64.customer_code = ('!' << 8) | '#';
	b64.action = 1;
	result_size = ioctl(fd, IOCTL_SPACC_BASE64_ACTION, &b64);
	printf("Base64 Customer code Encode result_size : %d\n", result_size);
	buf[result_size] = 0;

	ret = read(fd, buf, result_size);
	printf("Base64 Customer code Encode ret : %d, %s\n", ret, buf);

	// Customer code Decode
	memcpy(buf, "####", 4);

	write(fd, buf, 4);

	b64.customer_code = ('!' << 8) | '#';
	b64.action = 0;
	result_size = ioctl(fd, IOCTL_SPACC_BASE64_ACTION, &b64);
	printf("Base64 Customer code Decode result_size : %d\n", result_size);
	buf[result_size] = 0;

	ret = read(fd, buf, result_size);
	printf("Base64 Customer code Decode ret : %d, %s\n", ret, buf);

	int i = 0;

	for (; i < 3; i++)
		printf("0x%x ", buf[i]);

	printf("\n");

	return 0;
}
void dump_buf(unsigned char *buf, int size)
{
	int i = 0;
	for (; i < size; i++)
		printf("0x%x ", buf[i]);
	printf("\n");
}
int test_sha256(int fd)
{
	unsigned int result_size;
	int ret;
		// 期望结果
    // i : 0, 0xba4df22c 
    // i : 1, 0xea3b05f 
    // i : 2, 0x2a3be826 
    // i : 3, 0x9ee2b9c5 
    // i : 4, 0x5c1e161b 
    // i : 5, 0x5e42a71f 
    // i : 6, 0x62330473 
    // i : 7, 0x24988b93
	uint8_t data[64] = {0};
	memset(data, 0, 64);
	 // 填充数据
    memcpy(data, "hello", 5);
    data[5] = 0x80; // SHA256 padding
	data[63] = 0x28;
	write(fd, data, 64);
	printf("test_sha256\n");
	ret = ioctl(fd, IOCTL_SPACC_SHA256_ACTION);
	if (ret < 0) {
		printf("ioctl failed\n");
		return -1;
	}
	result_size = read(fd, buf, 32);
	printf("SHA256 result_size : %d\n", result_size);
	dump_buf(buf, result_size);
	return 0;
}

int test_sm3(int fd)
{
	unsigned int result_size;
	int ret;
// 	unsigned char except_result[] = {
//     0xBE,0xCB,0xBF,0xAA,0xE6,0x54,0x8B,0x8B,
//     0xF0,0xCF,0xCA,0xD5,0xA2,0x71,0x83,0xCD,
//     0x1B,0xE6,0x09,0x3B,0x1C,0xCE,0xCC,0xC3,
//     0x03,0xD9,0xC6,0x1D,0x0A,0x64,0x52,0x68
//   };
	uint8_t data[64] = {0};
	memset(data, 0, 64);
	memcpy(data, "hello", 5);
	data[5] = 0x80;
	data[63] = 0x28;
	write(fd, data, 64);
	ret = ioctl(fd, IOCTL_SPACC_SM3_ACTION);
	result_size = read(fd, buf, 32);
	printf("SM3 result_size : %d\n", result_size);
	dump_buf(buf, result_size);
	return 0;
}

int test_aes(int fd)
{
	unsigned int result_size;
	int ret;

	struct spacc_aes_config aes_config = {
		.len = 0,
		.key = key,
		.iv = iv,
		.mode = SPACC_ALGO_MODE_ECB,
		.key_mode = SPACC_KEY_SIZE_128BITS,
		.action = SPACC_ACTION_ENCRYPTION
	};

	write(fd, __16B_bin, __16B_bin_len);
	ret = ioctl(fd, IOCTL_SPACC_AES_ACTION, &aes_config);
	result_size = read(fd, buf, __16B_bin_len);
	printf("AES result_size : %d\n", result_size);
	dump_buf(buf, result_size);

	return 0;
}

int test_sm4(int fd)
{
	unsigned int result_size;
	int ret;

	spacc_sm4_config_s sm4_config = {
					  .len = 0,
					  .key = key,
					  .iv = iv,
					  .mode = SPACC_ALGO_MODE_CBC,
					  .key_mode = SPACC_KEY_SIZE_128BITS,
					  .action = SPACC_ACTION_ENCRYPTION };

	write(fd, __16B_bin, __16B_bin_len);
	ret = ioctl(fd, IOCTL_SPACC_SM4_ACTION, &sm4_config);
	result_size = read(fd, buf, __16B_bin_len);
	printf("SM4 result_size : %d\n", result_size);
	dump_buf(buf, result_size);

	return 0;
}

int test_tdes(int fd)
{
	unsigned int result_size;
	int ret;

	struct spacc_des_config tdes_config = {
		.key = key,//192bits
		.iv = iv,
		.mode = SPACC_ALGO_MODE_CBC,
		.action = SPACC_ACTION_ENCRYPTION
	};
	write(fd, __16B_bin, __16B_bin_len);
	ret = ioctl(fd, IOCTL_SPACC_TDES_ACTION, &tdes_config);
	result_size = read(fd, buf, __16B_bin_len);
	printf("TDES result_size : %d\n", result_size);
	dump_buf(buf, result_size);

	struct spacc_des_config des_config = {
		.key = key,
		.iv = iv,
		.mode = SPACC_ALGO_MODE_CBC,
		.action = SPACC_ACTION_ENCRYPTION
	};
	write(fd, __16B_bin, __16B_bin_len);
	ret = ioctl(fd, IOCTL_SPACC_DES_ACTION, &des_config);
	result_size = read(fd, buf, __16B_bin_len);
	printf("DES result_size : %d\n", result_size);
	dump_buf(buf, result_size);
	return 0;
}