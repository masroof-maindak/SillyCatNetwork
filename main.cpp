#include <iostream>
#include <fstream>

#include <unistd.h>      //socket-handling + system-level operations
#include <sys/socket.h>  //for sockets

#include <netinet/in.h>  //network structures, e.g sockaddr_in, used in conjunction with;
#include <arpa/inet.h>   //handling IP addresses + converting b/w host & network addresses
#include <pthread.h>     //threads

#include <random>        //thread image ID generation
#include <vector>        //store image
#include <utility>       //pair
#include <queue>         //processing/answer queues
#include <chrono>        //answer queue pops un-picked-up answer

#define SLEEP_TIME 690000 //0.69s
#define BUFFER_SIZE 512
const char delimiter = '`';

using namespace std;

//structs
struct answer {
    int transactionID;
    std::string retMessage;
};

//global queues
queue<std::string> filesBeingProcessed;
queue<answer> answerQueue;

#include "graph.h"

graph g(0);

//client thread
void* receiveImage(void* clientSocketPtr) {
    int clientSocket = *((int*)clientSocketPtr);
    free(clientSocketPtr);
    std::ofstream logger("_data/logs.txt", std::ios::app);

    char buffer[BUFFER_SIZE];
    ssize_t bytesRead = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (bytesRead < 1) {
        perror("Error receiving query");
        close(clientSocket); pthread_exit(NULL);
    }

    std::string query = std::string(buffer, bytesRead);
    logger << time(0) << " | REQUEST: " << query << "\n";

    //Now, to parse the query and call graph functions accordingly:
    //remove transaction ID and function name from query

    //first word of query is the transaction ID
    int transaID = std::stoi(query.substr(0, query.find(delimiter)));
    query = query.substr(query.find(delimiter) + 1);

    //second word of query is the function name
    std::string functionToCall = query.substr(0, query.find(delimiter));
    query = query.substr(query.find(delimiter) + 1);

    std::cout << "Query after removing stuff.. " << query << "\n";

    //the rest of the query is now the arguments to the function, convert them to a vector
    std::vector<std::string> arguments;
    while (query.find(delimiter) != std::string::npos) {
        arguments.push_back(query.substr(0, query.find(delimiter)));
        query = query.substr(query.find(delimiter) + 1);
    }

    std::cout << "Received transaction ID: " << transaID << "\n";
    std::cout << "Received function: " << functionToCall << "\n";
    std::cout << "Received arguments: ";
    for (int i = 0; i < arguments.size(); i++) {
        std::cout << arguments[i] << " ";
    }

    /*
    * Call one of the graph functions accordingly.
    * They will push a success/failure message to the answer queue
    * along with the transaction ID, which will be used to identify the answer
    * Except for the filter and relationalQuery functions, which will push the answer to the answer queue
    */

   if (functionToCall == "addVertex") {
        //arguments: transactionID, vertexID, vertexType, vertexProperties
        g.addVertex(transaID, arguments[0], arguments[1], arguments[2]);
    } else if (functionToCall == "addEdge") {
        //arguments: transactionID, edgeTypeLabel, bidirectional, vertex1ID, vertex2ID, vertex1Type, vertex2Type
        g.addEdge(transaID, arguments[0], stoi(arguments[1]), arguments[2], arguments[3], arguments[4], arguments[5]);
    } else if (functionToCall == "mergeVertex") {
        //arguments: transactionID, vertexID, vertexType, vertexProperties
        g.mergeVertex(transaID, arguments[0], arguments[1], arguments[2]);
    } else if (functionToCall == "removeVertex") {
        //arguments: transactionID, vertexID, vertexType
        g.removeVertex(transaID, arguments[0], arguments[1]);
    } else if (functionToCall == "removeEdge") {
        //arguments: transactionID, edgeTypeLabel, bidirectional, vertex1ID, vertex2ID, vertex1Type, vertex2Type
        g.removeEdge(transaID, arguments[0], stoi(arguments[1]), arguments[2], arguments[3], arguments[4], arguments[5]);
    } else if (functionToCall == "filter") {
        //arguments: transactionID, vertexType, vertexProperties
        g.filter(transaID, arguments[0], arguments[1]);
    } else if (functionToCall == "relationalQuery") {
        //arguments: transactionID, vertex1ID, vertex1Type, vertex2Type, edgeTypeLabel, properties
        g.relationalQuery(transaID, arguments[0], arguments[1], arguments[2], arguments[3], arguments[4]);
    } else if (functionToCall == "fetchVertexProperties") {
        //arguments: transactionID, vertexID, vertexType
        g.fetchVertexProperties(transaID, arguments[0], arguments[1]);
    } else if (functionToCall == "addVE") {
        //arguments: transactionID, edgeTypeLabel, bidrectional, vertex1ID, vertex2ID, vertex1Type, vertex2Type, vertex2Properties
        g.addVE(transaID, arguments[0], stoi(arguments[1]), arguments[2], arguments[3], arguments[4], arguments[5], arguments[6]);
    } else if (functionToCall == "removeVE") {
        //arguments: transactionID, edgeTypeLabel, bidrectional, vertex1ID, vertex2ID, vertex1Type, vertex2Type
        g.removeVE(transaID, arguments[0], stoi(arguments[1]), arguments[2], arguments[3], arguments[4], arguments[5]);
    } else {

        std::cout << "Invalid function name: " << functionToCall << "\n";
        //cleanup
        close(clientSocket);
        logger.close();
        return NULL;
    }

    std::string feedbackResponse;

    while(true) {
        if (!answerQueue.empty()) {
            answer temp = answerQueue.front();
            //confirming if element at top of Queue is the one generated for this thread
            if (temp.transactionID == transaID) {
                //if it is, remove it from the answer queue
                answerQueue.pop();
                //store the answer generated for this thread
                feedbackResponse = temp.retMessage;
                //and exit the whileloop
                break;
            }
        } else {
            usleep(SLEEP_TIME); 
        }
    }

    //send the feedback message to the client
    int feedbackSize = feedbackResponse.size();
    if (send(clientSocket, &feedbackResponse[0], feedbackSize, 0) == -1) {
        perror("Sending char count failed");
        close(clientSocket); pthread_exit(NULL);
    }

    //cleanup + closing client socket
    std::cout << "Successfully sent character count. Closing client socket." << std::endl;
    close(clientSocket);
    pthread_exit(NULL);
}

//answer thread
void* readAnswerQueue(void* arg) {
    std::chrono::seconds timeout(5);

    //forever
    while (true) {
        //if answerQ not emprty
        if (!answerQueue.empty()) {
            //current answer = front of queue
            std::string currAns = answerQueue.front().retMessage;
            std::string prevValue = currAns;

            if (answerQueue.size() == 1 || currAns != prevValue) {
                prevValue = currAns;
                auto startTime = std::chrono::steady_clock::now();
                while (true) {
                    //check elapsed time
                    auto elapsed_time = std::chrono::steady_clock::now() - startTime;
                    //if it exceeds our time out value
                    if (elapsed_time >= timeout) {
                        //leave inner while
                        break;
                    }

                    // Sleep for a short duration to avoid busy-waitings
                    struct timespec sleepTime;
                    sleepTime.tv_sec = 0;
                    sleepTime.tv_nsec = 100000000; //100 ms
                    nanosleep(&sleepTime, NULL);
                }
            }

            if (currAns == answerQueue.front().retMessage) {
                //value at queue front still matches previously recorded; pop it.
                answerQueue.pop();
                std::cout << "Popped value: " << currAns << "; Client might have disconnected" << std::endl;
            }
        } else {
            usleep(SLEEP_TIME);
        }
    }
    return NULL;
}

int main() {
    pthread_t answerThread; //pops if the top of the queue remains unchanged for a fixed time period
    if (pthread_create(&answerThread, NULL, readAnswerQueue, NULL) != 0) {
        perror("Answer thread creation failed"); return 1;
    }

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        perror("Socket creation failed"); return 1;
    }

    int reuse = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("setsockopt"); return 1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(9989);  //port 9989 basically - htons = host TO network short (like the data type)
    serverAddr.sin_addr.s_addr = INADDR_ANY; //// All available network interfaces - e.g Wifi/Ethernet/Bluetooth, etc.

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Binding failed"); return 1;
    }

    if (listen(serverSocket, 5) == -1) {
        perror("Listening failed"); return 1;
    }

    std::cout << "Server listening on port 9989..." << std::endl;

    while (true) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        int* clientSocket = (int*)malloc(sizeof(int));
        *clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);

        
        if (*clientSocket == -1) {
            perror("Failed to accept connection!"); return 1;
        }

        pthread_t clientThread;
        if (pthread_create(&clientThread, NULL, receiveImage, (void*)clientSocket) != 0) {
            perror("Thread creation failed"); return 1;
        }

        pthread_detach(clientThread); // Detach the thread to allow it to clean up automatically
    }

    close(serverSocket);
    return 0;
}