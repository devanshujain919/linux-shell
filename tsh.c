/*   
 * tsh - A tiny shell program with job control
 * 
 * 
	Name  : Devanshu Jain
	Email : 201201133@daiict.ac.in
	
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
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */

int flag_waitfg = 1;



/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
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
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if(*cmdline == '\n')
		continue;

	if (feof(stdin)) { /* End of file (ctrl-d) */
//		printf("23232323");
		fflush(stdout);
	    exit(0);
	}
//	printf("\n234");
	/* Evaluate the command line */

	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
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
*/
void eval(char *cmdline) 
{
	flag_waitfg = 1;
	char *argv[MAXARGS]; // to store the parsed data from parseline method
	int bg; // to know whether the job is requested as a bg or fg

	bg = parseline(cmdline, argv);

	if(!builtin_cmd(argv))
	{
		// its not a built in command
//		printf("\nnot builtin..");
		pid_t cpid; // pid of the child
		sigset_t mask;

		sigemptyset(&mask);
		sigaddset(&mask, SIGCHLD);
		sigprocmask(SIG_BLOCK, &mask, NULL);
		 		
		if((cpid = fork()) == 0)
		{
			setpgid(0, 0);
			sigprocmask(SIG_UNBLOCK, &mask, NULL);
			if(execve(argv[0], argv, environ) < 0)
			{
				printf("%s : Command not found\n", argv[0]);
			}
			exit(0) ;
		}
		
//		sigprocmask(SIG_UNBLOCK, &mask, NULL);

		if(!bg)
		{
//			printf("\nprocess is running in foreground..");
//			printf("one\n");
			addjob(jobs, cpid, FG, cmdline); // add the process in the job list as a foreground process

			sigprocmask(SIG_UNBLOCK, &mask, NULL);
//			printf("\nprocess fg is added to job list");
//			false_foregnd_sig = 1;
			waitfg(cpid); // do the wait-ing
//			false_foregnd_sig = 1;
		}
		else
		{
	//		printf("2\n");
			addjob(jobs, cpid, BG, cmdline); // add the process in the job list as a background process

			sigprocmask(SIG_UNBLOCK, &mask, NULL);
			struct job_t *job = getjobpid(jobs, cpid);
			printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
			fflush(stdout);
		}	

	}	
	return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
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
	while (*buf && (*buf == ' ')) /* ignore spaces */
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
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
	if(!strcmp(argv[0], "quit"))
	{
		exit(0);
	}
	else if(!strcmp(argv[0], "fg") || !strcmp(argv[0], "bg"))
	{
		do_bgfg(argv);
		return 1;
	}
	else if(!strcmp(argv[0], "jobs"))
	{
//		printf("\njobs called");
//		fflush(stdout);
		listjobs(jobs);
		fflush(stdout);
		return 1;
	}
	return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
	int option = !strcmp(argv[0], "fg");
	if(argv[1] == NULL)
	{
		if(option)
			printf("fg command requires PID or %%jobid  argument\n");
		else
                        printf("bg command requires PID or %%jobid  argument\n");

		fflush(stdout);
		return ;
	}

	int flag_jid = 0; // assume it to be for pid
        if(argv[1][0] == '%')
        {
        	//its jid
                flag_jid = 1;
        }
	int id = 0, start = 0;
	if(flag_jid)
	{
		start = 1;
	}
	while(argv[1][start] != '\0')
	{
		if(isalpha(argv[1][start]))
                {
			if(option)
			{
			        printf("fg : argument must be a PID or %%jobid\n");
			}
			else
			{
	                        printf("bg : argument must be a PID or %%jobid\n");
			}
			fflush(stdout);
			return ;
		}

		id = (id * 10) + (argv[1][start] - '0');
		start ++;
	}

        struct job_t *job;
	if(flag_jid)
	{
		job = getjobjid(jobs, id);
	        if(job == NULL)
                {
        	        printf("%s: No such job\n", argv[1]);
                        fflush(stdout);
                	return ;
                }
	}
	else
	{
		job = getjobpid(jobs, id);
		if(job == NULL)
                {
                        printf("(%s): No such process\n", argv[1]);
                        fflush(stdout);
                        return ;
                }

	}
//	printf("%d", id);
	if(!strcmp(argv[0], "fg"))
	{
		// change the stopped or running process in bg to running fg
//		if(job->state == ST)
//		{
			// job is stopped
			// objective : 1. run it
			//	       2. make it foreground	 
		//	kill(job->pid, SIGCONT);
	//		printf("done.. boss\n");
	//		fflush(stdout);
			job->state = FG;
			flag_waitfg = 1;
	//		printf("pid : %d", job->pid);
                        kill(-1*getpgid(job->pid), SIGCONT);
			waitfg(job->pid);
//		}	
/*		else if(job->state == BG)
		{
			// job is in background
			// objective : 1. make it foreground
		}
*/	}
	else
	{
		// change the stopped bg process in running bg
		job->state = BG;
		kill(-1*getpgid(job->pid), SIGCONT);
		printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
		fflush(stdout);
	}
	return;

}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */

void waitfg(pid_t pid)
{
//	int status;
//	pause();
//	printf("hello1234567890");
//	pid_t pid2;
	while(flag_waitfg)
		pause();
/*	while((pid2 = waitpid(pid, &status, WUNTRACED)) > 0)
	{
		printf("hello from waitfg");
		fflush(stdout);
		if(!WIFSTOPPED(status))	
		{
			deletejob(jobs, pid2);
		}
	}
*/
//	fflush(stdout);
//	printf(" ");
//	fflush(stdout);
//	printf("ahgkjbskjdg\t\n");
//	fflush(stdout);
//	struct job_t *job = getjobpid(jobs, pid);
	
//	if(job != NULL && job->state != ST)
//		deletejob(jobs, pid);
/*
	while(false_foregnd_sig)
	{
		pause();
	}
*/
//	printf("\nbye!");
	
	return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
//	printf("\nsigchld handler\n");
//	fflush(stdout);
//	write(1, "hello1", 7);
	pid_t pid;
	int status;
	while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0)
	{
		flag_waitfg = 0;
//		printf("pid : %d", pid);
//		struct job_t *job = getjobpid(jobs, pid);
//	write(1, "hello1", 7);
//		deletejob(jobs, pid);
//		printf("\nReaped child sigchld handler : %d\n", pid);
		fflush(stdout);
	        if(WIFEXITED(status))
	        {
//	                printf("\nChild pid : %d terminted normally with status %d\n", pid, WEXITSTATUS(status));
			deletejob(jobs, pid);
        	}
	        else if(WIFSIGNALED(status))
	        {
//        	        printf("Job [%d] (%d) terminated by signal %d", job->jid, job->pid, WTERMSIG(status));

	printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
	fflush(stdout);
			deletejob(jobs, pid);
	        }
	        else if(WIFSTOPPED(status))
	        {

        printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
	fflush(stdout);
	struct job_t *job = getjobpid(jobs, pid);
	job->state = ST;
//	                printf("\nChild pid : %d is stopped by receiving the signal no. %d\n", WSTOPSIG(status));
	        }
		else 
		{
//			printf("\nChild has been reaped...\n");
		}
//		fflush(stdout);
		//fflush(stdout); 
	}
//	fflush(stdout);
//	printf("\nexit\n");
	
	return ;
//	printf("\nExiting...\n");
//	fflush(stdout);
//	return;
//	printf("gds\n");
}


/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
//	printf("\nsigint handler\n");
//	fflush(stdout);
//	false_foregnd_sig = 0;
//	write(1, "hello1", 7);
	pid_t pid_fg = fgpid(jobs);
	if(pid_fg <= 0)
		return ;
//	printf("\nSending kill to %d\n", pid_fg);
	if(kill(-1*getpgid(pid_fg), SIGINT) < 0)
	{
		printf("kill error");
		fflush(stdout);
		return ;
	}
//	printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid_fg), pid_fg, sig);
	return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
//	printf("\nsigstp handler\n");
//	fflush(stdout);

//	printf("11111111111111");
        pid_t pid_fg = fgpid(jobs);
        if(pid_fg <= 0)
                return ;
  //      printf("\nSending kill to %d\n", pid_fg);
        if(kill(-1*getpgid(pid_fg), SIGTSTP) < 0)
        {
                printf("kill error");
                fflush(stdout);
                return ;
        }
	struct job_t *job = getjobpid(jobs, pid_fg);
	job->state = ST;
	fflush(stdout);
//        printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid_fg), pid_fg, sig);
//	printf("one");//fflush(stdout);
//	printf("one\n");
	fflush(stdout);
//	fflush(stdout);
//	printf(" ");
	return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
//	printf("adding");
//	fflush(stdout);
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

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{

//printf("\ndelete called....\n");
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

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
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

/* pid2jid - Map process ID to job ID */
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

/* listjobs - Print the job list */
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
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
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
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



