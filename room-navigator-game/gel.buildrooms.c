#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <memory.h>
#include <sys/stat.h>
#include <zconf.h>
#include <fcntl.h>


////A. Global variables required for rooom initial setup -------------------------------------------------------------

#define ROOM_COUNT    7
#define MAX_CONNECTION_COUNT    6
#define MIN_CONNECTION_COUNT    3
#define DIRECTORY_PREFIX        "gel.rooms."

//each room will be instantiated later as struct with all necessary information to setup
struct Room {
    char *name;
    char *type;
    struct Room **connection;
    int connectionCount;
};

//array of 10 strings with the hardcoded names for rooms
char *names[] = {
        "USA",
        "China",
        "Russia",
        "Persia",
        "Congo",
        "Korea",
        "Canada",
        "Peru",
        "Iran",
        "France"
};


////B. Functions for room initialization and setup--------------------------------------------------------------------

//Generates a random unique array of integers from 0 to 9 using Knuth algorithm. Then just shuffles around within array
//This is used to efficiently select random unique names.
#define RANGE   7
#define VALUES   10
int* ShuffleIntArray() {
    int in, im;
    int *randomNum = malloc(sizeof(int)*RANGE);
    im = 0;

    //Knuth algorithm
    for (in = 0; in < VALUES && im < RANGE; ++in) {
        int rn = VALUES - in;
        int rm = RANGE - im;
        if (rand() % rn < rm)
            randomNum[im++] = in;
    }

//    for (int l = 0; l < RANGE; l++)
//        printf("%d ", randomNum[l]);

    //reshuffle by swapping values randomly within the array
    int i;
    for (i = 0; i < RANGE; i++) {
        int j = rand() % RANGE; //0-6
        int temp = randomNum[j];
        randomNum[j] = randomNum[i];
        randomNum[i] = temp;
    }

//    printf("\n");
//    for (int k = 0; k < RANGE; k++)
//        printf("%d ", randomNum[k]);

    return randomNum;
}

//Primary setup: allocates memory for room structs and connections. Sets up names and room types as well.
//Returns a Rooms pointer as a handle for further modification and also to free() later on.
struct Room** SetupRooms () {
    struct Room **rooms = (struct Room**) malloc(sizeof(struct Room*)*ROOM_COUNT);

    srand(time(NULL));
    int *values = ShuffleIntArray(); //generate random unique int to assign names
    int i;
    for (i = 0; i < ROOM_COUNT; i++) {
        //allocate memory for a room and initialize
        rooms[i] = (struct Room*) malloc(sizeof(struct Room));

        rooms[i]->name = names[values[i]];

        //given randomized values, just setup first 2 rooms as start/end, rest as mid
        if (i == 0)
            rooms[i]->type = "START_ROOM";
        else if (i == 1)
            rooms[i]->type = "END_ROOM";
        else
            rooms[i]->type = "MID_ROOM";

        //space for 6 Room pointers to other Room structs
        rooms[i]->connection = (struct Room**) malloc(sizeof(struct Room*)*MAX_CONNECTION_COUNT);

        //set count to 0, which gets incremented as we create connections
        rooms[i]->connectionCount = 0;
    }
    free(values); //make sure to free the int array used to generate room name assignment

    return rooms;
}

//free all allocated memory, namely for structures and connections
void FreeMemory (struct Room **rooms) {
    int i;
    for (i = 0; i < ROOM_COUNT; i++) {
        free(rooms[i]->connection);
        free(rooms[i]);
    }

    free(rooms);
}


////C. Functions to create connections between all initialized rooms -----------------------------------------------
//declaring all function prototypes to avoid implicit declaration issues
void AddRandomConnection(struct Room **);
struct Room* GetRandomRoom(struct Room **);
int CanAddConnectionFrom(struct Room *);
int ConnectionAlreadyExists(struct Room *, struct Room *);
void ConnectRoom(struct Room *, struct Room *);
int IsSameRoom(struct Room *, struct Room *);
int IsGraphFull(struct Room **);
void SetupAllConnections(struct Room **);


//Makes random, valid connections with rooms until all rooms have at least 3-6 connections
void SetupAllConnections(struct Room **rooms) {
    while (IsGraphFull(rooms) == 0) {
        AddRandomConnection(rooms);
    }
}

// Returns true if all rooms have 3 to 6 outbound connections, false otherwise
int IsGraphFull(struct Room **rooms) {
    int i;
    for (i = 0; i < ROOM_COUNT; i++) {
        if (rooms[i]->connectionCount < MIN_CONNECTION_COUNT)
            return 0;
    }

    return 1;
}

// Adds a random, valid outbound connection from a Room to another Room
void AddRandomConnection(struct Room **rooms) {
    struct Room *A = GetRandomRoom(rooms);
    struct Room *B = GetRandomRoom(rooms);

//    printf("RoomA %s\n", A->name);
//    printf("RoomB %s\n", B->name);

    //takes a random room that can still make connection
    while(1) {
        A = GetRandomRoom(rooms);
        if (CanAddConnectionFrom(A) == 1)
            break;
    }

    do {
        B = GetRandomRoom(rooms);
    }
    while(CanAddConnectionFrom(B) == 0 || IsSameRoom(A, B) == 1 || ConnectionAlreadyExists(A, B) == 1);

//    printf("Connecting %s with %s.\n", A->name, B->name);

    ConnectRoom(A, B);

    //debugging purposes:
//    printf("Room %s connections:\n", A->name);
//    for (int i = 0; i < A->connectionCount; i++)
//        printf("connection %d: %s\n", i, A->connection[i]->name);
//    printf("Room %s connections:\n", B->name);
//    for (int j = 0; j < B->connectionCount; j++)
//        printf("connection %d: %s\n", j, B->connection[j]->name);
//
//    printf("\n\n\n");
}

// Returns a random Room pointer from given array of Room pointers, does NOT validate if connection can be added
struct Room* GetRandomRoom(struct Room **rooms) {
    int i = rand() % ROOM_COUNT;
    //printf("%d\n", i);
    return rooms[i];
}

// Returns true if a connection can be added from Room x (< 6 outbound connections), false otherwise
int CanAddConnectionFrom(struct Room *x) {
    if (x->connectionCount < MAX_CONNECTION_COUNT)
        return 1;
    else
        return 0;
}

// Returns true if a connection from Room x to Room y already exists, false otherwise
int ConnectionAlreadyExists(struct Room *x, struct Room *y)
{
    //iterates over x's connections and checks if y is in there
    int i;
    for (i = 0; i < x->connectionCount; i++) {
        if (IsSameRoom(x->connection[i], y))
            return 1;
    }

    return 0; //no connection found
}

// Connects Rooms x and y together, does not validate whatsoever.
void ConnectRoom(struct Room *x, struct Room *y) {
    x->connection[x->connectionCount] = y;
    y->connection[y->connectionCount] = x;
    x->connectionCount += 1;
    y->connectionCount += 1;
}

// Returns true if Rooms x and y are the same Room, false otherwise
int IsSameRoom(struct Room *x, struct Room *y) {
    if (strcmp(x->name, y->name) == 0)
        return 1;
    else
        return 0;
}


////D. Function to output finalized data to files ----------------------------------------------------------------
void CreateRoomFiles(struct Room **rooms) {
    //setup directory name
    int pid = getpid();
    char directoryName[20];
    memset(directoryName, '\0', 20);
    sprintf(directoryName, DIRECTORY_PREFIX"%d", pid);

    //create directory with given name
    int fileDescriptor = mkdir(directoryName, 0777);

    if (fileDescriptor == -1)
        printf("Warning, directory went wrong!");

    //iterate through rooms and create file for each
    int i;
    for (i = 0; i < ROOM_COUNT; i++) {
        //setup file name
        char fileName[30];
        memset(fileName, '\0', 30);
        sprintf(fileName, DIRECTORY_PREFIX"%d/%s", pid, rooms[i]->name);

        //create file
        fileDescriptor = open(fileName, O_WRONLY | O_TRUNC | O_CREAT, 0777);

        if(fileDescriptor == -1)
            printf("Warning, file creation/opening went wrong!");

        //setup string content to write to file
        char fileContent[200];
        memset(fileContent, '\0', 200);
        sprintf(fileContent, "ROOM NAME: %s\n", rooms[i]->name);

        //construction of "CONNECTION: <room name>"
        int j;
        for (j = 0; j < rooms[i]->connectionCount; j++) {
            char connectionContent[30];
            memset(connectionContent, '\0', 30);
            sprintf(connectionContent, "CONNECTION %d: %s\n", j + 1, rooms[i]->connection[j]->name);
            strcat(fileContent, connectionContent);
        }

        char typeContent[30];
        memset(typeContent, '\0', 30);
        sprintf(typeContent, "ROOM TYPE: %s\n", rooms[i]->type);
        strcat(fileContent, typeContent);

//        printf(fileContent); //for debugging
//        printf("\n\n");

        //write to file
        ssize_t nwritten;
        nwritten = write(fileDescriptor, fileContent, strlen(fileContent)*sizeof(char));

        //close file
        close(fileDescriptor);
    }
}


////E. Main Run
int main() {
    struct Room **rooms = SetupRooms();

//    for (int i = 0; i < ROOM_COUNT; i++) { //for debugging
//        printf("%s ", rooms[i]->name);
//    }
//    printf("\n\n");

    SetupAllConnections(rooms);
    CreateRoomFiles(rooms);

    //output for logic debugging check
//    for (int j = 0; j < ROOM_COUNT; j++) {
//        printf("Room %s has the following connections:\n", rooms[j]->name);
//        for (int k = 0; k < rooms[j]->connectionCount; k++)
//            printf("____connection %d: %s\n", k, rooms[j]->connection[k]->name);
//        printf("Type: %s", rooms[j]->type);
//        printf("\n\n");
//    }

    FreeMemory(rooms);
    return 0;
}