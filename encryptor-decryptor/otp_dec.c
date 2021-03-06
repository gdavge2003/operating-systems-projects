#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <zconf.h>
#include <fcntl.h>

////Global variables and constants
#define MAX_CHARACTER_LENGTH    100000
char plaintext[MAX_CHARACTER_LENGTH];
char key[MAX_CHARACTER_LENGTH];
char ciphertext[MAX_CHARACTER_LENGTH];

////Helper functions
//function prototoypes to avoid implicit declaration issues
int EstablishConnection(long listenPort);
void ValidFileCheck(char*, char*);
void RequestDecryption(int socketFD);


//sets up client address and connects to address at given user port
int EstablishConnection(long listenPort) {
    //create listen socket
    int listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == -1) {
        fprintf(stderr,"Client: Error creating listen socket.");
        exit(EXIT_FAILURE);
    }

    //get IP address of sever (default: localhost)
    struct hostent* serverHostInfo = gethostbyname("localhost");
    if (serverHostInfo == NULL) {
        fprintf(stderr,"Client: Cannot resolve hostname server address for listen socket.");
        exit(EXIT_FAILURE);
    }

    //setup port address of server
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(listenPort);

    memcpy((char*)&serverAddress.sin_addr.s_addr,
           serverHostInfo->h_addr_list[0], serverHostInfo->h_length);

    //connecting socket to server (localhost)
    int connectStatus = connect(listenSocket, (struct sockaddr*) &serverAddress, sizeof(serverAddress));
    if (connectStatus == -1) {
        fprintf(stderr,"Client: Cannot connect socket to server.' port.");
        exit(EXIT_FAILURE);
    }

//    printf("Client: Connected to server at port %d\n", ntohs(serverAddress.sin_port));

    //send own identity to otp_enc_d via own program name
    char programName[15];
    memset(programName, '\0', sizeof(programName));
    strcpy(programName, "otp_dec");
    int sendIdentity = (int) send(listenSocket, programName, sizeof(programName), 0);
    if (sendIdentity == -1)
        fprintf(stderr,"Client: error writing identity to socket.");
    if (sendIdentity < strlen(programName))
        fprintf(stderr,"Client: not all of identity written to socket.");

    //receive identity from otp_enc_d for verification
    memset(programName, '\0', sizeof(programName));
    int receiveIdentity = (int) recv(listenSocket, programName, sizeof(programName), 0);
    if (receiveIdentity == -1)
        fprintf(stderr,"Client: error reading identity from socket.");

    //compare identity; disconnect if it's not otp_enc_d
    if (strcmp(programName, "otp_dec_d") != 0) {
        fprintf(stderr, "Client: server at port %d is not otp_dec_d.\n", ntohs(serverAddress.sin_port));
        close(listenSocket);
        exit(2);
    }
//    else
//        printf("Client: identity of server otp_dec_d safely established.\n");

    return listenSocket;
}

//opens parameter file, checks for bad characters, and process them into strings
void ValidFileCheck(char* ciphertextFile, char* keyFile) {
    memset(ciphertext, '\0', MAX_CHARACTER_LENGTH);
    memset(key, '\0', MAX_CHARACTER_LENGTH);
    int ciphertextCount = 0, keyCount = 0;

    //open ciphertext
    FILE *fileDescriptor = fopen(ciphertextFile, "r");
    if (fileDescriptor == NULL) {
        fprintf(stderr,"Client: Cannot open ciphertext or key for reading.");
        exit(EXIT_FAILURE);
    }

    //goes through and copies ciphertext to array; removes newline at the end
    int c;
    while ((c = fgetc(fileDescriptor)) != EOF) {
        //c is space or A-Z: valid
        if (c == 32 || (c >= 65 && c <= 90)) {
            ciphertext[ciphertextCount] = (char) c;
            ciphertextCount++;
        }
            //c is something other than a valid character or newline at the end
        else if (c != 10) {
            fprintf(stderr,"Client: Bad character detected in ciphertext file: '%s'.", ciphertextFile);
            exit(EXIT_FAILURE);
        }
    }
    fclose(fileDescriptor);

    //open key
    fileDescriptor = fopen(keyFile, "r");
    if (fileDescriptor == NULL) {
        fprintf(stderr,"Client: Cannot open plaintext or key for reading.");
        exit(EXIT_FAILURE);
    }

    //goes through and copies key to array; removes newline at the end
    while ((c = fgetc(fileDescriptor)) != EOF) {
        //c is space or A-Z: valid
        if (c == 32 || (c >= 65 && c <= 90)) {
            key[keyCount] = (char) c;
            keyCount++;
        }
            //c is something other than a valid character or newline at the end
        else if (c != 10) {
            fprintf(stderr,"Client: Bad character detected in key file: '%s'.", keyFile);
            exit(EXIT_FAILURE);
        }
    }

    //verify plaintext is longer than key
    if (keyCount < ciphertextCount) {
        fprintf(stderr,"Client: Key is shorter than ciphertext.");
        exit(EXIT_FAILURE);
    }
        //if key is longer than plaintext, then shorten it since we don't need keys after ciphertext ends
    else if (keyCount > ciphertextCount) {
        for (int i = ciphertextCount; i < keyCount; i++)
            key[i] = '\0';
    }
}

//sends the ciphertext and key to server (otp_dec_d), and stores received plaintext in array
void RequestDecryption(int socketFD) {
    //establish and send value of length - this saves time so receive doesn't read max buffer of array
    long textLength = strlen(ciphertext);
    char strLength[10];
    memset(strLength, '\0', 10);
    sprintf(strLength, "%ld", textLength); //convert long to string

    //send length value
    int sendLength = (int) send(socketFD, strLength, sizeof(strLength), 0);
    if (sendLength == -1)
        fprintf(stderr,"Client: error writing length to socket.");
    if (sendLength < sizeof(strLength))
        fprintf(stderr,"Client: not all of length written to socket.");

    //send ciphertext
    int sendciphertext = (int) send(socketFD, ciphertext, (size_t) textLength, 0);
    if (sendciphertext == -1)
        fprintf(stderr,"Client: error writing ciphertext to socket.");
    if (sendciphertext < strlen(ciphertext))
        fprintf(stderr,"Client: not all of ciphertext written to socket.");

    //send key
    int sendKey = (int) send(socketFD, key, (size_t) textLength, 0);
    if (sendKey == -1)
        fprintf(stderr,"Client: error writing key to socket.");
    if (sendKey < strlen(key))
        fprintf(stderr,"Client: not all of key written to socket.");

    //receive plaintext
    memset(plaintext, '\0', sizeof(ciphertext));
    int receiverplaintext = (int) recv(socketFD, plaintext, (size_t) textLength, 0);
    if (receiverplaintext == -1)
        fprintf(stderr,"Client: error reading plaintext from socket.");
}


////Acts as client. Sends to server ciphertext/key and gets & outputs corresponding plaintext
//format: otp_dec ciphertext key port
int main(int argc, char *argv[]) {
    //checks if it's in the correct format of "otp_dec <ciphertext> <key> <server port>"
    if (argc != 4) {
        fprintf(stderr,"Client: Invalid number of arguments");
        exit(EXIT_FAILURE);
    }

    //check ciphertext and key files for any bad characters and process them as strings
    ValidFileCheck(argv[1], argv[2]);

    //convert port int and store as number
    char *ptr;
    long listenPort;
    errno = 0;
    listenPort = strtol(argv[3], &ptr, 10);
    if (errno != 0 && listenPort == 0) { //check valid integer entered
        fprintf(stderr,"Client: Invalid port entered");
        exit(EXIT_FAILURE);
    }

    //creates connection to given port on localhost (server)
    int socketFD = EstablishConnection(listenPort);

    //sends ciphertext and key to be encrpyted
    RequestDecryption(socketFD);

    //prints processed plaintext to stdout
    printf("%s\n", plaintext);

    close(socketFD);
    return 0;
}