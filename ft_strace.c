#include "./include/strace_data.h"
#include "./include/user.h"
#include <ctype.h>
#include <elf.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

int pid_temp = 0;

static void handler(int sig) {
    (void)sig;
    if (pid_temp > 0) {
        kill(pid_temp, SIGKILL);
        waitpid(pid_temp, NULL, 0);
    }
	fprintf(stderr, "\nstrace: Process %d detached\n", pid_temp);
    _exit(1);
}

static void handler_term(int sig) {
    (void)sig;
    if (pid_temp > 0) {
        kill(pid_temp, SIGKILL);
        waitpid(pid_temp, NULL, 0);
    }
	fprintf(stderr, "= ?\n");
	fprintf(stderr, "+++ killed by SIGKILL +++\n");
    _exit(1);
}

static ssize_t read_tracee(unsigned long long address, void *buf, size_t count)
{
	struct iovec local = {buf, count};
	struct iovec remote = {(void *)(uintptr_t)address, count};
	return process_vm_readv(pid_temp, &local, 1, &remote, 1, 0);
}

static void print_quoted(const unsigned char *buf, size_t size)
{
	fputc('"', stderr);
	for (size_t i = 0; i < size; i++) {
		if (buf[i] == '"' || buf[i] == '\\')
			fprintf(stderr, "\\%c", buf[i]);
		else if (isprint(buf[i]))
			fputc(buf[i], stderr);
		else
			fprintf(stderr, "\\%u", (unsigned)buf[i]);
	}
	fputc('"', stderr);
}

static void print_bytes(unsigned long long address, unsigned long long count)
{
	unsigned char buf[64];
	size_t wanted = count < sizeof(buf) ? (size_t)count : sizeof(buf);
	ssize_t size = wanted ? read_tracee(address, buf, wanted) : 0;
	if (size < 0 || (wanted && !size)) {
		fprintf(stderr, "0x%llx", address);
		return;
	}
	print_quoted(buf, (size_t)size);
	if ((unsigned long long)size < count)
		fputs("...", stderr);
}

static void print_string(unsigned long long address)
{
	unsigned char buf[4096];
	ssize_t size = read_tracee(address, buf, sizeof(buf));
	if (size <= 0) {
		fprintf(stderr, "0x%llx", address);
		return;
	}
	size_t length = strnlen((const char *)buf, (size_t)size);
	print_quoted(buf, length);
	if (length == (size_t)size)
		fputs("...", stderr);
}

static void print_argv(unsigned long long address, int compat)
{
	size_t width = compat ? sizeof(uint32_t) : sizeof(uint64_t);
	for (size_t i = 0; i < 128; i++) {
		uint64_t element = 0;
		if (read_tracee(address + i * width, &element, width) != (ssize_t)width) {
			if (i == 0)
				fprintf(stderr, "0x%llx", address);
			else
				fputs(", ...]", stderr);
			return;
		}
		if (!i)
			fputc('[', stderr);
		if (!element) {
			fputc(']', stderr);
			return;
		}
		if (i)
			fputs(", ", stderr);
		print_string(element);
	}
	fputs(", ...]", stderr);
}

static int read_registers(pid_t child, t_syscall_args *args)
{
	u_user_regs_struct regs;
	struct iovec iov = {&regs, sizeof(regs)};
	if (ptrace(PTRACE_GETREGSET, child, NT_PRSTATUS, &iov) == -1)
		return -1;
	if (iov.iov_len == sizeof(regs.regs64)) {
		args->arg[0] = regs.regs64.rdi;
		args->arg[1] = regs.regs64.rsi;
		args->arg[2] = regs.regs64.rdx;
		args->arg[3] = regs.regs64.r10;
		args->arg[4] = regs.regs64.r8;
		args->arg[5] = regs.regs64.r9;
		args->orig_ax = regs.regs64.orig_rax;
		args->rax = regs.regs64.rax;
		args->compat = 0;
	} else if (iov.iov_len == sizeof(regs.regs32)) {
		args->arg[0] = regs.regs32.ebx;
		args->arg[1] = regs.regs32.ecx;
		args->arg[2] = regs.regs32.edx;
		args->arg[3] = regs.regs32.esi;
		args->arg[4] = regs.regs32.edi;
		args->arg[5] = regs.regs32.ebp;
		args->orig_ax = regs.regs32.orig_eax;
		args->rax = regs.regs32.eax;
		args->compat = 1;
	} else {
		errno = ENOTSUP;
		return -1;
	}
	return 0;
}

static const t_SYS_TABLE *lookup_syscall(const t_syscall_args *args)
{
	size_t count = args->compat ? SYS32_TABLE_SIZE : SYS64_TABLE_SIZE;
	const t_SYS_TABLE *table = args->compat ? get_SYS32_TABLE : get_SYS64_TABLE;
	if (args->orig_ax >= count || !table[args->orig_ax].name)
		return NULL;
	return &table[args->orig_ax];
}

static void print_arg(const t_syscall_args *args, int index, int type, long long result)
{
	unsigned long long value = args->arg[index];
	switch (type) {
	case ARG_INPUT_BYTES:
		print_bytes(value, args->arg[index + 1]);
		break;
	case ARG_OUTPUT_BYTES:
		if (result < 0)
			fprintf(stderr, "0x%llx", value);
		else {
			unsigned long long count = args->arg[index + 1];
			if ((unsigned long long)result < count)
				count = (unsigned long long)result;
			print_bytes(value, count);
		}
		break;
	case ARG_STR:
		print_string(value);
		break;
	case ARG_VSTR:
		print_argv(value, args->compat);
		break;
	case ARG_OFFSET:
	case ARG_LONG:
		fprintf(stderr, "%lld", args->compat ? (long long)(int32_t)value : (long long)value);
		break;
	case ARG_SIZE:
		fprintf(stderr, "%llu", value);
		break;
	case ARG_UINT:
		fprintf(stderr, "%u", (unsigned)value);
		break;
	case ARG_FLAGS:
	case ARG_SIGNAL:
	case ARG_STRUCT_PTR:
	case ARG_PTR:
	case ARG_ULONG:
		fprintf(stderr, "0x%llx", value);
		break;
	default:
		fprintf(stderr, "%d", (int)value);
	}
}

struct pending_call {
	t_syscall_args entry;
	const t_SYS_TABLE *syscall;
	int next_arg;
};

static void print_entry(struct pending_call *call)
{
	call->syscall = lookup_syscall(&call->entry);
	call->next_arg = 0;
	if (!call->syscall) {
		fprintf(stderr, "syscall_%llu(/* unknown */)", call->entry.orig_ax);
		return;
	}
	fprintf(stderr, "%s(", call->syscall->name);
	for (; call->next_arg < call->syscall->argCount; call->next_arg++) {
		int i = call->next_arg;
		if (call->syscall->argType[i] == ARG_OUTPUT_BYTES)
			return;
		if (i)
			fputs(", ", stderr);
		print_arg(&call->entry, i, call->syscall->argType[i], 0);
	}
	fputc(')', stderr);
}

static void finish_args(const struct pending_call *call, long long result)
{
	if (!call->syscall || call->next_arg == call->syscall->argCount)
		return;
	for (int i = call->next_arg; i < call->syscall->argCount; i++) {
		if (i)
			fputs(", ", stderr);
		print_arg(&call->entry, i, call->syscall->argType[i], result);
	}
	fputc(')', stderr);
}

static int wait_child(pid_t child, int *status, int flags)
{
	pid_t waited;
	do {
		waited = waitpid(child, status, flags);
	} while (waited < 0 && errno == EINTR);
	return waited == child ? 0 : -1;
}

static int trace(pid_t child, int *status)
{
	struct pending_call call;
	int in_syscall = 0;
	int deliver = 0;
	int compat = 0;

	for (;;) {
		if (ptrace(PTRACE_SYSCALL, child, NULL, deliver) == -1 ||
		    wait_child(child, status, 0) == -1)
			return -1;
		deliver = 0;
		if (WIFEXITED(*status) || WIFSIGNALED(*status)) {
			if (in_syscall) {
				finish_args(&call, -1);
				fputs(" = ?\n", stderr);
			}
			return 0;
		}
		if (!WIFSTOPPED(*status) || (*status >> 16) != 0)
			continue;
		int sig = WSTOPSIG(*status);
		if (sig != (SIGTRAP | 0x80)) {
			siginfo_t info;
			if (ptrace(PTRACE_GETSIGINFO, child, NULL, &info) == -1)
				return -1;
			fprintf(stderr, "--- %s {si_signo=%s, si_code=%d, si_pid=%d, si_uid=%u} ---\n",
			        sigabbrev_np(sig), sigabbrev_np(info.si_signo), info.si_code,
			        info.si_pid, info.si_uid);
			deliver = sig;
			continue;
		}
		t_syscall_args regs;
		if (read_registers(child, &regs) == -1)
			return -1;
		if (in_syscall) {
			long long result = regs.compat ? (long long)(int32_t)regs.rax : (long long)regs.rax;
			finish_args(&call, result);
			if (result < 0 && result >= -4095)
				fprintf(stderr, " = -1 %s\n", strerror((int)-result));
			else
				fprintf(stderr, " = 0x%llx\n", regs.rax);
		}
		if (compat != regs.compat) {
			fprintf(stderr, "[ Process PID=%d runs in %d bit mode. ]\n", child, regs.compat ? 32 : 64);
			compat = regs.compat;
		}
		if (!in_syscall) {
			call.entry = regs;
			print_entry(&call);
		}
		in_syscall = !in_syscall;
	}
}

int main(int argc, char *argv[]){
	if (argc < 2) {
		fprintf(stderr, "Usage: strace <program>\n");
		return 1;
	}

	struct stat st;
	int executable = 0;
	if (lstat(argv[1], &st) == -1) {
		fprintf(stderr, "stat error\n");
		return 1;
	}
	if (st.st_mode & S_IXUSR) {
		executable = 1;
	} else if (st.st_mode & S_IXGRP) {
		executable = 1;
	} else if (st.st_mode & S_IXOTH) {
		executable = 1;
	}
	if (executable == 0) {
		fprintf(stderr, "strace: is not executable\n");
		return 1;
	}

	pid_t child = fork();
	if (child < 0) {
		perror("fork");
		return 1;
	}
	pid_temp = child;
	if (child == 0) {
		raise(SIGSTOP);
		execvp(argv[1], &argv[1]);
		perror("execvp");
		_exit(1);
	}
	signal(SIGINT, handler);
	signal(SIGTERM, handler_term);
	signal(SIGHUP, handler);
	int status;
	long options = PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL | PTRACE_O_TRACEEXEC;
	if (wait_child(child, &status, WUNTRACED) == -1 || !WIFSTOPPED(status) ||
	    ptrace(PTRACE_SEIZE, child, NULL, options) == -1 ||
	    ptrace(PTRACE_INTERRUPT, child, NULL, NULL) == -1 ||
	    wait_child(child, &status, 0) == -1 || trace(child, &status) == -1) {
		perror("ft_strace");
		kill(child, SIGKILL);
		wait_child(child, &status, 0);
		return 1;
	}
	if (WIFEXITED(status)) {
		fprintf(stderr, "+++ exited with %d +++\n", WEXITSTATUS(status));
		return WEXITSTATUS(status);
	}
	int sig = WTERMSIG(status);
	fprintf(stderr, "+++ killed by SIG%s +++\n", sigabbrev_np(sig));
	signal(sig, SIG_DFL);
	sigset_t unblocked;
	sigemptyset(&unblocked);
	sigaddset(&unblocked, sig);
	sigprocmask(SIG_UNBLOCK, &unblocked, NULL);
	raise(sig);
	return 128 + sig;
}
