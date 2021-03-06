/* 
 * tsh - A tiny shell program with job control
 *
 * You __MUST__ add your user information here below
 * 
 * === User information ===
 * Group: NONE
 * User 1: hilmarr13
 * Name: Hilmar Ragnarsson
 * SSN: 2310922509
 * User 2: sindrik13
 * Name: Sindri Mar Kaldal Sigurjonsson
 * SSN: 2205922359
 * === End User Information ===
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
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
	    app_error("fgets error");
	}
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

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
    int bg;
    pid_t pid;

    //Defining a set that will contain the signals to be blocked
    sigset_t blockMask;

    //Initalize the signal set blockMask, such that all signals defined
    //in this document are included 
    if(sigfillset(&blockMask) == -1){
	printf("Erorr!");
	return;
    }
    //Make sure to add SIGCHLD to the set
    if(sigaddset(&blockMask, SIGCHLD) == -1){
	printf("Erorr!");
	return;
    }
    
    //create the argument array
    char *argv[MAXARGS];

    //breake down the command line argument into the arrray
    bg = parseline(cmdline, argv);
    //if parseline returns -1, error.
    if(bg == -1){
	printf("Erorr!");
	return;
    }
    //get the job structure
    struct job_t *job;

    //check if the command from the user is a built-in command
    //if it's not, then create a child process to handle the command.
    if(!builtin_cmd(argv)) {

	//block SIGCHLD signals before it forks the child
	if(sigprocmask(SIG_BLOCK, &blockMask, NULL) == -1){
	     printf("Erorr!");
             return;
        }
	
	if((pid = fork()) == 0) { //The child
	   
	    //Set the child process group id
            if(setpgid(0, 0) == -1){
		printf("Erorr!");
		return;
	    }

	    //unblock signals
	    if(sigprocmask(SIG_UNBLOCK, &blockMask, NULL) == -1){
		printf("Erorr!");
           	return;
            }

	     //if the command is not buil tin  we need to break the command down    
            if(execvp(argv[0], argv) == (-1)) {
		//If the command is not found we print error message to the user
		printf("%s: Command not found\n", argv[0]);
		exit(0);
	    }
	}

	//Check if the process is in the foreground or background and add it accordingly
	//if addjob returns 0 then it tried to make to many jobs
	if(addjob(jobs, pid, bg ? BG : FG, cmdline) == 0){
	     printf("Erorr!");
	     return;
	}
	//unblock signals after adding a job to jobs.
	if(sigprocmask(SIG_UNBLOCK, &blockMask, NULL) == -1){
	     printf("Erorr!");
             return;
        }

	//If it's in the foreground we wait until it's no longer
	//a foreground process
	if(!bg){
	     waitfg(pid);
	}
	else{
	     //If it's in the backgound, we print it to the user
	     job = getjobpid(jobs, pid);
	     printf("[%d] (%d) %s", job->jid, job->pid, cmdline); 
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
    while (*buf && (*buf == ' ')) { /* ignore leading spaces */
	buf++;
    }

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
	while (*buf && (*buf == ' ')) { /* ignore spaces */
	    buf++;
	}

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0) { /* ignore blank line */
	return 1;
    }

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
    if(strcmp(argv[0], "quit") == 0) {
	exit(0);
    } else if(strcmp(argv[0], "jobs") == 0) {
	listjobs(jobs);
	return 1;
    } else if((strcmp(argv[0], "bg") == 0) || (strcmp(argv[0], "fg") == 0 )) {
	do_bgfg(argv);
	return 1;
    }
    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    //If there was no arguments we print the error message to the user
    if(argv[1] == NULL) {
	if(strcmp(argv[0], "bg") == 0) {
	   printf("bg command requires PID or %%jobid argument\n");
	}
	else {
	   printf("fg command requires PID or %%jobid argument\n");
	}
	return;
    }

    //Else we make a new array conisting of the arguments
    char *args = argv[1];

    //Declare the pid and the job
    pid_t pid;
    struct job_t *job;

    
    if(strcmp(argv[0], "bg") == 0) { //Background process

 	//If the first item in args is a digit (PID)
	if(isdigit(args[0])) {
           
	   //Convert the PID to int and then fetch the job
	   job = getjobpid(jobs, atoi(argv[1]));
	   
 	   //If the PID didn't match any job we print the error message to the user
           if(job == NULL) {
              printf("(%s): No such process\n", argv[1]);
              return;
           }
	   //If the job exists
	   
	   //retrieve the job pid	
	   pid = job->pid;

	   //Resume by sending the SIGCONT signal to the process through kill
	   kill(-pid, SIGCONT);

	   //Change the job state to BG and print it to the user
	   job->state = BG;
	   printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
	   
	}   
	//If the first item in the argument list is % it's a jobID
	else if(args[0] == '%'){
	   //Retrieve the job for the given job id
	   job = getjobjid(jobs, atoi(&args[1]));

	   //If the job ID didn't match any job we print the error message to the user
	   if(job == NULL) {
              printf("(%c): No such job\n", args[1]);
              return;
           }

	    //retrieve the pid from the job
	   pid = job->pid;

	   //resume the process by sending the SIGCONT signal to the process
	   kill(-pid, SIGCONT);
	   
           //Set the process to the background
	   job->state = BG;

	   //print out the message to the user
	   printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);	
	}
	//If the first argument was neither a digit or % we ask for a proper argument
	else {
	    printf("argument must be PID or %%jobid\n");
	    return;
	}
	
    }
    //FG 
    else {
	//check if the first argument in args is a digit (pid)
	if(isdigit(args[0])){
	  job = getjobpid(jobs, atoi(argv[1]));  
	  if(job == NULL) {
	     printf("(%s): No such process\n", argv[1]);
	     return;
	  }
	  //retrive the pid from the job
	  pid = job->pid;
	 
	  //Resume the process by sending the SIGCONT signal to the process through kill
	  kill(-pid, SIGCONT);

	  //Change the state of the job to FG and wait until it's no longer 
	  //a foreground process
	  job->state = FG;
	  waitfg(pid); 
	}

	//if it's not it's % (job ID)
	else if(args[0] == '%'){
	  //Retrieve the job for the given job id
	  job = getjobjid(jobs, (args[1] - '0'));

	  //If the job ID didn't match any job we print the error message to the user
	  if(job == NULL) {
	     printf("(%c): No such job\n", args[1]);
             return;
	  }
	  //retrieve the pid from the job
	  pid = job->pid;

	  //resume the process by sending the SIGCONT signal to the process
          kill(-pid, SIGCONT);

	  //Bring the process to the foreground
          job->state = FG;
	
	  //Wait while the job is still in the foreground
	  waitfg(pid);
	}

	//If the first argument was neither a digit or % we ask for a proper argument
	else {
	    printf("argument must be PID or %%jobid\n");
	}
    }

    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    //Get the job corresponding to the pid. 
    struct job_t *job = getjobpid(jobs, pid);

    //As long as the job is still a forground job were going to wait.
    while(job->state == FG){	
	sleep(1);
    }	
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
	int status;	//The status of the job
	pid_t pid; 	//the child's pid
	struct job_t *job;

	/*this while loops reaps the child processes one by one. The WNOHANG option makes waitpid return
 	immediatly instead of waiting for the child. The WUNTRACED option requests a status information
	from stopped processes so that the parent does not wait for them*/
	while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0){
	     if(WIFEXITED(status)){	//if the child is terminated
	     	deletejob(jobs, pid);
	     }
	     //If the child is terminated by signal we print the error message and check 
	     //which signal caused the child to terminate with WTERMSIG. Last we delete the job
	     if (WIFSIGNALED(status)) { 
		job = getjobpid(jobs, pid);
		printf("Job [%d] (%d) terminated by signal %d\n", job->jid, job->pid, WTERMSIG(status)); 
       	        deletejob(jobs,pid);
	     }
	     //Check if the child has stopped and then check the number of the signal with WSTOPSIG.
	     //We then print the error message to the user and change the state of the job to ST.
	     else if(WIFSTOPPED(status)){
		job = getjobpid(jobs, pid);
		job->state = ST;
		printf("Job [%d] (%d) stopped by signal %d\n", job->jid, job->pid, WSTOPSIG(status));	
	     } 
	}	
   	return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    //Retrieve the pid of the foreground job
    pid_t pid = fgpid(jobs);

    //kill the foreground job if one exists by sending the signal to the 
    //process through kill. 
    if(pid != 0) {
	kill(-pid, sig);
    }
 
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    //Retrieve the pid of the foreground job
    pid_t pid = fgpid(jobs);

    //if a foreground exists, i stop it by sending the signal to the process
    //through kill.
    if(pid != 0) {
	kill(-pid, sig);
    }
        
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

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].jid > max) {
	    max = jobs[i].jid;
	}
    }
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1) {
	return 0;
    }

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS) {
		nextjid = 1;
	    }
	    strcpy(jobs[i].cmdline, cmdline);
	    if (verbose){
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
    int i;

    if (pid < 1) {
	return 0;
    }

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

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].state == FG) {
	    return jobs[i].pid;
	}
    }
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1) {
	return NULL;
    }
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    return &jobs[i];
	}
    }
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1) {
	return NULL;
    }
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].jid == jid) {
	    return &jobs[i];
	}
    }
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1) {
	return 0;
    }
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
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

    if (sigaction(signum, &action, &old_action) < 0) {
	unix_error("Signal error");
    }
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
