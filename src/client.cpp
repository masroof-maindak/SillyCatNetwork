#include "../lib/cpp-httplib/httplib.h"
#include <iostream>

#define BUFFER_SIZE 1024

int main(void) {
    using namespace httplib;

    Server svr;

    svr.Post("/content_receiver", [&](const httplib::Request &req, httplib::Response &res, const httplib::ContentReader &content_reader) {
        if (req.is_multipart_form_data()) {
            //for JSON data
        } else {
            std::string body;
            content_reader([&](const char *data, size_t data_length) {
                body.append(data, data_length);
                return true;
            });

            //init client socket
            int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (clientSocket == -1) {
                perror("Socket creation failed");
                exit(EXIT_FAILURE);
            }

            //bind it
            sockaddr_in serverAddr;
            serverAddr.sin_family = AF_INET;    // ipv4
            serverAddr.sin_port = htons(9989);  // port

            if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) < 1) {
                perror("Invalid address or address not supported");
                exit(EXIT_FAILURE);
            }

            if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
                perror("Connection failed");
                exit(EXIT_FAILURE);
            }

            std::string query = body;
            std::cout << "QUERY WE ARE SENDING TO DB: " << query << "\n";

            int stringSize = query.size();
            if (send(clientSocket, &query[0], stringSize, 0) == -1) {
                perror("Sending query to server failed");
                close(clientSocket);
                exit(EXIT_FAILURE);
            }

            char* buffer = new char[BUFFER_SIZE];
            std::memset(buffer, 0, BUFFER_SIZE);
            int bytesRead = 10;

            bytesRead = recv(clientSocket, buffer, BUFFER_SIZE, 0);
            if (bytesRead == -1) {
                perror("Failed to recieve message");
                exit(1);
            } else if (bytesRead == 0) {
                std::cout << "Client disconnected\n";
                exit(1);
            }
            
            std::string serverResponse = std::string(buffer, bytesRead);
            delete[] buffer;
            std::cout << "RESPONSE WE GOT FROM THE SERVER: " << serverResponse << "\n\n";

            close(clientSocket);
            
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");

            res.set_content(serverResponse, "application/json");
        }
    });

    svr.listen("localhost", 1234);
}
