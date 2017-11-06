/****************************************************************************/
/*****                                                                   ****/
/***** Texas A&M University                                              ****/
/***** Developers : Han Bee Oh (826008965)                               ****/
/*****              Ramakrishna Prabhu (725006454)                       ****/
/***** Filename   : tftpServer.c                                         ****/
/*****                                                                   ****/
/****************************************************************************/
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "tftpServer.h"

// Description : Prints the error message as per errnno
// Parameters  : funcName - Name of the funcrion which returned error
// Return      : NONE
void printErrMsg (const char* funcName)
{
    char *errMsg = NULL;

    // strerror function gets appropriate error message as per the errno set
    errMsg = strerror (errno);
    if (NULL != funcName)
    {
        printf ("\n!!!!    '%s' returned with error \n", funcName);
    }
    printf ("\n!!!!    Error : %s  !!!!\n", errMsg);

    return;
}

// Description : Parses the request 
// Parameters  : buffer - Contains information sent from client
//               reqPkt - structure to map reqPkt
// Return      : NONE
void parseReq (char *buffer, struct REQPKT *reqPkt)
{
    if ((NULL == buffer) || (NULL == reqPkt))
    {
        return;
    }

    reqPkt->opcode = ntohs(*((uint16_t*)buffer));
}

// Description : Uses wait for reapting the child process
// Return      : NONE
void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

// Description : Forms and sends error message
// Parameters  : errNumber - represents the error number set in the background
//               clientAddr - client information
// Return      : NONE
void sendErrMsg (int errNumber, struct sockaddr_in clientAddr)
{
    int childFd = 0;
    struct sockaddr_in childAddr;
    socklen_t addrlen = sizeof (struct sockaddr_in);
    struct ERRPKT errPkt ;                 // to form the error packet
    int errType = FILE_NOT_FOUND;          // stores the type of the error occured
    int bufferSize = 0;

    memset (&childAddr, 0, sizeof (struct sockaddr_in));
    memset (&errPkt, 0, sizeof (struct ERRPKT));

    // Start to send the data
    if ((childFd = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printErrMsg ("socket");
        return;
    }

    // Child server information
    childAddr.sin_family = AF_INET;
    childAddr.sin_addr.s_addr = htonl (INADDR_ANY); // Taking local IP
    childAddr.sin_port = htons(0);                  // Bind will assign a ephemeral port

    if (bind(childFd, (struct sockaddr*) &childAddr, sizeof (childAddr)) < 0)
    {
        printErrMsg ("bind");

        return;
    }

    errPkt.opcode = htons(ERR);
    if (errNumber == EACCES)
    {
        errType = ACCESS_VIOLATION;
    }
    errPkt.errCode = htons(errType);
    snprintf (errPkt.errMsg, sizeof (errPkt.errMsg), "%s", errMsg[errType]);
    bufferSize = sizeof (errPkt.opcode) + sizeof (errPkt.errCode) + strlen (errPkt.errMsg) + 1;
    sendto (childFd, (void *)(&errPkt), bufferSize, 0, (struct sockaddr *) &clientAddr, addrlen);
}

// Description : sends the data to the client
// Parameters  : filePath - Contains the file path which is being requested
//               modeStr - The mode being requested by client, netascii/octet
//               clientAddr - Contains the information client address
// Return      : 1 - Success
//               0 - False
int sendData (char* filePath, char* modeStr, struct sockaddr_in clientAddr)
{
    struct DATAPKT dataPkt;                           // Provides the structure to prepare the data packet
    int mode = MODE_NETASCII;                         // Saves the mode in which the data will be sent
    FILE *fp = NULL;
    uint16_t blockCnt = 1;                            // Block count will have the block number that needs to be sent next in the pkt
    uint16_t count = 0;                               // Stores number of bytes copied to the data pkt
    char character = '\0';                            // To access each character
    char nextChar = -1;                               // To take care of '\r' and '\n' in ascii mode
    int childFd = 0;                                  // New FD to send the data
    struct sockaddr_in childAddr;
    size_t bufferSize = 0;
    bool isEOFReached = false;                        // checks whether the data sent/ being sent is last byte
    int status = 0; 
    socklen_t addrlen = sizeof (struct sockaddr_in);
    char buffer [MAX_DATA_SIZE] = {0};
    uint16_t buffLen = 0;
    uint16_t buffIndex = 0;
    int totalData = 0;
    fd_set readFds;
    struct timeval tv;                                // This is used for timer
    struct ACKPKT ackPkt;                             // Structure to accept the ACK pkt
    int retryCount = 0;                               // This will keep track of retries performed

    if ((NULL == filePath) || (NULL == modeStr))
    {
        printf ("[%s]ERROR : fileName/sockAddr sent is NULL \n", __FILE__);

        return 0;
    }
    
    memset (&dataPkt, 0, sizeof (struct DATAPKT));
    memset (&ackPkt, 0, sizeof (struct ACKPKT));
    memset (&childAddr, 0, sizeof (struct sockaddr_in));
    
    if (0 == strcmp (modeStr , OCTET))
    {
        mode = MODE_OCTET;
    }

    fp = fopen(filePath, "r");

    if (NULL == fp)
    {
        sendErrMsg (errno, clientAddr);
        return 0;
    }
    // Start to send the data
    if ((childFd = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printErrMsg ("socket");
        return 0;
    }

    // Child server information
    childAddr.sin_family = AF_INET;
    // Taking local IP
    childAddr.sin_addr.s_addr = htonl (INADDR_ANY);
    // Bind will assign a ephemeral port
    childAddr.sin_port = htons(0);

    if (bind(childFd, (struct sockaddr*) &childAddr, sizeof (childAddr)) < 0)
    {
        printErrMsg ("bind");

        return 0;
    }

    FD_ZERO (&readFds);
    FD_SET (childFd, &readFds);
    tv.tv_sec = 10;
    tv.tv_usec = 0;

    while (1)
    {
        // Will copy 512 bytes of data or less than that depending of the amount
        for (count = 0; count < MAX_DATA_SIZE; count++)
        {
            if ((buffIndex == buffLen) && (false == isEOFReached))
            {
                memset (buffer, 0, sizeof (buffer));   
                buffIndex = 0;
                buffLen = fread (buffer, 1, sizeof (buffer), fp);
                totalData = totalData + buffLen;
                
                if (buffLen < MAX_DATA_SIZE)
                {
                    isEOFReached = true;
                }
                if (ferror (fp))
                {
                    printf ("ERROR: Read error from getc on local machine \n");
                } 
            }
            if (nextChar >= 0) {
                dataPkt.data [count] = nextChar;
                nextChar = -1;
                continue;
            }
            // To take care of the data set which is less than 512
            if (buffIndex >= buffLen)
            {
                break;
            }
            character = buffer [buffIndex];
            buffIndex ++;
            if (MODE_NETASCII == mode)
            {
                if ('\n' == character)
                {
                    nextChar = character;
                    character = '\r';
                }
                else if ('\r' == character)
                {
                    nextChar = '\0';
                }
                else
                {
                    nextChar = -1;
                }
            }
            dataPkt.data [count] = character;
        }
	dataPkt.opcode = htons (DATA);
        dataPkt.blkNumber = htons (blockCnt % BLK_LIMIT);
        bufferSize = sizeof(dataPkt.opcode) + sizeof (dataPkt.blkNumber) + (count);
        do 
        {
            sendto (childFd, (void *)(&dataPkt), bufferSize, 0, (struct sockaddr *) &clientAddr, addrlen);
            if ((status = select (childFd + 1, &readFds, NULL, NULL, &tv)) <= 0)
            {
                retryCount ++;
                continue;
            }
            else
            {
	       if ((status = recvfrom (childFd, (void *)&ackPkt, sizeof (ackPkt), 0, (struct sockaddr*) &clientAddr, &addrlen)) >= 0)
               {
                   if ((ACK == ntohs (ackPkt.opcode)) && (blockCnt == (ntohs (ackPkt.blkNumber))))
                   {
                       break;
                   }
               }
            }
            memset (&ackPkt, 0, sizeof (struct ACKPKT));
        }while (retryCount < RETRY_LIMIT);
        if (retryCount >= RETRY_LIMIT)
        {
            printf ("ERROR: Timed out, breaking after %d tries \n", retryCount);
            break;
        }
        blockCnt++;
        memset (&dataPkt, 0, sizeof (struct DATAPKT));
        retryCount = 0;
        if ((count < MAX_DATA_SIZE) && (buffIndex == buffLen))
        {
            break;
        }
    }

    fclose (fp);

    return (1);
}

// Description : Function recevies the file sent from client
// Parameters  : filePath - Contains the file path which is being sent
//               modeStr - The mode being requested by client, netascii/octet
//               clientAddr - Contains the information client address
// Return      : 1 - Success
//               0 - False
int recvData (char* filePath, char* modeStr, struct sockaddr_in clientAddr)
{
    struct DATAPKT dataPkt;
    struct ACKPKT ackPkt;
    int childFd = 0;
    int mode = MODE_NETASCII;
    FILE *fp = NULL;
    uint16_t blkCnt = 0;
    uint16_t count = 0;
    char character = '\0';
    struct sockaddr_in childAddr;
    int status = 0;
    socklen_t addrlen = sizeof (struct sockaddr_in);
    struct timeval tv;
    bool errOccr = false;
    uint16_t byteLen = 0;
    uint16_t buffLen = MAX_DATA_SIZE;
    fd_set readFds;
    bool lastCharWasCR = false;

    if ((NULL == filePath) || (NULL == modeStr))
    {
        printf ("[%s]ERROR : fileName/sockAddr sent is NULL \n", __FILE__);

        return 0;
    }

    memset (&dataPkt, 0, sizeof (struct DATAPKT));
    memset (&ackPkt, 0, sizeof (struct ACKPKT));
    memset (&childAddr, 0, sizeof (struct sockaddr_in));

    if (0 == strcmp (modeStr , OCTET))
    {
        mode = MODE_OCTET;
    }

    fp = fopen(filePath, "w");

    if (NULL == fp)
    {
        sendErrMsg (errno, clientAddr);
        return 0;
    }

    // Start to recv data
    if ((childFd = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printErrMsg ("socket");
        return 0;
    }
    
    // Child server information
    childAddr.sin_family = AF_INET;
    // Taking local IP
    childAddr.sin_addr.s_addr = htonl (INADDR_ANY);
    // Bind will assign a ephemeral port 
    childAddr.sin_port = htons(0);

    if (bind(childFd, (struct sockaddr*) &childAddr, sizeof (childAddr)) < 0)
    {
        printErrMsg ("bind");

        return 0;
    }

    FD_ZERO (&readFds);
    FD_SET (childFd, &readFds);
    tv.tv_sec = 10;
    tv.tv_usec = 0;

    while (1)
    {
        ackPkt.opcode = htons (ACK);
        ackPkt.blkNumber = blkCnt;
        sendto (childFd, (void *)(&ackPkt), sizeof(ackPkt), 0, (struct sockaddr *) &clientAddr, addrlen);
        if ((buffLen != MAX_DATA_SIZE) || (true == errOccr))
        {
            break;
        }
        if ((status = select (childFd + 1, &readFds, NULL, NULL, &tv)) <= 0)
        {
            if (0 == status)
            {
                printf ("Timedout while waiting for packet from the host \n");
                break;
            }
            else if (0 > status)
            {
                printErrMsg("select");
                break;
            }
        }
        else
        {
            if ((byteLen = recvfrom (childFd, (void *)&dataPkt, sizeof (dataPkt), 0, (struct sockaddr*) &clientAddr, &addrlen)) >= 0)
            {
                // skip the packet if it is not data
                if (DATA != ntohs (dataPkt.opcode))
                {
                    continue;
                }
                buffLen = byteLen - sizeof(dataPkt.opcode) - sizeof(dataPkt.blkNumber);
                blkCnt = dataPkt.blkNumber;
                
                while (count < buffLen)
                {
                    if (MODE_NETASCII == mode)
                    {
                        if (true == lastCharWasCR)
                        {
                            character = dataPkt.data [count];
                            if ((0 == count) && ('\0' == dataPkt.data [count]))
                            {
                                character = '\r';
                            }
                            count ++;
                            lastCharWasCR = false;
                        }
                        else if ('\r' == dataPkt.data [count])
                        {
                            count ++;
                            if (('\n' == dataPkt.data [count]) && (count < buffLen))
                            {
                                character = dataPkt.data [count];
                                count ++;
                            }
                            else if (('\0' == dataPkt.data [count]) && (count < buffLen))
                            {
                                character = dataPkt.data [count - 1];
                                count ++;
                            }
                            else
                            {
                               if (count >= buffLen)
                               {
                                   if (dataPkt.data [count - 1] == '\r')
                                   {
                                       lastCharWasCR = true; 
                                   }
                                   continue;
                               } 
                            }
                        }
                        else
                        {
                            character = dataPkt.data [count];
                            count ++;
                        }
                    }
                    else if (MODE_OCTET == mode)
                    {
                        character = dataPkt.data [count];
                        count ++;
                    }
                    
                    if (EOF == putc (character, fp))
                    {
                        
                        printErrMsg("putc");
                        printf ("Error : There was an error while writing to the file \n");
                        errOccr = true;
                        break;
                    }
                }
                count  = 0;
                memset (&dataPkt, 0, sizeof (dataPkt));
            }
        }
    }

    fclose (fp);

    return 1;
}

// Description : processes the incoming requests from client
// Parameters  : buffer - buffer contains the request information got from client
//               clientAddr - Contains the information client address
//               tftpFolderPath - path provided by the admin of the tftp server
// Return      : NONE
void processTheRequest (void* buffer, struct sockaddr_in clientAddr, char *tftpFolderPath)
{
    int requestType = NONE;
    struct REQPKT *reqPkt = buffer;
    char filePath [MAX_FILE_PATH]= {0};

    parseReq (buffer, reqPkt);
  
    if ((NULL == reqPkt) || (NULL == tftpFolderPath))
    {
        printErrMsg ("processTheRequest");
        return;
    }

    switch (reqPkt->opcode)
    {
        case RRQ:
            {
                strncat (filePath, tftpFolderPath, sizeof (filePath));
                strcat (filePath, "/");
                strncat (filePath, reqPkt->fileName, (sizeof (filePath) - strlen (filePath)));
                sendData (filePath, (char *)(buffer + OPCODE_SIZE + strlen (reqPkt->fileName) + 1), clientAddr);
            }
            break; 

        case WRQ:
            {
                strncat (filePath, tftpFolderPath, sizeof (filePath));
                strcat (filePath, "/");
                strncat (filePath, reqPkt->fileName, (sizeof (filePath) - strlen (filePath)));
                recvData (filePath, (char *)(buffer + OPCODE_SIZE + strlen (reqPkt->fileName) + 1), clientAddr);
            }
            break;

        default :
            printf ("Invalid request from the client\n");
    }
}

int main (int argc, char** argv)
{
    int listenFd = 0;
    struct sockaddr_in servAddr;
    struct sockaddr_in clientAddr; 
    socklen_t addrlen = sizeof(struct sockaddr_in);
    char port[MAX_PORT_NUMBER_SIZE] = {0};   // Stores the port number in string format
    char buffer[MAX_BUFFER_SIZE] = {0};      // Buffer to save the information sent from client
    char ip[INET_ADDRSTRLEN] = {0};
    int pid = 0;
    int ret = 0;
    struct sigaction sa;
   

    if (argc != 3)
    {
        printf ("\nPlease provide the command in the following syntax \n"
                "\nSyntax :\n"
                "           ./tftps PORT_NUM FOLDER_PATH\n"
                "\nExample : ./tftps 8080 /home/tftpfolder/\n");
        return 1;
    }
    else
    {
        // Just an helper option, this will print syntax when echos is executed as "./echos -h"
        if (0 == strcmp (argv [1], "-h"))
        {
            printf ("\nSynatx  : ./tftps PORT_NUM FOLDER_PATH\n\n"
                    "\nExample : ./tftps 8080 /home/tftpfolder/\n");
            return 1;
        }
    }

    if ((listenFd = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printErrMsg ("socket");
        return 1;
    }

    memset (&servAddr, 0, sizeof (servAddr));
    memset (&clientAddr, 0, sizeof (clientAddr));

    if (atoi (argv[1]) > MAX_PORT_NUMBER)
    {
        printf ("Error : Port number provided is beyond the max value 65535 \n"
                "        Please provide a port number which is within the range of 1025-65535 \n");
        return 1;
    }
    else if (atoi (argv[1]) <= 0)
    {
        printf ("Error : Port number provided is less than 1, which is invalid\n"
                "        Please provide a port number which is within the range of 1025-65535 \n");
        return 1;
    }

    // Server Information
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl (INADDR_ANY); // Taking local IP
    servAddr.sin_port = htons(atoi(argv[1]));      // PORT number got from command line

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // This for loop is used to take care of the scenario where the PORT number provided is already in use
    while (bind(listenFd, (struct sockaddr*) &servAddr, sizeof (servAddr)) < 0)
    {
        // Check for port already in use error
        if (EADDRINUSE == errno)
        {
            printf ("Port %s is occupied by some other process, please choose any other free port\n", argv [1]);
            printf ("Port : ");
            scanf ("%s", port);
            if (MAX_PORT_NUMBER < atoi (port))
            {
                printf ("Error : Port number provided is beyond the max value 65535 \n"
                        "        Please provide a port number which is within the range of 1025-65535 \n");
                return 1;
            }
            else if (0 >= atoi (port))
            {
                printf ("Error : Port number provided is less than 1, which is invalid\n"
                        "        Please provide a port number which is within the range of 1025-65535 \n");
                return 1;
            }
            servAddr.sin_port = htons(atoi(port));
        }
        else
        {
            printErrMsg ("bind");
            return 1;
        }
    }

    while (1)
    {
        // Initializing the variable for every loop
        memset (&clientAddr, 0, sizeof (clientAddr));
        memset (&buffer, 0, sizeof (buffer));
        addrlen = sizeof (clientAddr);
        recvfrom (listenFd, (void *)buffer, sizeof(buffer), 0, (struct sockaddr *)&clientAddr, &addrlen);
        inet_ntop( AF_INET, &(clientAddr.sin_addr), ip, INET_ADDRSTRLEN);
        if ((pid = fork()) ==  -1)
        {
            printErrMsg ("fork");
        }
        else if (pid == 0)
        {
            processTheRequest ((void *)buffer, clientAddr, argv [2]);
            break;
        }
    }

    return 0; 
}
