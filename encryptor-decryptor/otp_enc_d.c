#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <zconf.h>

////Global variables
#define CHILD_POOL_SIZE     5
#define MAX_CHARACTER_LENGTH    100000
char plaintext[MAX_CHARACTER_LENGTH];
char key[MAX_CHARACTER_LENGTH];
char ciphertext[MAX_CHARACTER_LENGTH];


////Helper functions
//function prototoypes to avoid implicit declaration issues
int SetupListenSocket(long listenPort);
void AcceptConnections(int listenSocket);
void ProcessEncryption(int establishedConnectionFD);
void ProcessCipherText();
int ConvertToDec(char character);


//sets up listening socket based on user input
int SetupListenSocket(long listenPort) {
    //create listen socket
    int listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == -1) {
        fprintf(stderr, "Error creating listen socket.");
        exit(EXIT_FAILURE);
    }

    //setup address of listen socket
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(listenPort);
    serverAddress.sin_addr.s_addr = INADDR_ANY; //allow connections anywhere


    //bind listen socket to port
    int bindStatus = bind(listenSocket, (struct sockaddr*) &serverAddress, sizeof(serverAddress));
    if (bindStatus == -1) {
        fprintf(stderr, "Cannot bind listen socket to established address' port.");
        exit(EXIT_FAILURE);
    }

    //start listening for incoming connections
    int listenStatus = listen(listenSocket, 5);
    if (listenStatus == -1) {
        fprintf(stderr, "Listening socket unable to listen.");
        exit(EXIT_FAILURE);
    }

    return listenSocket;
}

//blocks and waits for open incoming connections, then processes them; loops
void AcceptConnections(int listenSocket) {
    //setup variables for client information
    struct sockaddr_in clientAddress;
    socklen_t sizeOfClientInfo = sizeof(clientAddress);
    int establishedConnectionFD;

    //loops to receive incoming connections
    while (1) {
        //block and wait for incoming client connection
        establishedConnectionFD = accept(listenSocket, (struct sockaddr *) &clientAddress, &sizeOfClientInfo);
        if (establishedConnectionFD == -1) {
            int myPID = (int) getpid();
            fprintf(stderr, "Open connection (PID: %i) failed to accept connection!", myPID);
            exit(EXIT_FAILURE);
        }

        //just check to make sure connects successfully
        //printf("SERVER: Connected Client at port %d\n", ntohs(clientAddress.sin_port));


        //receive identity from otp_enc_d
        char programName[15];
        memset(programName, '\0', sizeof(programName));
        int receiveIdentity = (int) recv(establishedConnectionFD, programName, sizeof(programName), 0);
        if (receiveIdentity == -1)
            fprintf(stderr, "Server: error reading identity from socket.");

        //compare identity; disconnect if it's not otp_enc
        if (strcmp(programName, "otp_enc") != 0) {
            fprintf(stderr, "Server: program at port %d is not otp_enc. "
                            "Identity received: %s.\n", ntohs(clientAddress.sin_port), programName);
            close(establishedConnectionFD);
            exit(2);
        }
//        else
//            printf("Server: Identity of client otp_enc established!\n");

        //send its own identity for client otp_enc to verify
        memset(programName, '\0', sizeof(programName));
        strcpy(programName, "otp_enc_d");
        //send identity to otp_enc_d via own program name
        int sendIdentity = (int) send(establishedConnectionFD, programName, sizeof(programName), 0);
        if (sendIdentity == -1)
            fprintf(stderr, "Server: error writing identity to socket.");
        if (sendIdentity < strlen(programName))
            fprintf(stderr, "Server: not all of identity written to socket.");

        //receives plaintext and key, and sends out cipher
        ProcessEncryption(establishedConnectionFD);

        close(establishedConnectionFD);
    }
}

//converts A-Z, space dec values to 0-26 (A-Z, space)
int ConvertToDec(char character) {
    //space
    if (character == 32)
        return 26;
    //A-Z
    else if (character >= 65 && character <= 90)
        return (character - 65);
    //debugging: invalid character; this should already be caught much earlier and never reach this
    else {
        fprintf(stderr, "Server: Warning! Cannot convert an invalid char to dec value.");
        exit(EXIT_FAILURE);
    }
}

//uses the received plaintext and key strings to create ciphertext
void ProcessCipherText() {
    memset(ciphertext, '\0', MAX_CHARACTER_LENGTH);

    int result, plainInt, keyInt;
    //calculate encrypt/decrypt
    for (int i = 0; i < strlen(plaintext); i++) {
        //convert to decimal values 0-26
        plainInt = ConvertToDec(plaintext[i]);
        keyInt = ConvertToDec(key[i]);

        //calculate ciphertext decimal
        result = (plainInt + keyInt) % 27;

        //store appropriate character into ciphertext
        if (result == 26) //space
            ciphertext[i] = (char) 32;
        else //A-Z
            ciphertext[i] = (char) (result + 65);
    }
}

void ProcessEncryption(int establishedConnectionFD) {
    //receive length of text and convert to long
    char strLength[10];
    memset(strLength, '\0', sizeof(strLength));
    int receiveLength = (int) recv(establishedConnectionFD, strLength, sizeof(strLength), 0);
    if (receiveLength == -1)
        fprintf(stderr, "Server: error reading length from socket.");
    char *ptr;
    long textLength = strtol(strLength, &ptr, 10);

    //receive plaintext
    memset(plaintext, '\0', MAX_CHARACTER_LENGTH);
    int receivePlaintext = (int) recv(establishedConnectionFD, plaintext, (size_t) textLength, 0);
    if (receivePlaintext == -1)
        fprintf(stderr, "Server: error reading plaintext from socket.");

    //receive key
    memset(key, '\0', MAX_CHARACTER_LENGTH);
    int receiveKey = (int) recv(establishedConnectionFD, key, (size_t) textLength, 0);
    if (receiveKey == -1)
        fprintf(stderr, "Server: error reading key from socket.");

    //process ciphertext
    ProcessCipherText();

    //send ciphertext to client
    int sendCiphertext = (int) send(establishedConnectionFD, ciphertext, (size_t) strlen(ciphertext), 0);
    if (sendCiphertext == -1)
        fprintf(stderr, "Server: error writing ciphertext to socket.");
    if (sendCiphertext < strlen(ciphertext))
        fprintf(stderr, "Server: not all of ciphertext written to socket.");
}

////Acts as server. Waits for connection to receive plaintext/key, encrpyts, and sends ciphertext
int main(int argc, char *argv[]) {
    //checks if it's in the correct format of "otp_enc_d <listening port>"
    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments");
        exit(EXIT_FAILURE);
    }

    //convert port int and store as number
    char *ptr;
    long listenPort;
    errno = 0;
    listenPort = strtol(argv[1], &ptr, 10);
    if (errno != 0 && listenPort == 0) { //check valid integer entered
        fprintf(stderr, "Invalid port entered");
        exit(EXIT_FAILURE);
    }

    //setup listen socket and return info of it here
    int listenSocket = SetupListenSocket(listenPort);

    //fork 5 times before accepting to have a pool of processes available for processing connections
    for (int i = 0; i < CHILD_POOL_SIZE; i++) {
        pid_t spawnid = fork();

        if (spawnid == -1) {
            fprintf(stderr, "Warning! Failed to create a child process to accept incoming connections.");
            exit(EXIT_FAILURE);
        }
        //child process runs this - break from the for loop
        else if (spawnid == 0)
            break;

        //parent keeps running through the for-loop to spawn rest while child breaks
    }

    //number of processes in pool to accept incoming connection: parent + CHILD_POOL_SIZE (default: 4)
    //on loop: waits for connection, accepts, processes, returns to waiting
    AcceptConnections(listenSocket);

    close(listenSocket);
    return 0;
}