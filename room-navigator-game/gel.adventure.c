#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <memory.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

////A. Global Variables --------------------------------------------------------------------
#define DIRECTORY_PREFIX    "gel.rooms."
#define DIRECTORY_PREFIX_CHAR_COUNT 10  //"gel.rooms."
#define ROOM_COUNT    7
#define MAX_CONNECTIONS 6
#define TIME_FILE   "currentTime.txt"

//file data will be read into struct Data
struct Room {
    char *name;
    char *type;
    struct Room **connection;
    int connectionCount;
};

//node for linked list for keeping track of path player takes
struct Node {
    struct Room *room;
    struct Node *next;
};

//mutex lock to be used in different functions
//setup mutex lock to prevent main thread from displaying time before thread #1 records it
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

////B. Functions To Identify Correct Rooms Directory --------------------------------------
//function prototypes to avoid implicit declaration issues
int RoomsDirCount();
char* MostRecentRoomsDir();

//finds most recent rooms directory and returns string of directory name
char* MostRecentRoomsDir() {
    int count = RoomsDirCount();
    struct stat *roomDirs = (struct stat *) malloc(count * sizeof(struct stat));
    DIR *d;
    struct dirent *dir;

    //open current directory and error check
    if (count == 0)
        printf("Warning! There were no rooms directories! This might create seg fault!");
    d = opendir(".");
    if (d == NULL)
        printf("Warning! Issue with opening current working directory!");

    //iterates through and grabs relevant directory information
    char prefixMatch[DIRECTORY_PREFIX_CHAR_COUNT+1];
    int i = 0;
    time_t mostRecent = 0;
    char *mostRecentDir;
    while ((dir = readdir(d)) != NULL) {
        //gets just the prefix of directory w/o the pid portion
        memset(prefixMatch, '\0', sizeof(prefixMatch));
        strncpy(prefixMatch, dir->d_name, 10);

        if (strcmp(prefixMatch, DIRECTORY_PREFIX) == 0) {
            //this would happen if this program runs before creating any rooms directory.. don't do that
            if (stat(dir->d_name, &roomDirs[i]) < 0)
                printf("Warning! Failed to find stat of a rooms directory");

            //takes the time and directory name of most recent among all iterated directories
            if(roomDirs[i].st_mtime > mostRecent) {
                mostRecent = roomDirs[i].st_mtime;
                mostRecentDir = dir->d_name;
            }

            i++; //i should never be greater than count, unless directory was modified concurrently
        }
    }
    closedir(d);

    //sets up a malloc char array to return out of the function the name of most recent rooms dir
    char *recentRoom = (char*) malloc(sizeof(char)*(DIRECTORY_PREFIX_CHAR_COUNT+10));
    memset(recentRoom, '\0', sizeof(recentRoom));
    strcpy(recentRoom, mostRecentDir);

    free(roomDirs); //frees what was malloc'd to hold the stats

    return recentRoom;
}

//finds all relevant room directories and return char* array of them for further filtering
int RoomsDirCount() {
    DIR *d;
    struct dirent *dir;

    //open current directory and error check
    d = opendir(".");
    if (d == NULL)
        printf("Warning! Issue with opening current working directory!");

    //iterates through and gets count of relevant rooms directory
    int count = 0;
    char prefixMatch[DIRECTORY_PREFIX_CHAR_COUNT+1];
    while ((dir = readdir(d)) != NULL) {
        //gets just the prefix of directory w/o the pid portion
        memset(prefixMatch, '\0', sizeof(prefixMatch));
        strncpy(prefixMatch, dir->d_name, 10);

        //adds to count so get only count of relevant directories, not all of them
        if (strcmp(prefixMatch, DIRECTORY_PREFIX) == 0)
            count++;
    }
    closedir(d);

    return count;
}


////C. Functions to Bring Room Data Into Memory -----------------------------------------
//function prototypes to avoid implicit declaration issues
void FreeMemory(struct Room **);
int IsHiddenFile(char*);
void InstantiateRoomNamesTypes(char *, struct Room **);
void InstantiateRoomConnections(char *, struct Room **);
struct Room** FillRoomsData(char *);

//given rooms directory, iterate through all files and instantiate Room structs
struct Room** FillRoomsData(char *roomDir) {
    struct Room **rooms = (struct Room **) malloc(ROOM_COUNT * sizeof(struct Room*));

    //instantiate rooms
    InstantiateRoomNamesTypes(roomDir, rooms);
    InstantiateRoomConnections(roomDir, rooms);

    return rooms;
}

//given rooms directory, instantiate room structs with name and type
void InstantiateRoomNamesTypes(char *roomDir, struct Room **rooms) {
    DIR *d;
    struct dirent *dir;
    char filePath[DIRECTORY_PREFIX_CHAR_COUNT+15];

    //open rooms directory and error check
    d = opendir(roomDir);
    if (d == NULL)
        printf("Warning! Issue with opening current working directory!");

    //iterates through files and instantiate room name + type
    int i = 0;
    while ((dir = readdir(d)) != NULL) {
        if ((IsHiddenFile(dir->d_name) == 0) && (i < ROOM_COUNT)) { //not a hidden, non-room file
            //create full dirpath to file
            memset(filePath, '\0', DIRECTORY_PREFIX_CHAR_COUNT+15);
            strcpy(filePath, roomDir);
            strcat(filePath, "/");
            strcat(filePath, dir->d_name);

            //instantiate room
            rooms[i] = (struct Room*) malloc(sizeof(struct Room));

            //find name + type
            char line[50];
            char lineId[7];

            FILE* file = fopen(filePath, "r");
            while(fgets(line, sizeof(line), file)) {
                //get first 6 characters to identify which line is being looked at
                memset(lineId, '\0', 7);
                memcpy(lineId, &line[0], 6);

                //based on identifier, correlate and instantiate data
                if (strcmp(lineId, "ROOM N") == 0) { //ROOM NAME
                    rooms[i]->name = (char*) malloc(50*sizeof(char));
                    memset(rooms[i]->name, '\0', 50);
                    strncpy(rooms[i]->name, line+11, strlen(line)-12);
                }
                else if (strcmp(lineId, "ROOM T") == 0) { //ROOM TYPE
                    rooms[i]->type = (char*) malloc(50*sizeof(char));
                    memset(rooms[i]->type, '\0', 50);
                    strncpy(rooms[i]->type, line+11, strlen(line)-12);
                }
            }
            fclose(file);

            i++; //increments for next room struct in array
        }
    }
    closedir(d);
}

//given room file, fill in necessary data for struct Room
void InstantiateRoomConnections(char *roomDir, struct Room **rooms) {
    DIR *d;
    struct dirent *dir;
    char filePath[DIRECTORY_PREFIX_CHAR_COUNT+15];

    //open rooms directory and error check
    d = opendir(roomDir);
    if (d == NULL)
        printf("Warning! Issue with opening current working directory!");

    //iterates through files, match file with correct room struct, and instantiate connections
    int i = 0;
    while ((dir = readdir(d)) != NULL) {
        if ((IsHiddenFile(dir->d_name) == 0) && (i < ROOM_COUNT)) { //not a hidden, non-room file
            //create full dirpath to file
            memset(filePath, '\0', DIRECTORY_PREFIX_CHAR_COUNT+15);
            strcpy(filePath, roomDir);
            strcat(filePath, "/");
            strcat(filePath, dir->d_name);

            //match file name with room
            int j;
            for (j = 0; j < ROOM_COUNT; j++) {
                if (strcmp(dir->d_name, rooms[j]->name) == 0)
                    break; //j holds correct index for room struct
            }

            //setup room
            rooms[j]->connectionCount = 0;
            rooms[j]->connection = (struct Room **) malloc(MAX_CONNECTIONS*sizeof(struct Room*));

            //make connections
            char line[50];
            char lineId[7];
            char lineData[50];

            FILE* file = fopen(filePath, "r");
            while(fgets(line, sizeof(line), file)) {
                //get first 6 characters to identify which line is being looked at
                memset(lineId, '\0', 7);
                memcpy(lineId, &line[0], 6);

                //based on identifier, correlate and instantiate data
                if (strcmp(lineId, "CONNEC") == 0) {
                    memset(lineData, '\0', 50);
                    strncpy(lineData, line+14, strlen(line)-15);

                    //find the correct room with matching name of connection, and add it to connection array
                    int k;
                    for (k = 0; k < ROOM_COUNT; k++) {
                        if (strcmp(rooms[k]->name, lineData) == 0) {
                            rooms[j]->connection[rooms[j]->connectionCount] = rooms[k];
                            rooms[j]->connectionCount++;
                            break;
                        }
                    }
                }
            }
            fclose(file);

            i++; //acts as a protection in case bad directory has additional files
        }
    }
    closedir(d);
}

//checks whether passed in file name begins w/ ".". If it does, then likely autogenerated hidden file
int IsHiddenFile(char* fileName) {
    char hidden[2];
    memset(hidden, '\0', sizeof(hidden));
    strncpy(hidden, fileName, 1);

    //all hidden files start w/ .
    //there shouldn't be any other files since the directory was created by buildrooms...
    if (strcmp(hidden, ".") == 0)
        return 1;
    else
        return 0;
}

//free all allocated memory, namely for structures and connections
void FreeMemory(struct Room **rooms) {
    int i;
    for (i = 0; i < ROOM_COUNT; i++) {
        free(rooms[i]->connection);
        free(rooms[i]->name);
        free(rooms[i]->type);
        free(rooms[i]);
    }

    free(rooms);
}


////D. Functions For Game & UI -------------------------------------------------------------
//function prototypes to prevent implicit declaration issues
int BeginGame(struct Room**);
int IdentifyStartRoom(struct Room**);
int IsEndRoom(struct Room *);
struct Room* UserQuery(struct Room**, struct Room*);
void DispEnd(int, struct Node *);
void FreeNodeMemory(struct Node *);
void recordTime();
void displayTime();

//primary interface for player interaction. utilizes several helper functions to pull data
int BeginGame(struct Room** rooms) {
    //setup for starting room, step tracker, and also head node of linked list for path tracking
    int startIndex = IdentifyStartRoom(rooms);
    struct Room* currentLocation = rooms[startIndex];
    int roomTracker = 0;
    struct Node *head = (struct Node*) malloc(sizeof(struct Node));
    head->room = currentLocation;
    head->next = NULL;
    struct Node *trackPointer = head;

    //primary loop of user interaction and movement
    while(!IsEndRoom(currentLocation)) {
        currentLocation = UserQuery(rooms, currentLocation);

        //tracking data with a classic linked list!
        roomTracker++;
        trackPointer->next = (struct Node*) malloc(sizeof(struct Node));
        trackPointer = trackPointer->next;
        trackPointer->room = currentLocation;
        trackPointer->next = NULL;
    }

    //display results. make sure to pass head->next as first room doesn't get listed
    DispEnd(roomTracker, head->next);

    //free up nodes used for path tracking
    FreeNodeMemory(head);
}

//displays end results: message, steps, and path
void DispEnd(int steps, struct Node *tracker) {
    printf("YOU HAVE FOUND THE END ROOM. CONGRATULATIONS!\n");
    printf("YOU TOOK %d STEPS. YOUR PATH TO VICTORY WAS:\n", steps);
    while (tracker != NULL) {
        printf("%s\n", tracker->room->name);
        tracker = tracker->next;
    }
}

//frees linked list used for path tracking
void FreeNodeMemory(struct Node *head) {
    struct Node *pointer = head->next;

    while (head != NULL) {
        free(head);
        head = pointer;
        if (head == NULL) { //for last, so pointer is not trying to reassign null->next
            free(head);
            head = NULL;
        }
        else
            pointer = head->next;
    }
}

//handles logic for displaying primary interface and redirect of bad responses
struct Room* UserQuery(struct Room** rooms, struct Room* currentLocation) {
    //begin looping for user I/O
    while (1) {
        //display information based on current location
        printf("CURRENT LOCATION: %s\n", currentLocation->name);
        printf("POSSIBLE CONNECTIONS: ");
        int i;
        for (i = 0; i < currentLocation->connectionCount-1; i++)
            printf("%s, ", currentLocation->connection[i]->name);
        printf("%s.\n", currentLocation->connection[i]->name);
        printf("WHERE TO? >");

        //user query
        char userInput[256];
        memset(userInput, '\0', 256);
        scanf(" %s", userInput);

        //process input if matches
        for (i = 0; i < currentLocation->connectionCount; i++) {
            if (strcmp(userInput, currentLocation->connection[i]->name) == 0) {
                currentLocation = currentLocation->connection[i];
                printf("\n");
                return currentLocation;
            }
        }

        if(strcmp(userInput, "time") == 0) {

            //main threads lets go of lock, passing it to time thread
            pthread_mutex_unlock(&lock);

            //after unlock, time thread able to run this:
            recordTime();

            //time thread is destroyed at end of recordTime(), so main thread runs
            displayTime();

            //main thread picks up lock again and recreates time thread within this loop
            pthread_mutex_lock(&lock);

            //recreate time thread, which is idling until recordTime() is called after this loop
            int resultInt1;
            pthread_t threadId1;
            resultInt1 = pthread_create(&threadId1, NULL, (void*) recordTime, NULL);
            if (resultInt1 != 0)
                printf("Warning! Error creating thread #1 for recording time.");
        }
        else //if input reaches here, means not a valid input
            printf("\nHUH? I DON’T UNDERSTAND THAT ROOM. TRY AGAIN.\n\n");
    }
}

//main thread retrieves the time afterwards
void displayTime() {
    char output[100];
    memset(output, '\0', 100);

    FILE* file = fopen(TIME_FILE, "r");
    fgets(output, sizeof(output), file);
    fclose(file);

    printf("\n%s\n\n", output);
}

//2nd thread creates a file with current time in same directory
void recordTime() {
    //time thread locks and prevents main thread from running
    pthread_mutex_lock(&lock);


    //create string for time format
    char output[100];
    memset(output, '\0', 100);
    time_t t = time(NULL);
    struct tm *tmp = localtime(&t);

    //1:03pm, Tuesday, September 13, 2016
    if (strftime(output, 100, "%l:%M%P, %A, %B %d, %Y", tmp) == 0)
        printf("Warning! Error with recording time in correct format with strftime.");

    //write time to new file
    FILE* file = fopen(TIME_FILE, "w");
    fprintf(file, "%s", output);
    fclose(file);

    //time thread unlocks, and after this function gets destroyed
    pthread_mutex_unlock(&lock);
}

//returns true if user location is at the end room
int IsEndRoom(struct Room *currentLocation) {
    if (strcmp(currentLocation->type, "END_ROOM") == 0)
        return 1;
    else
        return 0;
}

//returns index of start room
int IdentifyStartRoom(struct Room** rooms) {
    int i;
    for (i = 0; i < ROOM_COUNT; i++) {
        if (strcmp(rooms[i]->type, "START_ROOM") == 0)
            return i;
    }

    printf("Warning! Could not find start room!");
    return -1;
}


////E. MAIN -------------------------------------------------------------------------------
int main() {
    //main thread will always grab this lock
    pthread_mutex_lock(&lock);

    //create second thread, which is idling until recordTime() is called
    int resultInt1;
    pthread_t threadId1;
    resultInt1 = pthread_create(&threadId1, NULL, (void*) recordTime, NULL);
    if (resultInt1 != 0)
        printf("Warning! Error creating thread #1 for recording time.");

    //get most recent relevant rooms directory
    char *roomDir;
    roomDir = MostRecentRoomsDir();

    //initialize game data in memory based on file data
    struct Room** rooms = FillRoomsData(roomDir);

    //start the game
    BeginGame(rooms);

    //free up anything else malloc'd
    FreeMemory(rooms);
    free(roomDir);

    return 0;
}