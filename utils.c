#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>

#include "utils.h"

/* findProcessByName()
 *
 * Given the name of a process, try to find its PID by searching through /proc
 * and reading /proc/[pid]/exe until we find a process whose name matches the
 * given process.
 *
 * args: name of the process
 * returns: pid of the process (or -1 if not found)
 */
pid_t findProcessByName(char* processName)
{
	if(processName == NULL)
	{
		return -1;
	}

	struct dirent *procDirs;

	DIR *directory = opendir("/proc/");

	if (directory)
	{
		while ((procDirs = readdir(directory)) != NULL)
		{
			if (procDirs->d_type != DT_DIR)
				continue;

			pid_t pid = atoi(procDirs->d_name);

			int exePathLen = 10 + strlen(procDirs->d_name) + 1;
			char* exePath = malloc(exePathLen * sizeof(char));

			if(exePath == NULL)
			{
				continue;
			}

			sprintf(exePath, "/proc/%s/exe", procDirs->d_name);
			exePath[exePathLen-1] = '\0';

			char* exeBuf = malloc(PATH_MAX * sizeof(char));
			if(exeBuf == NULL)
			{
				free(exePath);
				continue;
			}
			ssize_t len = readlink(exePath, exeBuf, PATH_MAX - 1);

			if(len == -1)
			{
				free(exePath);
				free(exeBuf);
				continue;
			}

			exeBuf[len] = '\0';

			char* exeName = NULL;
			char* exeToken = strtok(exeBuf, "/");
			while(exeToken)
			{
				exeName = exeToken;
				exeToken = strtok(NULL, "/");
			}

			if(strcmp(exeName, processName) == 0)
			{
				free(exePath);
				free(exeBuf);
				closedir(directory);
				return pid;
			}

			free(exePath);
			free(exeBuf);
		}

		closedir(directory);
	}

	return -1;
}

/* freespaceaddr()
 *
 * Search the target process' /proc/pid/maps entry and find an executable
 * region of memory that we can use to run code in.
 *
 * args: pid of process to inspect
 * returns: address of an executable region of memory inside that process'
 * address space.
 */
long freespaceaddr(pid_t pid)
{
	FILE *fp;
	char filename[30];
	char line[85];
	long addr;
	char str[20];
	char perms[5];
	sprintf(filename, "/proc/%d/maps", pid);
	fp = fopen(filename, "r");
	if(fp == NULL)
		exit(1);
	while(fgets(line, 850, fp) != NULL)
	{
		sscanf(line, "%lx-%*lx %s %*s %s %*d", &addr, perms, str);

		if(strstr(perms, "x") != NULL)
		{
			break;
		}
	}
	fclose(fp);
	return addr;
}

/* getlibcaddr()
 *
 * Gets the base address of libc.so inside a process by reading /proc/pid/maps.
 *
 * args: pid of process to inspect
 * returns: base address of libc.so inside that process
 */
long getlibcaddr(pid_t pid)
{
	FILE *fp;
	char filename[30];
	char line[850];
	long addr;
	char perms[5];
	char* modulePath;
	sprintf(filename, "/proc/%d/maps", pid);
	fp = fopen(filename, "r");
	if(fp == NULL)
		exit(1);
	while(fgets(line, 850, fp) != NULL)
	{
		sscanf(line, "%lx-%*lx %*s %*s %*s %*d", &addr);
		if(strstr(line, "libc") != NULL)
		{
			break;
		}
	}
	fclose(fp);
	return addr;
}

/* getFunctionAddress()
 *
 * Find the address of a function within our own loaded copy of libc.so.
 *
 * args: name of the function whose address we want to find
 * returns: address of that function
 */
long getFunctionAddress(char* funcName)
{
	void* self = dlopen("libc.so.6", RTLD_NOLOAD);
	void* funcAddr = dlsym(self, funcName);
	return (long)funcAddr;
}

/* findRet()
 *
 * Starting at an address somewhere after the end of a function, search for the
 * "ret" instruction that ends it. We do this by searching for an 0xc3 byte,
 * and assuming that it represents that function's "ret" instruction. This
 * should be a safe assumption. Function addresses are word-aligned, and so
 * there's usually extra space at the end of a function. This space is always
 * padded with "nop"s, so we'll end up just searching through a series of of
 * "nop"s before finding our "ret". In other words, it's unlikely that we'll
 * run into a 0xc3 byte that corresponds to anything other than an actual "ret"
 * instruction.
 *
 * Note that this function only applies to x86 and x86_64, and not ARM.
 *
 * args: ending address of the function to find the "ret" instruction for
 * return: the address of the "ret" instruction of the function
 */
unsigned char* findRet(void* endAddr)
{
	unsigned char* retInstAddr = endAddr;
	while(*retInstAddr != INTEL_RET_INSTRUCTION)
	{
		retInstAddr--;
	}
	return retInstAddr;
}
