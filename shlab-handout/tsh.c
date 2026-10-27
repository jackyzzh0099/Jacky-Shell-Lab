/* 
 * tsh - A tiny shell program with job control
 * 
 * < Jacky_Zhang >
 * tsh —— 一个带有作业控制功能的简易 Shell 程序；作者：Jacky_Zhang
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
/* 杂项常量定义 */
#define MAXLINE    1024   /* max line size 最大行长*/
#define MAXARGS     128   /* max args on a command line 一条命令中最大的参数数量*/
#define MAXJOBS      16   /* max jobs at any point in time 任意时刻允许的最大作业数量*/
#define MAXJID    1<<16   /* max job ID 最大作业 ID*/

/* Job states */
/* 作业状态 */
#define UNDEF 0 /* undefined 未定义*/
#define FG 1    /* running in foreground 前台运行*/
#define BG 2    /* running in background 后台运行*/
#define ST 3    /* stopped 已停止*/

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

 /*
 * 作业状态：FG（前台）、BG（后台）、ST（已停止）
 *
 * 作业状态转换及触发这些转换的操作：
 *     FG -> ST  ：按下 Ctrl+Z
 *     ST -> FG  ：执行 fg 命令
 *     ST -> BG  ：执行 bg 命令
 *     BG -> FG  ：执行 fg 命令
 *
 * 同一时刻最多只能有一个作业处于 FG（前台）状态。
 */

/* Global variables */
/* 全局变量 */
extern char **environ;      /* defined in libc 在 C 标准库中定义*/
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) 命令行提示符*/
int verbose = 0;            /* if true, print additional output 如果为真，则打印额外的输出信息*/
int nextjid = 1;            /* next job ID to allocate 下一个要分配的作业 ID*/
char sbuf[MAXLINE];         /* for composing sprintf messages 用于组合 sprintf 格式化输出的消息 */

struct job_t {              /* The job struct  作业结构体*/
    pid_t pid;              /* job PID  作业的进程 ID（PID）*/
    int jid;                /* job ID [1, 2, ...]  作业 ID：[1, 2, ...]*/
    int state;              /* UNDEF, BG, FG, or ST  状态：UNDEF、BG、FG 或 ST*/
    char cmdline[MAXLINE];  /* command line  命令行内容*/
};
struct job_t jobs[MAXJOBS]; /* The job list  作业列表*/
/* End global variables  全局变量定义结束*/


/* Function prototypes  函数原型声明*/

/* Here are the functions that you will implement  以下是需要你实现的函数*/
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you  以下是已经为你提供的辅助函数*/
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine
 * main —— Shell 的主程序
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default)  是否显示提示符（默认显示）*/

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout)
 * 将标准错误输出重定向到标准输出，这样驱动程序就能从连接到标准输出的管道中获取所有输出内容。
 */
    dup2(1, 2);

    /* Parse the command line  解析命令行参数*/
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message  打印帮助信息*/
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info  输出额外的诊断信息*/
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt  不打印命令提示符*/
            emit_prompt = 0;  /* handy for automatic testing  便于进行自动化测试*/
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers  安装信号处理程序*/

    /* These are the ones you will need to implement  以下是需要你实现的信号处理程序*/
    Signal(SIGINT,  sigint_handler);   /* ctrl-c  Ctrl+C*/
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z  Ctrl+Z*/
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child  子进程终止或停止*/

    /* This one provides a clean way to kill the shell  这个处理程序提供了一种干净地终止 Shell 的方式*/
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list  初始化作业列表*/
    initjobs(jobs);

    /* Execute the shell's read/eval loop  执行 Shell 的读取/求值循环*/
    while (1) {

	/* Read command line  读取命令行*/
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d)  文件结束（Ctrl+D）*/
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line  对命令行进行求值*/
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here  控制流永远不会执行到这里*/
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 * eval —— 对用户刚刚输入的命令行进行求值。如果用户请求执行内置命令
 *（quit、jobs、bg 或 fg），则立即执行。否则，创建子进程并在子进程上下文
 *中运行该作业。如果作业在前台运行，则等待它终止后返回。注意：每个子进程都
 *必须拥有唯一的进程组 ID，以避免后台子进程在键盘按下 Ctrl+C（或 Ctrl+Z）
 *时接收到内核发送的 SIGINT（或 SIGTSTP）。
 */
void eval(char *cmdline)
{
    char *argv[MAXARGS];        // 创建参数数组，一共 [MAXARGS] 个
    int bg;                     // 前后台状态指示
    pid_t pid;                      // 进程 ID
    sigset_t mask_all, prev;

    bg = parseline(cmdline, argv);      // 解析命令行

    if (argv[0] == NULL)
        return;                     // 忽略空命令，直接进行下一次循环

    if (builtin_cmd(argv))
        return;                     // 调用 builtin_cmd() ，若是内置命令直接由 Shell 自己
                                    // 处理，并返回 main() 进行下一次循环，不需要 fork 子进程

    /* 重点！！！暂时屏蔽信号，避免作业列表出现竞争条件 */
    sigfillset(&mask_all);  /* 全频段阻塞干扰（笑） */
    sigprocmask(SIG_BLOCK, &mask_all, &prev);

    /* 创建子进程 */
    pid = fork();

    if (pid < 0) {
        unix_error("fork error");
    }

    if (pid == 0) {
        /* 子进程 */

        /* 恢复原来的信号屏蔽状态 */
        sigprocmask(SIG_SETMASK, &prev, NULL);

        /* 创建独立的进程组 */
        setpgid(0, 0);

        /* 执行外部程序 */
        execve(argv[0], argv, environ);

        /* execve() 返回说明执行失败 */
        printf("%s: Command not found\n", argv[0]);
        exit(0);
    }

    /* 父进程：先将子进程登记到作业列表 */
    addjob(jobs, pid, bg ? BG : FG, cmdline);

    /* 恢复原来的信号屏蔽状态 */
    sigprocmask(SIG_SETMASK, &prev, NULL);

    /* 根据前台或后台模式进行处理 */
    if (!bg) {
        waitfg(pid);
    } else {
        printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
    }
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 * parseline —— 解析命令行并构建 argv 数组。单引号括起来的字符会被视为一个完
 * 整参数。用户请求后台作业时返回真，请求前台作业时返回假。
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line  保存命令行的局部副本*/
    char *buf = array;          /* ptr that traverses command linef  遍历命令行的指针*/
    char *delim;                /* points to first space delimiter  指向第一个空格分隔符*/
    int argc;                   /* number of args  参数数量*/
    int bg;                     /* background job?  是否为后台作业？*/

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space  将末尾的 '\n' 替换为空格*/
    while (*buf && (*buf == ' ')) /* ignore leading spaces  忽略开头的空格*/
	buf++;

    /* Build the argv list  构建 argv 参数列表*/
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces  忽略空格*/
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line  忽略空行*/
	return 1;

    /* should the job run in the background?  作业是否应该在后台运行？*/
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 * builtin_cmd —— 如果用户输入的是内置命令，则立即执行该命令。
 */
int builtin_cmd(char **argv) 
{
    if(strcmp( argv[0] , "quit" ) == 0 ){
        exit(0);
    }
        
    else if(strcmp( argv[0] , "jobs" ) == 0 ){
        listjobs(jobs);
        return 1;
    }
        
    else if((strcmp( argv[0] , "fg" ) == 0 ) || (strcmp( argv[0] , "bg" ) == 0 )){
        do_bgfg( argv );
        return 1;
    }
    else
    return 0;     /* not a builtin command  不是内置命令*/
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 * do_bgfg —— 执行内置的 bg 和 fg 命令。
 */
void do_bgfg(char **argv) 
{
    if((strcmp( argv[0] , "fg" ) == 0 ) && argv[1] == NULL ){
        fputs("fg command requires PID or %jobid argument\n", stderr);
        return;
    }
    else if((strcmp( argv[0] , "bg" ) == 0 ) && argv[1] == NULL ){
        fputs("bg command requires PID or %jobid argument\n", stderr);
        return;
    }               // 处理参数输入不全的问题和提示
    
    int i = 0 ;
    if((strcmp( argv[0] , "fg" ) == 0 )){
        if((argv[1][0] != '%') && ( !isdigit( argv[1][0] ))){
            fputs("fg: argument must be a PID or %jobid\n", stderr);
            return;
        }
        if(argv[1][0] == '%')
            i = 1;
        if(argv[1][i] == '\0') { // 拦截只有 % 的情况
            fputs("fg: argument must be a PID or %jobid\n", stderr);
            return;
        }
        while( argv[1][i] ){
            if( !isdigit(argv[1][i]) ){
                fputs("fg: argument must be a PID or %jobid\n", stderr);
                return;
            }
            i++;
        }
    }
    if((strcmp( argv[0] , "bg" ) == 0 )){
        if((argv[1][0] != '%') && ( !isdigit( argv[1][0] ))){
            fputs("bg: argument must be a PID or %jobid\n", stderr); 
            return;
        }
        if(argv[1][0] == '%')
            i = 1;
        if(argv[1][i] == '\0') { // 拦截只有 % 的情况
            fputs("bg: argument must be a PID or %jobid\n", stderr);
            return;
        }
        while( argv[1][i] ){
            if( !isdigit(argv[1][i]) ){
                fputs("bg: argument must be a PID or %jobid\n", stderr);
                return;
            }
            i++;
        }
    }           // 处理不按格式输入 JID 或 PID 的情况

    if( argv[1][0] != '%' ){               // 当输入的是 PID 时的处理
        long int pid = strtol( argv[1] , NULL , 10 );       // 字符串转 10 进制数
        struct job_t *jobhere = getjobpid( jobs , pid );
        if( jobhere == NULL ){
            printf("(%ld): No such process\n", pid);     // 未找到 PID 的返回
            return;
        }
        else if(strcmp( argv[0], "fg") == 0 ){
            jobhere->state = FG;
            if (kill(-pid, SIGCONT) < 0)
                unix_error("kill (fg) error");
            waitfg(pid);
            return;                         // 变前台
        }
        else if(strcmp( argv[0], "bg") == 0 ){
            jobhere->state = BG;
            if (kill(-pid, SIGCONT) < 0)
                unix_error("kill (bg) error");
            printf("[%d] (%ld) %s", pid2jid(pid), pid, jobhere->cmdline);
            return;                         // 变后台
        }
    }   
    
    if( argv[1][0] == '%' ){               // 当输入的是 JID 时的处理
        long int jid = strtol( argv[1] + 1 , NULL , 10 );       // 字符串转 10 进制数
        struct job_t *jobnow = getjobjid( jobs , jid );
        if( jobnow == NULL ){
            printf("%%%ld: No such job\n", jid);     // 未找到 JID 的返回
            return;
        }
        int pid = jobnow->pid;
        struct job_t *jobhere = getjobpid( jobs , pid );
        if( jobhere == NULL ){
            printf("%%%ld: No such job\n", jid);     // 未找到 ID 的返回
            return;
        }
        else if(strcmp( argv[0], "fg") == 0 ){
            jobhere->state = FG;
            if (kill(-pid, SIGCONT) < 0)
                unix_error("kill (fg) error");
            waitfg(pid);
            return;                         // 变前台
        }
        else if(strcmp( argv[0], "bg") == 0 ){
            jobhere->state = BG;
            if (kill(-pid, SIGCONT) < 0)
                unix_error("kill (bg) error");
            printf("[%d] (%d) %s", pid2jid(pid), pid, jobhere->cmdline);
            return;                         // 变后台
        }
    }
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 * waitfg —— 阻塞等待，直到进程 pid 不再是前台进程。
 */
void waitfg(pid_t pid)
{
    while( fgpid(jobs) == pid ){
        sleep(1);
    }
    return;
}

/*****************
 * Signal handlers
 ****************
 * 信号处理程序
 */

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 * sigchld_handler —— 当子作业终止（变成僵尸进程），或者因收到 SIGSTOP 或
 * SIGTSTP 信号而停止时，内核会向 Shell 发送 SIGCHLD。该处理程序会回收所有当
 * 前可回收的僵尸子进程，但不会等待其他仍在运行的子进程终止。
 */
void sigchld_handler(int sig) 
{
    int olderrno = errno;       // 保存原始 errno
    pid_t pid;
    int status;

    while(( pid = waitpid( -1 , &status , WNOHANG | WUNTRACED )) > 0 ){     // 获取对应 pid 的状态信息
        if((WIFEXITED(status)) > 0 ){      // 使用宏 WIFEXITED 解析状态，判断进程是否正常退出
            deletejob( jobs , pid );        // 若正常退出，则仅需从作业列表中删除作业
        }
        else if(WIFSIGNALED(status)){    // 是否被信号杀死
            printf("Job [%d] (%d) terminated by signal %d\n",       // 若是，打印详细信息
                    pid2jid(pid), pid, WTERMSIG(status));       // 其中用宏 WTERMSIG 解析终止信号编号
            deletejob( jobs , pid );    // 因为进程已经被杀死，故应当回收
        }
        else if(WIFSTOPPED(status)){        // 是否被中断（暂停）
            printf("Job [%d] (%d) stopped by signal %d\n",      // 若是，按格式打印详细信息
                    pid2jid(pid), pid, WSTOPSIG(status));       // 其中用宏 WSTOPSIG 解析终止信号编号
            struct job_t *p = getjobpid( jobs , pid );      // 定义结构体指针，调用 getjobpid()找到对应的jobs
            p->state = ST;              // 结构体指针访问成员语法，修改作业状态
        }
    }

    errno = olderrno;       // 恢复原始 errno
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 * sigint_handler —— 用户在键盘上按下 Ctrl+C 时，内核会向 Shell 发送 SIGINT。
 * 捕获该信号并将其转发给前台作业。
 */
void sigint_handler(int sig) 
{
    int fgp = fgpid(jobs);
    if( fgp == 0 ){
        return;
    }
    
    kill( -fgp, SIGINT );
    
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 * sigtstp_handler —— 当用户按下 Ctrl+Z 时，内核会向 Shell 发送 SIGTSTP。捕获该信号，
 * 并通过向前台作业发送 SIGTSTP 来暂停该作业。
 */
void sigtstp_handler(int sig) 
{
    int fgp = fgpid(jobs);
    if( fgp == 0 ){
        return;
    }
    
    kill( -fgp, SIGTSTP );
    
    return;
}

/*********************
 * End signal handlers
 ********************
 * 信号处理程序结束
 */

/***********************************************
 * Helper routines that manipulate the job list
 *********************************************
 * 用于操作作业列表的辅助函数
 */

/* clearjob - Clear the entries in a job struct  clearjob —— 清空作业结构体中的条目*/
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list  initjobs —— 初始化作业列表*/
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID  maxjid —— 返回已分配的最大作业 ID*/
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list  addjob —— 将一个作业添加到作业列表中*/
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list  deletejob —— 从作业列表中删除 PID 等于 pid 的作业*/
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job  fgpid —— 返回当前前台作业的 PID；如果不存在，则返回 0*/
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list  getjobpid —— 根据 PID 在作业列表中查找作业*/
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list  getjobjid —— 根据 JID（作业 ID）在作业列表中查找作业*/
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID  pid2jid —— 将进程 ID（PID）映射为作业 ID（JID）*/
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list  listjobs —— 打印作业列表*/
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 *****************************
 * 作业列表辅助函数结束
 */


/***********************
 * Other helper routines
 **********************
 * 其他辅助函数
 */

/*
 * usage - print a help message
 * usage —— 打印帮助信息。
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 * unix_error —— Unix 风格的错误处理函数。
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 * app_error —— 应用程序风格的错误处理函数。
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 * Signal —— 对 sigaction 函数的封装。
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled  屏蔽正在处理的同类型信号*/
    action.sa_flags = SA_RESTART; /* restart syscalls if possible  如果可能，在系统调用被信号中断后自动重新启动*/

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 * sigquit_handler —— 驱动程序可以通过向子 Shell 发送 SIGQUIT 信号，以一种干净的方式终止它。
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



