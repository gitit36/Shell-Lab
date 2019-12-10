/*
 * tsh - A tiny shell program with job control
 *
 * sl5583 + aal544
 * Sangjin Lee and Andrii Lunin
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
  if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
      app_error("fgets error");
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

   char *argv[MAXARGS];
   char buffer[MAXLINE];

   pid_t id_of_process;
   sigset_t mask;

   strcpy(buffer, cmdline);

   int bg = parseline(buffer, argv);

   if (!argv[0])
      return;                             // return if no arguments are passed

   /*Lets block SIGCHILD*/

   if (!builtin_cmd(argv))
   {

      int emptyset = sigemptyset(&mask);
      if (emptyset != 0) { fprintf(stdout, "sigemptyset error\n"); exit(1);} // app errors checks here

      int addset = sigaddset(&mask, SIGCHLD);
      if (addset != 0) {fprintf(stdout, "sigaddset error\n");exit(1);}

      int procmask = sigprocmask(SIG_BLOCK, &mask, NULL);
      if (procmask != 0) { fprintf(stdout, "sigprocmask error\n"); exit(1); }

      id_of_process = fork();

      // check for errors when forking
      if (id_of_process == -1)
      {fprintf(stdout, "fork error: %s\n", strerror(errno)); exit(1); } // UNIX errors while forking are checked here

      // if its a child, deal with the setpgid problem

      if (!id_of_process)
      {

         if (setpgid(0, 0) == -1)
         { // returns -1 in case of a fail, and sets errno to the indicator of the error
            fprintf(stdout, "setpgid error: %s\n", strerror(errno));
            exit(1);
         }

         if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1)
         { // SIGCHLD signal unblocked

            fprintf(stdout, "sigprocmask error\n");
            exit(1);
         }

         if (execve(argv[0], argv, environ) == -1)
         {
            printf("%s: Command not found\n", argv[0]);
            exit(0); // exit(0) to successfully terminate on a non-existing command
         }
      }
      // Parent
      if (bg == 0)
      { // Foreground
         addjob(jobs, id_of_process, FG, cmdline);
         if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {fprintf(stdout, "sigprocmask error\n"); exit(1); }
         waitfg(id_of_process);
      }
      else
      {// Background
         addjob(jobs, id_of_process, BG, cmdline);
         if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {fprintf(stdout, "sigprocmask error\n"); exit(1); }
         printf("[%d] (%d) %s", pid2jid(id_of_process), (int)id_of_process, cmdline);
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
  if (strcmp(argv[0],"quit")==0){     //if the 1st argument is 'quit' then exit
    exit(0);
  }
  else if (strcmp(argv[0],"fg")==0){    //if the 1st argument is 'fg'
    do_bgfg(argv);
    return 1;
  }
  else if (strcmp(argv[0],"bg")==0){    //if the 1st argument is 'bg'
    do_bgfg(argv);
    return 1;
  }
  else if (strcmp(argv[0],"jobs")==0){    //if the 1st argument is 'jobs'
    listjobs(jobs);
    return 1;
  }
    // else if (!strcmp("&", argv[0])){    //if the 1st argument is '&'
    //     return 1;
    // }
    return 0;
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    char *x = argv[1];

    // if ID does not exist
    if(!x){
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }

    //checking whether argv[1][0] is valid or not
    if(!isdigit(x[0])){
      if(x[0] != '%'){
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
      }
    }

    struct job_t *requested_job;

    int a = (x[0] == '%' ? 1 : 0); //determines if it is PID or JID

    switch(a){
        case 0: // if PID
            requested_job = getjobpid(jobs,atoi(x)); //setting pointer to the job
            if(requested_job == NULL){ //if job is not present
                printf("(%d): No such process\n", atoi(argv[1])); 
                return;
            }
        default: //if JID
            requested_job = getjobjid(jobs,(pid_t)atoi(&x[1]));
            if(requested_job == NULL){
                printf("%s: No such job\n", argv[1]);
                return;
            }
    }

    int currentlybg = strcmp(argv[0], "bg"); //checks if the first argument is background

    if (currentlybg != 0){           //if BG
        requested_job -> state = FG; //change state to foreground
        if (kill(-requested_job->pid, SIGCONT)) { // send SIGCONT to the list of jobs
            fprintf(stdout, "%s: %s\n", "kill error", strerror(errno));
            exit(EXIT_FAILURE);
        }
        waitfg(requested_job -> pid); //wait until foreground job finishes
    }

    else {                           //if FG
        requested_job -> state = BG; //change state to background
        printf("[%d] (%d) %s", requested_job ->jid, requested_job->pid, requested_job->cmdline);
        if (kill(-requested_job->pid, SIGCONT)) { // send SIGCONT to the list of jobs
            fprintf(stdout, "%s: %s\n", "kill error", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    struct job_t *j = getjobpid(jobs,pid);

    //Checking validity for PID
    if(!pid) return;

    //if job is FG, use loop on sleep()
    if(j->state == FG){
        while(pid == fgpid(jobs)) sleep(0);
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
    int st;
    int id_of_job;

    pid_t id_of_process  = waitpid(-1, &st, WNOHANG | WUNTRACED);

    while (id_of_process > 0) {

       id_of_job = pid2jid(id_of_process); // reap the child
        // check state
        if (WIFEXITED(st)) {
            deletejob(jobs, id_of_process);
        }
        else if (WIFSTOPPED(st)) {
            getjobpid(jobs, id_of_process)->state = ST;
            printf("Job [%d] (%d) stopped by signal %d\n", id_of_job, (int) id_of_process, WSTOPSIG(st));

        }
        else if (WIFSIGNALED(st)) {
            deletejob(jobs,id_of_process);
            printf("Job [%d] (%d) terminated by signal %d\n", id_of_job, (int) id_of_process, WTERMSIG(st));}

        id_of_process = waitpid(-1, &st, WNOHANG | WUNTRACED);
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
    pid_t pid = fgpid(jobs);

    int a = (pid != 0 ? 0 : 1);
    // Send SIGINT to all the processes in the foreground
    switch(a){
        case 0:
            kill(-pid,sig); //using -pid instead of pid to the kill function
        default:
            break;
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
    pid_t pid = fgpid(jobs);

    int a = (pid != 0 ? 0 : 1);
    // Send SIGINT to all the processes in the foreground
    switch(a){
        case 0:
            kill(-pid,sig); //using -pid instead of pid to the kill function
        default:
            break;
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

    for (i = 0; i < MAXJOBS; i++)
  if (jobs[i].jid > max)
      max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
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
        if(verbose==1){
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
