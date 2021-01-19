// tdusb.c

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "td-usb.h"
#include "tdhid.h"
#include "tdtimer.h"

unsigned char buffer[MAX_REPORT_LENGTH + 1];

td_device_t *dt;
int *handle;

char *arg_model_name, *arg_operation;
int arg_model_name_length;
int option_format = OPTION_FORMAT_SIMPLE;
int option_loop = FALSE;
int option_interval = OPTION_DEFAULT_INTERVAL;


static void print_report(void)
{
	int err;

	memset(buffer, 0, dt->report_size + 1);

	if (strcmp(arg_operation, "read") == 0)
	{
		err = TdHidGetReport(handle, buffer, dt->report_size + 1, dt->input_report_type);
	}
	else
	{
		err = TdHidListenReport(handle, buffer, dt->report_size + 1);
	}
	
	if (err)
	{
		fprintf(stderr, "USB I/O Error.\n");
		TdHidCloseDevice(handle);
		delete_device_type(dt);
		exit(EXITCODE_DEVICE_IO_ERROR);
	}

	if (option_format == OPTION_FORMAT_RAW)
	{
		for (int i = 0; i < dt->report_size; i++) {
			if (i > 0) printf(",");
			printf("%02X", buffer[i + 1]);
		}
		printf("\n");
	}
	else if (dt->print_report != NULL)
	{
		dt->print_report(option_format, buffer);
	}
	else
	{
		fprintf(stderr, "Read operation is not supported on this device.\n");
	}
}

static int write_report(const char *data_string)
{
	if (option_format == OPTION_FORMAT_RAW)
	{
		fprintf(stderr, "Raw format is not supported yet.\n");
		return 1;
	}
	else if (dt->prepare_report != NULL)
	{
		if (dt->prepare_report(option_format, data_string, buffer))
		{
			fprintf(stderr, "Error on parsing request string.\n");
			return 3;
		}
	}
	else
	{
		fprintf(stderr, "The operation is not supported on this device.\n");
		return 1;
	}
	
	if (TdHidSetReport(handle, buffer, dt->report_size + 1, dt->output_report_type))
	{
		fprintf(stderr, "USB I/O Error.\n");
		return 2;
	}

	return 0;
}

static void print_usage(void)
{
	printf("td-usb version 0.1\n");
	printf("Copyright (C) 2020-2021 Tokyo Devices, Inc. (tokyodevices.jp)\n");
	printf("Usage: td-usb model_name[:serial] operation [options]\n");
	printf("Visit https://github.com/tokyodevices/td-usb/ for details\n");
}

static void TimerCallback(void) { print_report(); }

void errExit(int exitcode, const char *msg)
{
	if(msg != NULL) fprintf(stderr, "%s\n", msg);

	if (handle != NULL) {
		TdHidCloseDevice(handle); handle = NULL;
	}

	if (dt != NULL) {
		delete_device_type(dt);
		dt = NULL;
	}

	exit(exitcode);
}

int main(int argc, char *argv[])
{
	char *p;

	if (argc < 3) { print_usage(); return 1; }

	arg_model_name = argv[1];
	p = strchr(arg_model_name, ':');
	arg_model_name_length = (p == NULL) ? strlen(arg_model_name) : (p - arg_model_name);

	// Get device type object by model name string
	dt = import_device_type(arg_model_name, arg_model_name_length);
	if (dt == 0) 
	{
		fprintf(stderr, "Unknown model name: %s\n", arg_model_name);
		return EXITCODE_UNKNOWN_DEVICE;
	}
		
	arg_operation = argv[2];
	if (strcmp(arg_operation, "list") == 0)
	{
		int len = TdHidListDevices(dt->vendor_id, dt->product_id, dt->product_name, NULL, 0);
		p = (char *)malloc(len);
		TdHidListDevices(dt->vendor_id, dt->product_id, dt->product_name, p, len);
		printf("%s\n", p);
		free(p);
		handle = NULL;
		return EXITCODE_NO_ERROR;
	}

	p = strchr(argv[1], ':');
	handle = TdHidOpenDevice(dt->vendor_id, dt->product_id, dt->product_name, (p == NULL) ? NULL : p + 1);
	if (handle == NULL)
	{
		fprintf(stderr, "Device open error. code=%d\n", errno);
		errExit(EXITCODE_DEVICE_OPEN_ERROR, NULL);
	}

	if (strcmp(arg_operation, "read") == 0 || strcmp(arg_operation, "listen") == 0)
	{
		for (int i = 3; i < argc; i++)
		{
			if (strncmp("--format", argv[i], 8) == 0)
			{
				p = strchr(argv[i], '=');
				if (p != NULL)
				{
					if (strcmp("json", p + 1) == 0) option_format = OPTION_FORMAT_JSON;
					if (strcmp("raw", p + 1) == 0) option_format = OPTION_FORMAT_RAW;
					if (strcmp("csv", p + 1) == 0) option_format = OPTION_FORMAT_CSV; // reserved.
					if (strcmp("tsv", p + 1) == 0) option_format = OPTION_FORMAT_TSV; // reserved.
				}
			}
			if (strncmp("--loop", argv[i], 6) == 0)
			{
				option_loop = TRUE;
				p = strchr(argv[i], '=');
				if (p != NULL) option_interval = atoi(p + 1);
				if (option_interval <= 0) option_interval = OPTION_DEFAULT_INTERVAL;
			}
		}

		print_report();

		if (option_loop == TRUE) TdTimer_Start(TimerCallback, option_interval);
	}
	else if (strcmp(arg_operation, "write") == 0)
	{
		char option_string[256];
		memset(option_string, 0, 256);

		for (int i = 3; i < argc; i++)
		{
			if (strncmp("--data", argv[i], 6) == 0)
			{
				p = strchr(argv[i], '=');
				if (p != NULL) strcpy(option_string, p + 1);
			}
			if (strncmp("--loop", argv[i], 6) == 0)
			{
				option_loop = TRUE;
			}
		}

		do
		{
			if (strlen(option_string) == 0)
			{
				fgets(option_string, 255, stdin);
				if ((p = strchr(option_string, '\n')) != NULL) *p = '\0';
			}

			if (write_report(option_string) != 0) break;

			option_string[0] = '\0';

		} while (option_loop == TRUE);

		errExit(EXITCODE_NO_ERROR, NULL);
	}
	else if (strcmp(arg_operation, "erase") == 0)
	{
		if ((dt->capability1 & CPBLTY1_DFU_MASK) == CPBLTY1_DFU_AFTER_ERASE)
		{
			printf("WARNING: The device will not be available until new firmware is written. Continue? [y/N]");
			char c = fgetc(stdin);

			if (c == 'y')
			{
				memset(buffer, 0, dt->report_size + 1);
				buffer[1] = 0x66; buffer[2] = 0x31; buffer[2] = 0x1C; buffer[3] = 0x66; // Erase command & magics

				if(TdHidSetReport(handle, buffer, dt->report_size + 1, USB_HID_REPORT_TYPE_FEATURE))
				{
					errExit(EXITCODE_DEVICE_IO_ERROR, "USB I/O Error.");
				}
			}
			else
			{
				printf("abort.\n");
			}
		}
		else
		{
			fprintf(stderr, "Erase operation is not supported on this device.\n");
		}
	}
	else if (strcmp(arg_operation, "dfu") == 0)
	{
		if ((dt->capability1 & CPBLTY1_DFU_MASK) == CPBLTY1_DFU_AFTER_SWITCH)
		{
			fprintf(stderr, "Switching to DFU-mode is not supported yet.\n");
		}
		else
		{
			fprintf(stderr, "Switching to DFU-mode is not supported on this device.\n");
		}
	}
	else if (strcmp(arg_operation, "init") == 0)
	{
		// 0x82 INIT Command for old-device. need report size == 16.
		if (dt->capability1 & CPBLTY1_CHANGE_SERIAL) 
		{
			time_t epoc;

			memset(buffer, 0, dt->report_size + 1);
			buffer[1] = 0x82;
			time(&epoc); sprintf((char *)&buffer[2], "%llu", epoc);

			if (TdHidSetReport(handle, buffer, dt->report_size + 1, USB_HID_REPORT_TYPE_FEATURE))
			{
				errExit(EXITCODE_DEVICE_IO_ERROR, "USB I/O Error.");
			}

			printf("Set serial number to %s\n", &buffer[2]);
		}
		else
		{
			fprintf(stderr, "Changing serial is not supported on this device.\n");
		}
	}
	else
	{
		fprintf(stderr, "Unknown operation: %s\n", arg_operation);
		errExit(EXITCODE_UNKNOWN_OPERATION, NULL);
	}

	errExit(EXITCODE_NO_ERROR, NULL);
}