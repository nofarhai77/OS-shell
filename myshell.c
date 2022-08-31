#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <wait.h>
#include "unistd.h"
#include <fcntl.h>

/*int open(const char *path, int oflags, mode_t mode);*/

/* ----------------------------------- Assignment details -----------------------------------
The goal of this assignment is to gain experience with process management, pipes, signals, and the
relevant system calls.
We will implement a simple shell program, a function that receives a shell command and performs it,
and initialization/finalization code that required.
Those will be prepare, process_arglist and finalize functions. others will be auxilary functions.
We also receive a skeleton shell program (shell.c) that reads lines from the user, parses them into
commands, and invokes the function we write.
------------------------------------------------------------------------------------------*/


/* setting signal handlers using sigaction system call.
# signalDisposition == 0 --> create SIGCHLD handler
                             SIGCHLD indicates a process that started by the curr process
                             and has terminated.
                             we are using SA_NOCLDSTOP flag, so the kernel posts
                             the SIGCHLD signal to its parent only when the child process dies.
                             it allows to “reap" zombie process.
# signalDisposition == 1 --> create SIG_IGN handler
                             SIG_IGN specifies that the signal should be ignored,
                             except for SIGKILL or SIGSTOP signals.
                             this signal is using for making the shell ignore SIGINT.
                             SA_RESTART flag - if dup2 interrupted by signal, it means errno == EINTR.
                             so this flag make it possible to call dup2 again.
# signalDisposition == 2 --> create SIG_DFL handler
                             according to instructions, after prepare and before execvp, 
                             a foreground child processes (regular commands or parts of a pipe)
                             should terminate upon SIGINT.
                             SIG_DFL specifies that the signal should be handled with the
                             the default action for SIGINT signal.
                             SA_RESTART flag - if dup2 interrupted by signal, it means errno == EINTR.
                             so this flag make it possible to call dup2 again. */
void signalHandler(int signalDisposition)
{
    struct sigaction sigactionStruct;

    /* SIGCHLD signal */
    if (signalDisposition == 0)
    {
        /* reset to zeros */
        memset(&sigactionStruct, 0, sizeof(sigactionStruct));
        
        sigactionStruct.sa_sigaction = NULL;
        sigactionStruct.sa_flags = SA_NOCLDSTOP; /* using for "reap" zombies process */

        /* by instruction - if an error occurs in a signal handler in the shell parent process,
        need to print an error msg and terminate the shell process with exit(1) */
        if (sigaction(SIGCHLD, &sigactionStruct, 0) == -1)
        {
            perror("Error; sigaction has failed.");
            exit(1);
        }
    }

    /* SIG_IGN signal */
    if (signalDisposition == 1)
    {
        /* reset to zeros */
        memset(&sigactionStruct, 0, sizeof(sigactionStruct));

        sigactionStruct.sa_sigaction = (void*)SIG_IGN; /* ignore SIG_INT in shell process */
        sigactionStruct.sa_flags = SA_RESTART;

        /* by instruction - if an error occurs in a signal handler in the shell parent process,
        need to print an error msg and terminate the shell process with exit(1) */
        if (sigaction(SIGINT, &sigactionStruct, 0) == -1)
        {
            perror("Error; sigaction has failed.");
            exit(1);
        }
    }

    /* SIG_DFL signal */
    if (signalDisposition == 2)
    {
        sigactionStruct.sa_handler = SIG_DFL; /* using for handeling SIG_INT in foreground child */
        sigactionStruct.sa_flags = SA_RESTART;

        /* by instruction - if an error occurs in a child process (before it calls execvp()),
        print a proper error message and terminate only the child process using exit(1).
        Nothing should change for the parent or other child processes. */
        if (sigaction(SIGINT, &sigactionStruct, 0) == -1)
        {
            perror("Error; sigaction has failed.");
        }
    }
}

/* this function creates signal handlers.
the skeleton calls this function before the first invocation of process_arglist().
returns 0 on success; any other return value indicates an error. */
int prepare()
{
    /* SIGCHLD with SA_NOCLDSTOP flag - to "reap" zombies */
    signalHandler(0);

    /* SIG_IGN - for shell to ignore SIGINT */
    signalHandler(1);

    return 0;
}

/* in case we have "&" command */
int backgroundProcess(char **arglist)
{
    /* creating child process */
    pid_t pid = fork();
    
    if (pid < 0)
    {
        perror("Error; Fork has failed in background process.");
        exit(-1);
    }

    /* background child process */
    else if (pid == 0)
    {
        execvp(arglist[0], arglist);
        perror(arglist[0]); /* if failed so return as msg arglist[0] */
        exit(1);
    }

    return 1;
}

/* in case we have "|" command */
int pipeProcess(char **arglist, int secondPipeProcessIndex)
{
    char **secondPipeProcess = arglist + secondPipeProcessIndex + 1;

    /* init array for pipe */
    int pipeArray[2];

    if (pipe(pipeArray) == -1)
    {
        perror("Error; Pipe has failed.");
        exit(1);
    }

    /* creating first foreground child process */
    pid_t pid1 = fork();

    if (pid1 == -1)
    {
        perror("Error; Fork has failed.");
        exit(1);
    }

    /* first foreground child process */
    else if (pid1 == 0)
    {
        /* creating handler to sigint of disposition SIG_DFL */
        signalHandler(2);

        /* keep until dup2 works.
        if dup2 failed it means errno == EINTR, so try again.
        if dup2 interrupted by signal, it means errno == EINTR, so try again - its possible beacuse 
        SA_RESTART flag was on when signal handler was created. */
        while ((dup2(pipeArray[1], STDOUT_FILENO) == -1) && (errno == EINTR))
        {
        }

        close(pipeArray[1]);
        close(pipeArray[0]);
        
        execvp(arglist[0], arglist);
        perror(arglist[0]); /* if failed so return as msg arglist[0] */
        exit(1);
    }

    /* parent process */
    else
    {
        close(pipeArray[1]);

        /* creating second foreground child */
        pid_t pid2 = fork();
        
        if (pid2 == -1)
        {
            perror("Error; Fork has failed.");
            exit(1);
        }

        /* second foreground child process */
        else if (pid2 == 0)
        {
            /* creating handler to sigint of disposition SIG_DFL */
            signalHandler(2);

            /* keep until dup2 works.
            if dup2 failed it means errno == EINTR, so try again.
            if dup2 interrupted by signal, it means errno == EINTR, so try again - its possible beacuse 
            SA_RESTART flag was on when signal handler was created. */
            while ((dup2(pipeArray[0], STDIN_FILENO) == -1) && (errno == EINTR))
            {
            }

            close(pipeArray[0]);
            execvp(secondPipeProcess[0], secondPipeProcess);

            perror(arglist[0]); /* if failed so return as msg arglist[0] */
            exit(1);
        }

        /* parent process */
        else
        {
            close(pipeArray[0]);

            /* parent waits for first and second child.
            WUNTRACED flag - return if child process stopped. */
            waitpid(pid1, NULL, WUNTRACED); 
            waitpid(pid2, NULL, WUNTRACED);
        }
    }
    return 0;
}

/* in case there is ">" command */
int outputRedirection(char** arglist, char** commandBeforeFile, const char *fileName)
{
    /* file descriptor for the open file */
    int fd;

    /* creating first foreground child process */
    pid_t pid1 = fork();

    if (pid1 == -1)
    {
        perror("Error; Fork has failed.");
        exit(1);
    }

    /* first foreground child process */
    else if (pid1 == 0)
    {
        signalHandler(2); /* creating handler to sigint of disposition SIG_DFL */
        execvp(arglist[0], arglist);
        perror(arglist[0]); /* if failed so return as msg arglist[0] */
        exit(1);
    }

    /* parent process */
    else
    {
        /* parent waits for first child to finish.
        WUNTRACED flag - return if child process stopped. */
        waitpid(pid1, NULL, WUNTRACED);

        /* open the file given in command line.
        O_CREAT flag - If the file is not exist, so creat it.
        O_WRONLY flag - Open for writing only.
        0640 - permissions. */
        fd = open(fileName, O_CREAT | O_WRONLY, 0640);
        if (fd == -1)
        {
            perror("Error; could not open nor create given file.");
            exit(1);
        }       

        /* creating second foreground child */
        pid_t pid2 = fork();
        
        if (pid2 == -1)
        {
            perror("Error; Fork has failed.");
            exit(1);
        }

        /* second foreground child process */
        else if (pid2 == 0)
        {
            signalHandler(2); /* creating handler to sigint of disposition SIG_DFL */

            /* keep until dup2 works.
            if dup2 failed it means errno == EINTR, so try again.
            if dup2 interrupted by signal, it means errno == EINTR, so try again - its possible beacuse 
            SA_RESTART flag was on when signal handler was created. */
            while ((dup2(fd, STDOUT_FILENO) == -1) && (errno == EINTR))
            {
            }

            close(fd);

            execvp(commandBeforeFile[0], commandBeforeFile);
            perror(arglist[0]); /* if failed so return as msg arglist[0] */
            exit(1);
        }

        /* parent process */
        else
        {
            /* parent waits for second child.
            WUNTRACED flag - return if child process stopped. */
            waitpid(pid2, NULL, WUNTRACED);
        }
    }
    return 0;
}

/* no special commands */
int regularProcess(char **arglist)
{
    /* creating child */
    pid_t pid = fork();
    
    if (pid < 0)
    {
        perror("Error; Fork has failed.");
        exit(-1);
    }

    /* foreground child process */
    else if (pid == 0)
    {
        /* creating handler to sigint of disposition SIG_DFL */
        signalHandler(2);

        execvp(arglist[0], arglist);
        perror(arglist[0]); /* if failed so return as msg arglist[0] */

        exit(1);
    }

    /* parent process */
    else
    {
        /* parent waits for child to finish.
        WUNTRACED flag - return if child process stopped. */
        waitpid(pid, NULL, WUNTRACED);
    }
    return 1;
}

int process_arglist(int count, char **arglist)
{
    int containsAmp = -1;
    int containsPipe = -1;
    int pipeIndex = 0;
    int containsFile = -1;
    char** commandBeforeFile;
    const char *fileName;

    /* checking if there is "&" command. can be only last word before null.  */
    containsAmp = strcmp(arglist[count - 1], "&");

    /* checking if there is "|" command. can be at any index. */
    for (pipeIndex = 0; pipeIndex < count; pipeIndex++)
    {
        if (strcmp(arglist[pipeIndex], "|") == 0)
        {
            containsPipe = pipeIndex;
        }
    }

    /* checking if there is ">". the redirection operator and file name are the last two words. */
    if (count > 2)
    {
        containsFile = strcmp(arglist[count - 2], "<");
    }

    /* Executing commands in the background - there is "&" command */
    if (containsAmp == 0)
    {
        arglist[count - 1] = NULL;
        backgroundProcess(arglist);
    }

    /* Single piping - there is "|" command */
    else if (containsPipe != -1)
    {
        arglist[containsPipe] = NULL;
        pipeProcess(arglist, containsPipe);
    }

    /* Output redirecting - there is ">" command */
    if (containsFile == 0)
    {
        commandBeforeFile = arglist + count - 3; /* arglist[count-3] */
        arglist[count-3] = NULL;
        fileName = arglist[count - 1];
        outputRedirection(arglist, commandBeforeFile, fileName);
    }

    /* Executing commands in queue - no special commands */
    else
    {
        regularProcess(arglist);
    }

    return 1;
}

/* the skeleton calls this function before exiting.
returns 0 on success; any other return value indicates an error. */
int finalize()
{
    return 0;
}