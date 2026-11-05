#ifndef USER_H
#define USER_H

struct user_regs_struct64
{
  __extension__ unsigned long long int r15;
  __extension__ unsigned long long int r14;
  __extension__ unsigned long long int r13;
  __extension__ unsigned long long int r12;
  __extension__ unsigned long long int rbp;
  __extension__ unsigned long long int rbx;
  __extension__ unsigned long long int r11;
  __extension__ unsigned long long int r10;
  __extension__ unsigned long long int r9;
  __extension__ unsigned long long int r8;
  __extension__ unsigned long long int rax;
  __extension__ unsigned long long int rcx;
  __extension__ unsigned long long int rdx;
  __extension__ unsigned long long int rsi;
  __extension__ unsigned long long int rdi;
  __extension__ unsigned long long int orig_rax;
  __extension__ unsigned long long int rip;
  __extension__ unsigned long long int cs;
  __extension__ unsigned long long int eflags;
  __extension__ unsigned long long int rsp;
  __extension__ unsigned long long int ss;
  __extension__ unsigned long long int fs_base;
  __extension__ unsigned long long int gs_base;
  __extension__ unsigned long long int ds;
  __extension__ unsigned long long int es;
  __extension__ unsigned long long int fs;
  __extension__ unsigned long long int gs;
};

struct user_regs_struct32
{
  unsigned int ebx;
  unsigned int ecx;
  unsigned int edx;
  unsigned int esi;
  unsigned int edi;
  unsigned int ebp;
  unsigned int eax;
  unsigned int xds;
  unsigned int xes;
  unsigned int xfs;
  unsigned int xgs;
  unsigned int orig_eax;
  unsigned int eip;
  unsigned int xcs;
  unsigned int eflags;
  unsigned int esp;
  unsigned int xss;
};

typedef union{
	struct user_regs_struct64 regs64;
	struct user_regs_struct32 regs32;
} u_user_regs_struct;

typedef struct s_syscall_args{
	unsigned long long arg[6];
	unsigned long long orig_ax;
	unsigned long long rax;
	int compat;
} t_syscall_args;

#endif
