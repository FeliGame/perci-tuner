#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int fpet_enable(int fe);
int fpet_disable();

double dotproduct(double x[], double y[], size_t n) {
  double prod = 0.0;
#pragma precision region
#pragma precision range(float, double)
  {
    for (size_t i = 0; i < n; i++) {
      float xi = (float)x[i];
      float yi = (float)y[i];
      float pro = xi * yi;
      prod += (double)pro;
    }
  }
  return prod;
}

static inline uint64_t f64ToU64(double d) {
  union {
    uint64_t i;
    double f;
  } word;
  word.f = d;
  return word.i;
}
uint64_t rand64() {
  uint64_t r = 0;
  r |= ((uint64_t)rand() & 0xFFFFULL) << 0;
  r |= ((uint64_t)rand() & 0xFFFFULL) << 16;
  r |= ((uint64_t)rand() & 0xFFFFULL) << 32;
  r |= ((uint64_t)rand() & 0xFFFFULL) << 48;
  return r;
}
#define NUM_NORMAL_MINf (1.17549435e-38f)
#define NUM_NORMAL_MAXf (3.40282347e+38f)
#define NUM_NORMAL_MIN_FP16 6.103515625e-6
#define NUM_NORMAL_MAX_FP16 65504
uint64_t rand64Range(uint64_t min, uint64_t max) {
  if (min > max) {
    uint64_t t = min;
    min = max;
    max = t;
  }
  uint64_t range = max - min + 1U;
  return min + (rand64() % range);
}

void filldata(double *buf, size_t num) {
  uint64_t min = f64ToU64(NUM_NORMAL_MINf);
  uint64_t max = f64ToU64(NUM_NORMAL_MAXf);
  uint64_t *ptr = (uint64_t *)buf;
  for (size_t i = 0; i < num; i++) {
    ptr[i] = rand64Range(min, max);
  }
}

int main(int argc, char **argv) {
  size_t num = 100;
  if (argc > 1) {
    num = atoi(argv[1]);
  }
  double *x = (double *)malloc(sizeof(double) * num);
  double *y = (double *)malloc(sizeof(double) * num);
  filldata(x, num);
  filldata(y, num);
//   fprintf(stdout, "num : %lu\n", num);
//   fflush(stdout);
  double prod = dotproduct(x, y, num);
  fprintf(stdout, "%le\n", prod);
  free(x);
  free(y);
  return 0;
}


#define _GNU_SOURCE
#define __USE_GNU

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <execinfo.h>
#include <libgen.h>
#include <ucontext.h>
#include <errno.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <stdlib.h>
#include <unwind.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <fenv.h>
#include <unistd.h>

extern char *program_invocation_name;
extern char *program_invocation_short_name;
#define SIZE 100
#define EXE_MAPS_SIZE 100
static char me[SIZE];
static char me_basename[SIZE];
static void *buffer[SIZE];
typedef struct maps_type {
  void *start_addr;
  void *end_addr;
  char file_name[SIZE];
} MAPS;
static MAPS exec_maps[EXE_MAPS_SIZE];
static int curr_maps = 0;

struct trace_arg
{
  void **array;
  _Unwind_Word cfa;
  int cnt;
  int size;
};

#ifdef SHARED
static _Unwind_Reason_Code (*unwind_backtrace) (_Unwind_Trace_Fn, void *);
static _Unwind_Ptr (*unwind_getip) (struct _Unwind_Context *);
static _Unwind_Word (*unwind_getcfa) (struct _Unwind_Context *);
static void *libgcc_handle;


/* Dummy version in case libgcc_s does not contain the real code.  */
static _Unwind_Word
dummy_getcfa (struct _Unwind_Context *ctx __attribute__ ((unused)))
{
  return 0;
}


static void
init (void)
{
  libgcc_handle = __libc_dlopen ("libgcc_s.so.1");

  if (libgcc_handle == NULL)
    return;

  unwind_backtrace = __libc_dlsym (libgcc_handle, "_Unwind_Backtrace");
  unwind_getip = __libc_dlsym (libgcc_handle, "_Unwind_GetIP");
  if (unwind_getip == NULL)
    unwind_backtrace = NULL;
  unwind_getcfa = (__libc_dlsym (libgcc_handle, "_Unwind_GetCFA")
                   ?: dummy_getcfa);
}
#else
# define unwind_backtrace _Unwind_Backtrace
# define unwind_getip _Unwind_GetIP
# define unwind_getcfa _Unwind_GetCFA
#endif

static _Unwind_Reason_Code
backtrace_helper (struct _Unwind_Context *ctx, void *a)
{
  struct trace_arg *arg = a;

  /* We are first called with address in the __backtrace function.
     Skip it.  */
  if (arg->cnt != -1)
    {
      arg->array[arg->cnt] = (void *) unwind_getip (ctx);

      /* Check whether we make any progress.  */
      _Unwind_Word cfa = unwind_getcfa (ctx);

      if (arg->cnt > 0 && arg->array[arg->cnt - 1] == arg->array[arg->cnt]
          && cfa == arg->cfa)
        return _URC_END_OF_STACK;
      arg->cfa = cfa;
    }
  if (++arg->cnt == arg->size)
    return _URC_END_OF_STACK;
  return _URC_NO_REASON;
}

int backtrace (void **array, int size){
  struct trace_arg arg = { .array = array, .cfa = 0, .size = size, .cnt = -1 };
#ifdef SHARED
  __libc_once_define (static, once);

  __libc_once (once, init);
  if (unwind_backtrace == NULL)
    return 0;
#endif

  if (size >= 1)
    unwind_backtrace (backtrace_helper, &arg);

  /* _Unwind_Backtrace on IA-64 seems to put NULL address above
     _start.  Fix it up here.  */
  if (arg.cnt > 1 && arg.array[arg.cnt - 1] == NULL)
    --arg.cnt;
  return arg.cnt != -1 ? arg.cnt : 0;
}
//weak_alias (__backtrace, backtrace)
//libc_hidden_def (__backtrace)


#ifdef SHARED
/* Free all resources if necessary.  */
libc_freeres_fn (free_mem)
{
  unwind_backtrace = NULL;
  if (libgcc_handle != NULL)
    {
      __libc_dlclose (libgcc_handle);
      libgcc_handle = NULL;
    }
}
#endif

typedef struct _sig_ucontext {
  unsigned long     uc_flags;
  struct ucontext   *uc_link;
  stack_t           uc_stack;
  struct sigcontext uc_mcontext;
  sigset_t          uc_sigmask;
} sig_ucontext_t;


static void init_exec_maps(void)
{
  int pid = getpid();
  char file_name[50];
  FILE *fp;
  ssize_t read;
  char *line = NULL;
  size_t len = 0;
  char *sptr;
  char *start_addr;
  char *end_addr;
  char *exec_basename;
  sprintf(file_name, "/proc/%d/maps", pid);
  fp = fopen(file_name, "r");
  if (fp == NULL)
    exit(EXIT_FAILURE);
  /* Assumed maps file form
   * 00903000-00904000 rw-p 0001d000 08:06 2359991    /lib/ld-2.13.so
   * 00906000-00a89000 r-xp 00000000 08:06 2359993    /lib/libc-2.13.so
   */
  while ((read = getline(&line, &len, fp)) != -1) {
     //printf("%s", line);
     sptr = line;
     while(isspace(*sptr)) sptr++;
     start_addr = sptr;
     while(*sptr !='-') sptr++;
     *sptr = 0;
     end_addr = ++sptr;
     while(isxdigit(*sptr)) sptr++;
     *sptr = 0;
     sptr += 3;
     if (*sptr != 'x')
       continue;
     exec_maps [curr_maps].start_addr = (void *) strtol(start_addr, NULL, 16);
     exec_maps [curr_maps].end_addr = (void *) strtol(end_addr, NULL, 16);
     sptr+=3;
     while(isxdigit(*sptr)) sptr++; /* jump field 3*/
     sptr++;
     while(!isspace(*sptr)) sptr++; /* jump field 4*/
     sptr++;
     while(isxdigit(*sptr)) sptr++; /* jump field 5*/
     while(isspace(*sptr)) sptr++;/* jump to field 6*/
     if ( strlen(sptr) == 0)
       continue;
     strncpy(exec_maps [curr_maps].file_name, sptr, strlen(sptr) - 1);
     exec_maps [curr_maps].file_name[strlen(sptr) - 1] = 0;
     exec_basename = basename(exec_maps [curr_maps].file_name);
     if (!strcmp(exec_basename,me_basename))
       continue;
     if (!strncmp(exec_maps [curr_maps].file_name,"[vdso]", 6))
       continue;
     if (!strncmp(exec_maps [curr_maps].file_name,"[vsyscall]", 12))
       continue;
     if (!strncmp(exec_maps [curr_maps].file_name,"[stack]", 7))
       continue;
     if (!strncmp(exec_maps [curr_maps].file_name,"[heap]", 6))
       continue;
     curr_maps++;
  }

  free(line);
}
static char *getaddr2line (void *virt_addr, void **off_addr)
{
  int i;
  *off_addr = virt_addr;
  for (i = 0; i < curr_maps; i++)
    {
      #if 0
      fprintf(stdout, 
            "Debug : virt_addr : %p, start_addr : %p, end_addr : %p\n", 
            virt_addr, exec_maps[i].start_addr, exec_maps[i].end_addr 
      );
      fflush(stdout);
      #endif
      if (virt_addr >= exec_maps[i].start_addr && virt_addr < exec_maps[i].end_addr)
        {
          *off_addr = (void *) (virt_addr - exec_maps[i].start_addr);
          return exec_maps[i].file_name;
        }
    }
  return me;
}

static void sig_handler(int n, siginfo_t *siginfo, void *ucontext)
{
    int i, num;
    char **calls;
    char cmd[100];
    void *virt_addr;
    void *off_addr;
    char *rfname;
    void *caller_address;
    int errno_old = errno;
#if defined(__aarch64__) // gcc specific 
    ucontext_t *uc = (ucontext_t *)ucontext;
#else
    sig_ucontext_t *uc = (sig_ucontext_t *)ucontext;
#endif
    int level = 0;
    int start_level = 1;
  
#if defined(__i386__) // gcc specific
    caller_address = (void *) uc->uc_mcontext.eip; // EIP: x86 specific
#elif defined(__x86_64__)
    caller_address = (void *) uc->uc_mcontext.rip; // RIP: x86_64 specific
#elif defined(__arm__) // gcc specific
    caller_address = (void *) uc->uc_mcontext.arm_pc; // arm_pc: arm specific
#elif defined(__aarch64__) // gcc specific
    caller_address = (void *) uc->uc_mcontext.pc; // pc: aarch64 specific
#else
  #error Unsupported architecture. // TODO: Add support for other arch.
#endif

    int pid=getpid();
    int tid=syscall(SYS_gettid);

    char feinfo[200];
    if(n == SIGFPE){
       switch(siginfo->si_code){
           case FPE_INTDIV:
               sprintf(feinfo, " : Integer division by zero");
               break;
           case FPE_INTOVF:
               sprintf(feinfo, " : Integer overflow");
               break;
           case FPE_FLTDIV:
               sprintf(feinfo, " : Floating point division by zero");
               break;
           case FPE_FLTOVF:
               sprintf(feinfo, " : Floating point overflow");
               break;
           case FPE_FLTUND:
               sprintf(feinfo, " : Floating point underflow");
               break;
           case FPE_FLTINV:
               sprintf(feinfo, " : Floating point invalid operation");
               break;
           case FPE_FLTRES:
               sprintf(feinfo, " : Floating point inexact result");
               break;
           default:
               sprintf(feinfo, " : Unknown floating point exception");
       }
    }else{
       strcpy(feinfo, "");
    }
    //strsignal ???
    fprintf(stdout, 
       "\nsignal %d (%s), referenced fault address is %p from instruction address %p\n", 
       n, feinfo, siginfo->si_addr, (void *)caller_address
    );

    num = backtrace(buffer, SIZE);
  #if 1
    for (i = 0; i < num; i++){
        if(buffer[i] == caller_address){
            start_level = i;
            break;
        }
    }
    buffer[start_level] = caller_address;
    calls = backtrace_symbols(buffer, num);
    for (i=start_level; i<num; i++){
        int level2=level++;
  	    fprintf(stdout, "[ %02d ] %s\n", level2, calls[i]);
        rfname = getaddr2line (buffer[i], &off_addr);
        //off_addr = (void *)((uint64_t)off_addr & 0xFFFF);
        sprintf(cmd, "addr2line -e %s %p", rfname, off_addr);
        //fprintf(stdout, "DEBUG : buffer[i] : %p, off_addr : %p, calls[i] : %s, CMD : %s\n", buffer[i], off_addr, calls[i], cmd);
        system(cmd);
    }
  #endif
    errno = errno_old;
#if defined(__aarch64__) // gcc specific
    // Jump to next instr to avoid infinite trap
    // The AArch64 has a fixed 32-bits encoding
    uc->uc_mcontext.pc += 4; // pc: aarch64 specific
#else 
  #error Unsupported architecture. // TODO: Add support for other arch.
#endif
}

#if defined(__aarch64__) // gcc specific
static inline 
uint64_t read_fpcr(){
    uint64_t fpcr;
    __asm__ __volatile__(
        "mrs %0, fpcr\n\t"
        : "=r"(fpcr)
    );
    return fpcr;
}
static inline 
void write_fpcr(uint64_t fpcr){
    __asm__ __volatile__(
        "msr fpcr, %0\n\t"
        :
        : "r"(fpcr)
    );
}

static inline 
uint64_t read_fpsr(){
    uint64_t fpsr;
    __asm__ __volatile__(
        "mrs %0, fpsr\n\t"
        : "=r"(fpsr)
    );
    return fpsr;
}
static inline 
void write_fpsr(uint64_t fpsr){
    __asm__ __volatile__(
        "msr fpsr, %0\n\t"
        :
        : "r"(fpsr)
    );
}
#endif //defined(__aarch64__)

static inline 
int fpet_check(){
    int ret = 0;
#if defined(__aarch64__) // gcc specific
    // 1. test fpcr modifiable
    uint64_t old_fpcr = read_fpcr();
    // IXE,UFE,OFE,DZE,IOE on 12,11,10,9,8
    uint64_t mask = 0x1FULL << 8;
    write_fpcr(mask);
    uint64_t new_fpcr = read_fpcr();
    uint64_t fpe_value = new_fpcr & mask;
    if(fpe_value != mask){
        fprintf(stdout, \
            "[FPET WARNING] : The FPCR register of current AArch64 machine is unmodified, "\
            "FP exception trap is unavailable, fpcr : 0x%016lx\n", new_fpcr \
        );
        fflush(stdout);
        ret |= 0x1;
    }
    write_fpcr(old_fpcr);
#endif
    return ret;
}


///////////////////////////////////////////////////////////////
static volatile int fpet_fe = 0x0;
static volatile int fpet_prev_fe  = 0x0;
static struct sigaction prev_act;
///////////////////////////////////////////////////////////////
/**
 * TODO :
 * 1. 异步安全
 * 2. 信号堵塞
 * */
int fpet_enable(int fe){
    fpet_fe = fe;
    fpet_check();
//    fpet_prev_fe = fegetexcept();
    fpet_prev_fe = feenableexcept(fe);

    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction=sig_handler;

    sigaction(SIGFPE , &act, &prev_act);

    strcpy(me, program_invocation_name);
    strcpy(me_basename, program_invocation_short_name);
    init_exec_maps();
    return 0;
}

int fpet_disable(){
    sigaction(SIGFPE , &prev_act, NULL);
    fedisableexcept(fpet_fe);
    return 0;
}

