#define _XOPEN_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>

////Global variables and constants
#define MAX_CMD_LENGTH      2048
#define MAX_ARGS            512
#define BG_PID_SIZE         100         //size of bgPid array

char commandPrompt[MAX_CMD_LENGTH];     //string to hold user input commands
char blank[] = "___blank";              //workaround to avoid some crazy seg fault for blank lines and SIGTSTP..
char** commandArgs;                     //pointer to parsed command prompts
int bgPid[BG_PID_SIZE];                 //storage for background processes
char shellPid[10];                      //container for $$ expansion
int argsCount;                          //counter of arguments in user input
int isBackgroundTask = 0;               //fast way to identify and check for background
int exitStatus;                         //exit status captured by shell upon child termination
int isTermBySignal;                     //evaluates whether foreground process terminated by signal, otherwise normal.
int isBackgroundEnabled = 1;            //toggles background processes, default is enabled


////Helper Functions
//declare function prototypes to avoid implicit compiler issues
void UserInput();
void ParseCommandPrompt();
void CommandExit();
void CommandCd();
void CommandStatus();
void ChildExecute(pid_t);
void ManageRedirection();
void ProcessHandler(pid_t);
void CheckBGProcesses();
void CatchStopSigForBackgroundToggle(int);


//takes input from the user, format as string, and update global commandPrompt array
void UserInput() {
    fflush(stdout);
    printf(": "); //prompt for command line
    fflush(stdout);

    char *endP;
    memset(commandPrompt, '\0', MAX_CMD_LENGTH);
    //takes input, checks stdin and exits early if there's issue
    if(fgets(commandPrompt, MAX_CMD_LENGTH, stdin) != NULL) {
        //converts the stdin's newline to null terminator for string
        endP = strstr(commandPrompt, "\n");
        if (endP != NULL)
            *endP = '\0';
    }
    else { //fgets is null when any signals come in during this. Clear prompt, which will be treated as blank
        memset(commandPrompt, '\0', MAX_CMD_LENGTH);
    }

    //checks if it's background task, and record it as such
    isBackgroundTask = 0; //default

    //printf("the char 2nd from end is: '%c', and last is '%c'\n", commandPrompt[strlen(commandPrompt)-2], commandPrompt[strlen(commandPrompt)-1]);

    if (commandPrompt[strlen(commandPrompt)-1] == '&' && commandPrompt[strlen(commandPrompt)-2] == ' ') {
        isBackgroundTask = 1;
        //remove " &" from args now that flag is set
        commandPrompt[strlen(commandPrompt)-1] = '\0';
        commandPrompt[strlen(commandPrompt)-1] = '\0';
    }

    //printf("command prompt is now: '%s'\n", commandPrompt);

    //if not enabled, then after updating the arg string, revert background flag
    if(!isBackgroundEnabled)
        isBackgroundTask = 0;

    //replacing $$ if it happens not to be spaced apart from other things
    endP = strstr(commandPrompt, "$$");
    if (endP != NULL) {
        //get pid
        memset(shellPid, '\0', 10);
        snprintf(shellPid, 10, "%d", (int) getpid());
        int pidLength = (int) strlen(shellPid);

        int indexInsertion = endP - commandPrompt;

        //move enough space for pid insertion
        for (int i = strlen(commandPrompt) + pidLength; i > indexInsertion + pidLength; i--) {
            commandPrompt[i] = commandPrompt[i-pidLength];
        }

        //insert pid
        for (int i = 0; i < pidLength; i++)
            commandPrompt[i+indexInsertion] = shellPid[i];
    }

//    printf("length: %d. content: '%s'\n", strlen(commandPrompt), commandPrompt);
}

//parses command prompt input into array of strings with space as delimiter
void ParseCommandPrompt() {
    char *token;
    argsCount = 0;
    commandArgs = (char**) malloc(MAX_ARGS * sizeof(char *)); //allocates maximum arguments; remember to free!

    //blank line; set first element as __blank to indicate so
    if (strlen(commandPrompt) <= 1) {
        commandArgs[0] = blank;
    }
    else {
        //stores each argument in order to string array
        token = strtok(commandPrompt, " ");
        while (token != NULL && argsCount < MAX_ARGS) {
            //if it's $$, expand into PID instead
            if (strcmp(token, "$$") == 0) {
                memset(shellPid, '\0', 10);
                snprintf(shellPid, 10, "%d", (int) getpid());
                commandArgs[argsCount] = shellPid;
            }
            else
                commandArgs[argsCount] = token;
            argsCount++;

            if (argsCount == MAX_ARGS)
                printf("Warning, # of arguments is at max arguments allowed. Additional arguments ignored.");

            token = strtok(NULL, " ");
        }
    }
}

//built-in command 'exit' - kills all active child processes then exits shell
void CommandExit() {
    for (int i = 0; i < BG_PID_SIZE; i++) {
        if (bgPid[i] > 0)
            kill(bgPid[i], SIGTERM);
    }
}

//built-in command 'cd' - if no argument, cd to HOME, otherwise cd to given directory
void CommandCd() {
    // no argument is provided, cd to default HOME
    if (argsCount == 1)
        chdir(getenv("HOME"));
    // additional argument provided for destination directory
    else {
        //chdir fails (bad directory path) -
        if (chdir(commandArgs[1]) != 0)
            fprintf(stderr, "%s: no such file or directory to enter.\n", commandArgs[1]);
    }
}

//built-in command 'status' - displays the exit status or terminating signal of last FG process
void CommandStatus() {
    if (!isTermBySignal) {
        printf("process finished with exit status: %d\n", exitStatus);
        fflush(stdout);
    }
    else {
        printf("process terminated by signal: %d\n", exitStatus);
        fflush(stdout);
    }
}

//function to regulate execution of non-built in commands with exec()
void ChildExecute(pid_t spawnpid) {
    //all children processes ignore SIGTSTP signal
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = SIG_IGN;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    //reset SIGINT to terminate foreground process
    if (isBackgroundTask == 0) {
        struct sigaction SIGINT_action = {0};
        SIGINT_action.sa_handler = SIG_DFL;
        sigaction(SIGINT, &SIGINT_action, NULL);
    }

    //process any redirection and modify commandArgs to remove shell operators
    ManageRedirection();

    execvp(commandArgs[0], commandArgs); //execute the command!!

    //catch for if exec() fails due to invalid command
    fprintf(stderr, "command failed: command '%s' is invalid\n", commandArgs[0]);
    exit(EXIT_FAILURE);
}

//constructs an improved command argument array that processes redirection (<, >)
void ManageRedirection() {
    int fileDescriptor; //file descriptors

    //iterate through the original command arguments, look for redirection to process
    for (int i = 0; i < argsCount; i++) {
        //if it's a background task, by default set i/o to /dev/null
        if (isBackgroundTask == 1) {
            //stdin
            fileDescriptor = open("/dev/null", O_RDONLY);
            if (fileDescriptor == -1) {
                fprintf(stderr, "command failed: cannot open stdin '/dev/null' for background task.");
                exit(EXIT_FAILURE);
            }
            //redirect and verify success
            if (dup2(fileDescriptor, 0) == -1) {
                fprintf(stderr, "command failed: cannot set '/dev/null' as stdin for background task.");
                exit(EXIT_FAILURE);
            }

            //stdout
            fileDescriptor = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fileDescriptor == -1) {
                fprintf(stderr, "command failed: cannot open stdout '/dev/null' for background task.");
                exit(EXIT_FAILURE);
            }
            //redirect and verify success
            if (dup2(fileDescriptor, 1) == -1) {
                fprintf(stderr, "command failed: cannot set '/dev/null' as stdout for background task.");
                exit(EXIT_FAILURE);
            }
        }

        //regular process redirection for "<" stdin and/or ">" stdout
        if (strcmp(commandArgs[i], "<") == 0 || strcmp(commandArgs[i], ">") == 0) {

            //check existence to avoid a segfault; for debugging only
            if (commandArgs[i+1] == NULL)
                printf("Warning bad command! Segfault will occur. Fix code.");

            //stdin
            if (strcmp(commandArgs[i], "<") == 0) {
                fileDescriptor = open(commandArgs[i+1], O_RDONLY);
                if (fileDescriptor == -1) {
                    fprintf(stderr, "command failed: cannot open '%s' as input\n", commandArgs[i+1]);
                    exit(EXIT_FAILURE);
                }

                //redirect and verify success
                if (dup2(fileDescriptor, 0) == -1) {
                    fprintf(stderr, "command failed: redirection of stdin failed.\n");
                    exit(EXIT_FAILURE);
                }
            }
            //stdout
            else {
                fileDescriptor = open(commandArgs[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fileDescriptor == -1) {
                    fprintf(stderr, "command failed: cannot open '%s' as output\n", commandArgs[i+1]);
                    exit(EXIT_FAILURE);
                }

                //redirect and verify success
                if (dup2(fileDescriptor, 1) == -1) {
                    fprintf(stderr, "command failed: redirection of stdout failed.\n");
                    exit(EXIT_FAILURE);
                }
            }

            //reorganize command argument array after processing redirection
            argsCount -= 2;
            for (int j = i; j < argsCount; j++)
                commandArgs[j] = commandArgs[j+2];
            //these need to point to NULL otherwise exel() keeps reading them
            commandArgs[argsCount] = NULL;
            commandArgs[argsCount+1] = NULL;

            i--; //rerun at the same index
        }
    }
}

//Helps the shell process handle foreground and background processes
void ProcessHandler(pid_t spawnpid) {
    int childExitMethod = -5;

    //foreground task: shell needs to wait for finish
    if (!isBackgroundTask) {
        waitpid(spawnpid, &childExitMethod, 0);

        //check and record exit status
        if (WIFEXITED(childExitMethod)) {
            isTermBySignal = 0;
            exitStatus = WEXITSTATUS(childExitMethod);
            //printf("process finished with exit status: %d\n", exitStatus);
        }
        else if (WIFSIGNALED(childExitMethod)) {
            isTermBySignal = 1;
            exitStatus = WTERMSIG(childExitMethod);
            printf("process terminated by signal: %d\n", exitStatus);
        }
    }
    //background task handling
    else {
        //print out beginning of background running task
        printf("background task pid is '%d'\n", (int)spawnpid);

        //slot it into the most recent available element in bg array for the records
        for (int i = 0; i < BG_PID_SIZE; i++) {
            if (bgPid[i] == 0) {
                bgPid[i] = (int) spawnpid;
                break;
            }
        }
    }
}

//Check background processes for completion
void CheckBGProcesses() {
    int bgExitStatus;
    int childExitMethod = -5;

    for (int i = 0; i < BG_PID_SIZE; i++) {
        if (waitpid(bgPid[i], &childExitMethod, WNOHANG) > 0) {
            //check and record exit status
            if (WIFEXITED(childExitMethod)) {
                bgExitStatus = WEXITSTATUS(childExitMethod);
                printf("background pid %d is done with exit status: %d\n", bgPid[i], bgExitStatus);
            }
            else if (WIFSIGNALED(childExitMethod)) {
                bgExitStatus = WTERMSIG(childExitMethod);
                printf("background pid %d is done with signal termination: %d\n", bgPid[i], bgExitStatus);
            }

            bgPid[i] = 0; //remove completed bg pid for new bg processes to use
        }
    }
}

//function on handling SIGTSTP to use as enable/disable background processes
void CatchStopSigForBackgroundToggle(int signo) {
    if (isBackgroundEnabled) {
        isBackgroundEnabled = 0;
        char *message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);
    }
    else {
        isBackgroundEnabled = 1;
        char *message = "\nExiting foreground-only mode (& is re-enabled)\n";
        write(STDOUT_FILENO, message, 48);
    }
    fflush(stdout);
}


////Main - primary shell process
int main() {
    //shell signal setup for SIGINT (ctrl+c) to be ignored
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &SIGINT_action, NULL);

    //shell signal setup for SIGTSTP to toggle enable/disable background processes
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = CatchStopSigForBackgroundToggle;
    SIGTSTP_action.sa_flags = SA_RESTART; //this prevents function from failing if ctrl+z while waiting child
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    int exitShell = 0;
    //initiate w/ 0 to clear bg process records
    for (int i = 0; i < BG_PID_SIZE; i++)
        bgPid[i] = 0;

    //main shell loop
    while (exitShell == 0) {
        CheckBGProcesses();
        UserInput(); //flushes everything and sets prompt
        ParseCommandPrompt(); //parses prompt

        //kills all bg child processes, then exits shell if user specifies 'exit' command
        if (strcmp(commandArgs[0], "exit") == 0 && argsCount == 1) {
            CommandExit();
            exitShell = 1;
        }

        //reset shell loop if blank line, space, or bogus one char value
        else if (strcmp(commandArgs[0], blank) == 0) {
//            printf("blank line in main () command prompt: '%s'. Length: '%d'\n", commandPrompt, (int) strlen(commandPrompt));
//            fflush(stdout);
            continue;
        }

        //reset shell loop if user enters comment
        else if (strcmp(commandArgs[0], "#") == 0 || commandArgs[0][0] == '#')
            continue;

        //built-in command cd to change directory
        else if (strcmp(commandArgs[0], "cd") == 0) {
            CommandCd();
        }

        //built-in command to display exit status
        else if (strcmp(commandArgs[0], "status") == 0) {
            CommandStatus();
        }

        //fork and execute other command
        else {
            pid_t spawnpid = -5;
            spawnpid = fork();

            switch (spawnpid) {
                //fork error - no child process created, parent keeps going
                case -1:
                    perror("Hull Breach!");
                    break;

                case 0: //child process runs
                    ChildExecute(spawnpid);
                    printf("child exec() did not exit normally - something not caught!\n");
                    fflush(stdout);
                    exit(0);
                    break;

                default: //parent process runs
                    ProcessHandler(spawnpid);
                    break;
            }
        }
    }

    free(commandArgs);
    commandArgs = NULL;
    return 0;
}