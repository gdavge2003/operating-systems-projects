#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/time.h>
#include <errno.h>

//keygen takes in a single argument for length, and prints out on std
//a random string of that length consisting of A-Z and space
int main(int argc, char *argv[]) {
    //checks if it's in the correct format of "keygen <len of key>"
    if (argc != 2) {
        perror("Invalid number of arguments");
        exit(EXIT_FAILURE);
    }

    //convert string int and store as a long
    char *ptr;
    long strLength;
    errno = 0;
    strLength = strtol(argv[1], &ptr, 10);
    if (errno != 0 && strLength == 0) { //check valid integer entered
        perror("Invalid numerical length entered");
        exit(EXIT_FAILURE);
    }

    //setup for processing
    srand((unsigned int)time(0));
    char keyPool[27];
    keyPool[0] = 32; //have character pool to include space
    for (int i = 1; i < 27; i++) //fill in the rest of pool for A-Z
        keyPool[i] = (char)(64+i);

    //create array with the randomized key
    char key[strLength+2]; //length of key + \n + \0
    memset(key, '\0', strLength+2);
    key[strLength] = '\n'; //set 2nd to last as newline

    //fill rest with randomized char
    for (int i = 0; i < strLength; i++)
        key[i] = keyPool[rand() % 27];

    printf(key);


    return 0;
}