# Compile server
g++ -std=c++17 -Wall -O2 server.cpp -o server -lpthread

# Compile client
g++ -std=c++17 -Wall -O2 client.cpp -o client -lpthread

